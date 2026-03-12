#include "SpectralDelay.h"
#include <cmath>
#include <algorithm>
#include <random>

// Static constexpr definitions
constexpr float SpectralDelay::majorProfile[12];
constexpr float SpectralDelay::minorProfile[12];

SpectralDelay::SpectralDelay()
{
    // Generate fixed random phase offsets for stereo spread chaos
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 6.283185f);
    for (auto& p : binPhaseOffsets)
        p = dist(rng);

    // Init chroma and key mask
    chromaAccum.fill(0.0f);
    keyMask.fill(1.0f);  // All bins pass by default
    yinBuffer.fill(0.0f);
}

void SpectralDelay::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    samplesUntilNextHop = 0;
    hopCount = 0;

    for (auto& ch : channels)
    {
        ch.inputBuffer.resize(FFT_SIZE, 0.0f);
        ch.outputBuffer.resize(FFT_SIZE * 2, 0.0f);
        ch.inputWritePos = 0;
        ch.outputWritePos = 0;

        ch.delayLine.resize(MAX_DELAY_FRAMES);
        for (auto& frame : ch.delayLine)
            frame.fill({0.0f, 0.0f});
        ch.delayWritePos = 0;

        ch.frozenFrame.fill({0.0f, 0.0f});
        ch.envelopeFollower.fill(0.0f);
    }
}

void SpectralDelay::reset()
{
    for (auto& ch : channels)
    {
        std::fill(ch.inputBuffer.begin(), ch.inputBuffer.end(), 0.0f);
        std::fill(ch.outputBuffer.begin(), ch.outputBuffer.end(), 0.0f);
        for (auto& frame : ch.delayLine)
            frame.fill({0.0f, 0.0f});
        ch.frozenFrame.fill({0.0f, 0.0f});
        ch.envelopeFollower.fill(0.0f);
    }
}

void SpectralDelay::process(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = std::min(buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int s = 0; s < numSamples; ++s)
    {
        // Feed mono mix into YIN buffer for pitch detection
        float monoSample = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            monoSample += buffer.getSample(c, s);
        monoSample /= static_cast<float>(numChannels);

        yinBuffer[yinWritePos] = monoSample;
        yinWritePos = (yinWritePos + 1) % YIN_BUFFER_SIZE;

        // Run pitch detection every YIN_HOP samples (when key-aware is active)
        if (keyAware > 0.01f && ++yinSampleCount >= YIN_HOP)
        {
            yinSampleCount = 0;
            // Build contiguous buffer for YIN (unwrap ring buffer)
            float yinContiguous[YIN_BUFFER_SIZE];
            for (int i = 0; i < YIN_BUFFER_SIZE; ++i)
                yinContiguous[i] = yinBuffer[(yinWritePos + i) % YIN_BUFFER_SIZE];

            float pitchHz = detectPitchYIN(yinContiguous, YIN_BUFFER_SIZE);
            if (pitchHz > 50.0f && pitchHz < 2000.0f)
            {
                updateChromaAndKey(pitchHz);
                rebuildKeyMask();
            }
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[c];
            float input = buffer.getSample(c, s);

            ch.inputBuffer[ch.inputWritePos] = input;
            ch.inputWritePos = (ch.inputWritePos + 1) % FFT_SIZE;

            float wet = ch.outputBuffer[ch.outputWritePos];
            ch.outputBuffer[ch.outputWritePos] = 0.0f;
            ch.outputWritePos = (ch.outputWritePos + 1) % (FFT_SIZE * 2);

            buffer.setSample(c, s, input * (1.0f - mix) + wet * mix);
        }

        if (--samplesUntilNextHop <= 0)
        {
            for (int c = 0; c < numChannels; ++c)
                processHop(c);
            samplesUntilNextHop = HOP_SIZE;
        }
    }
}

void SpectralDelay::processHop(int channel)
{
    auto& ch = channels[channel];

    // ── Forward FFT ──────────────────────────────────────────────
    for (int i = 0; i < FFT_SIZE; ++i)
    {
        int idx = (ch.inputWritePos + i) % FFT_SIZE;
        fftWorkspace[i] = ch.inputBuffer[idx];
    }
    for (int i = FFT_SIZE; i < FFT_SIZE * 2; ++i)
        fftWorkspace[i] = 0.0f;

    window.multiplyWithWindowingTable(fftWorkspace.data(), FFT_SIZE);
    fft.performRealOnlyForwardTransform(fftWorkspace.data());

    std::array<std::complex<float>, 513> currentFrame;
    for (int bin = 0; bin < NUM_BINS; ++bin)
        currentFrame[bin] = { fftWorkspace[bin * 2], fftWorkspace[bin * 2 + 1] };

    // Store current input in delay line
    ch.delayLine[ch.delayWritePos] = currentFrame;

    // ── TIME: Quadratic mapping for extended range ───────────────
    // At max: 512 frames × 256 hop / 44100 ≈ 3 seconds of spectral delay
    int baseDelay = static_cast<int>(time * time * (MAX_DELAY_FRAMES - 1));

    // ── TILT: Extreme spectral dispersion (4× range) ────────────
    // tilt=0 → lows delayed 4×, highs near-instant
    // tilt=1 → highs delayed 4×, lows near-instant
    const float tiltRange = 4.0f;

    // ── DIRECTION: Forward / reverse / frequency mirror ─────────
    // 0.0 = full reverse + freq mirror (alien)
    // 0.3 = reverse playback only
    // 0.5 = stutter zone (very short random reads)
    // 1.0 = normal forward
    float dirMix = 0.0f;
    bool mirrorBins = false;
    if (direction < 0.5f)
    {
        dirMix = 1.0f - (direction / 0.5f);      // 1.0 at dir=0, 0.0 at dir=0.5
        mirrorBins = direction < 0.2f;             // Bin mirroring below 0.2
    }

    // ── ENVELOPE: Aggressive spectral gate ──────────────────────
    // envelope=0 → HARD gate, only top frequencies survive
    // envelope=1 → everything passes
    float gateThreshold = (1.0f - envelope) * (1.0f - envelope) * 0.5f;
    float gateAttack  = 0.3f + envelope * 0.7f;
    float gateRelease = 0.05f + envelope * 0.25f;

    // ── FEEDBACK: Self-oscillation curve ────────────────────────
    // Quadratic scaling: reaches 1.15 at max for controlled self-oscillation
    float fbk = feedback * feedback * 1.15f;

    // ── Process each bin ────────────────────────────────────────
    std::array<std::complex<float>, 513> outputFrame;

    for (int bin = 0; bin < NUM_BINS; ++bin)
    {
        float binNorm = static_cast<float>(bin) / static_cast<float>(NUM_BINS);

        // Per-bin delay from tilt
        float tiltFactor = 1.0f + (tilt - 0.5f) * tiltRange * (binNorm * 2.0f - 1.0f);
        tiltFactor = std::max(0.0f, tiltFactor);
        int binDelay = std::clamp(static_cast<int>(baseDelay * tiltFactor), 0, MAX_DELAY_FRAMES - 1);

        // Forward read position
        int fwdPos = (ch.delayWritePos - binDelay + MAX_DELAY_FRAMES) % MAX_DELAY_FRAMES;
        // Reverse read position (reads from opposite end of delay)
        int revPos = (ch.delayWritePos - (MAX_DELAY_FRAMES - 1 - binDelay) + MAX_DELAY_FRAMES) % MAX_DELAY_FRAMES;

        int readBin = mirrorBins ? (NUM_BINS - 1 - bin) : bin;

        // ── FREEZE: Continuous crossfade with spectral drift ────
        if (freeze > 0.99f)
        {
            outputFrame[bin] = ch.frozenFrame[bin];
            // Spectral drift: frozen frame slowly smears between neighbors
            if (bin > 0 && bin < NUM_BINS - 1)
            {
                ch.frozenFrame[bin] = ch.frozenFrame[bin] * 0.997f
                    + (ch.frozenFrame[bin - 1] + ch.frozenFrame[bin + 1]) * 0.0015f;
            }
            continue;
        }

        // Read delayed frames
        auto delayedFwd = ch.delayLine[fwdPos][bin];
        auto delayedRev = ch.delayLine[revPos][readBin];

        // Direction crossfade
        auto delayed = delayedFwd * (1.0f - dirMix) + delayedRev * dirMix;

        // ── FEEDBACK: Inject with soft-clip saturation ──────────
        auto fbkSignal = delayed * fbk;
        float fbMag = std::abs(fbkSignal);
        if (fbMag > 0.001f)
        {
            // tanh saturation prevents true blowup while allowing self-oscillation
            float saturated = std::tanh(fbMag * 1.5f) / 1.5f;
            fbkSignal *= (saturated / fbMag);
        }
        ch.delayLine[ch.delayWritePos][bin] += fbkSignal;

        // Cross-bin feedback bleed: creates spectral smearing at high feedback
        if (feedback > 0.3f && bin > 0 && bin < NUM_BINS - 1)
        {
            float crossFbk = (feedback - 0.3f) * 0.15f;
            ch.delayLine[ch.delayWritePos][bin - 1] += delayed * crossFbk * 0.5f;
            ch.delayLine[ch.delayWritePos][bin + 1] += delayed * crossFbk * 0.5f;
        }

        // Freeze crossfade (partial freeze creates ghostly sustain)
        if (freeze > 0.01f)
        {
            delayed = delayed * (1.0f - freeze) + ch.frozenFrame[bin] * freeze;
        }
        ch.frozenFrame[bin] = currentFrame[bin];

        outputFrame[bin] = delayed;

        // ── ENVELOPE: Per-bin spectral gate with dynamics ───────
        float mag = std::abs(outputFrame[bin]);

        if (mag > ch.envelopeFollower[bin])
            ch.envelopeFollower[bin] += (mag - ch.envelopeFollower[bin]) * gateAttack;
        else
            ch.envelopeFollower[bin] += (mag - ch.envelopeFollower[bin]) * gateRelease;

        float envMag = ch.envelopeFollower[bin];
        if (envMag < gateThreshold)
        {
            float gateGain = envMag / std::max(gateThreshold, 0.0001f);
            gateGain *= gateGain;          // Quadratic for hard-knee gate
            outputFrame[bin] *= gateGain;
        }
    }

    // ── SPREAD: Wild stereo phase scattering ────────────────────
    // At max: bins are flung across a 6π stereo field with random offsets
    if (channel == 1 && spread > 0.01f)
    {
        for (int bin = 0; bin < NUM_BINS; ++bin)
        {
            float angle = spread * 6.0f * 3.14159265f
                          * static_cast<float>(bin) / static_cast<float>(NUM_BINS);
            angle += binPhaseOffsets[bin] * spread;   // Per-bin random scatter
            auto rotation = std::complex<float>(std::cos(angle), std::sin(angle));
            outputFrame[bin] *= rotation;
        }
    }

    // ── KEY-AWARE FILTERING: Attenuate out-of-key bins in delay tails ──
    if (keyAware > 0.01f)
    {
        for (int bin = 0; bin < NUM_BINS; ++bin)
        {
            // Blend between no filtering (1.0) and full key mask
            float maskGain = 1.0f - keyAware * (1.0f - keyMask[bin]);
            outputFrame[bin] *= maskGain;
        }
    }

    // ── OUTPUT: Soft-clip any bins that exceed unity ─────────────
    for (int bin = 0; bin < NUM_BINS; ++bin)
    {
        float mag = std::abs(outputFrame[bin]);
        if (mag > 1.0f)
        {
            float clipped = std::tanh(mag);
            outputFrame[bin] *= (clipped / mag);
        }
    }

    // ── Inverse FFT ──────────────────────────────────────────────
    for (int bin = 0; bin < NUM_BINS; ++bin)
    {
        fftWorkspace[bin * 2]     = outputFrame[bin].real();
        fftWorkspace[bin * 2 + 1] = outputFrame[bin].imag();
    }

    for (int bin = 0; bin < NUM_BINS; ++bin)
        currentSpectrum[bin] = std::abs(outputFrame[bin]);

    fft.performRealOnlyInverseTransform(fftWorkspace.data());

    window.multiplyWithWindowingTable(fftWorkspace.data(), FFT_SIZE);
    float gain = 1.0f / (FFT_SIZE * 0.375f);

    for (int i = 0; i < FFT_SIZE; ++i)
    {
        int outIdx = (ch.outputWritePos + i) % (FFT_SIZE * 2);
        ch.outputBuffer[outIdx] += fftWorkspace[i] * gain;
    }

    ch.delayWritePos = (ch.delayWritePos + 1) % MAX_DELAY_FRAMES;
    ++hopCount;
}

void SpectralDelay::getSpectrum(float* dest, int numBins) const
{
    int step = NUM_BINS / numBins;
    for (int i = 0; i < numBins; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < step; ++j)
            sum += currentSpectrum[i * step + j];
        dest[i] = sum / static_cast<float>(step);
    }
}

// ═══════════════════════════════════════════════════════════════════
// YIN pitch detection algorithm
// ═══════════════════════════════════════════════════════════════════
float SpectralDelay::detectPitchYIN(const float* buffer, int length) const
{
    // YIN operates on the second half of the buffer
    int halfLen = length / 2;

    // Step 1: Difference function
    std::vector<float> diff(halfLen, 0.0f);
    for (int tau = 1; tau < halfLen; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < halfLen; ++j)
        {
            float delta = buffer[j] - buffer[j + tau];
            sum += delta * delta;
        }
        diff[tau] = sum;
    }

    // Step 2: Cumulative mean normalized difference
    std::vector<float> cmndf(halfLen, 0.0f);
    cmndf[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < halfLen; ++tau)
    {
        runningSum += diff[tau];
        cmndf[tau] = diff[tau] * tau / std::max(runningSum, 1e-10f);
    }

    // Step 3: Absolute threshold -- find first dip below threshold
    int tauEstimate = -1;
    for (int tau = 2; tau < halfLen; ++tau)
    {
        if (cmndf[tau] < YIN_THRESHOLD)
        {
            // Find the local minimum following this dip
            while (tau + 1 < halfLen && cmndf[tau + 1] < cmndf[tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 1)
        return 0.0f;  // No pitch detected

    // Step 4: Parabolic interpolation for sub-sample accuracy
    float betterTau = static_cast<float>(tauEstimate);
    if (tauEstimate > 0 && tauEstimate < halfLen - 1)
    {
        float s0 = cmndf[tauEstimate - 1];
        float s1 = cmndf[tauEstimate];
        float s2 = cmndf[tauEstimate + 1];
        float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs(denom) > 1e-10f)
            betterTau += (s0 - s2) / denom;
    }

    return static_cast<float>(sampleRate) / betterTau;
}

// ═══════════════════════════════════════════════════════════════════
// Chroma accumulation + Krumhansl-Kessler key estimation
// ═══════════════════════════════════════════════════════════════════
void SpectralDelay::updateChromaAndKey(float pitchHz)
{
    // Convert Hz to MIDI note and pitch class
    float midiNote = 69.0f + 12.0f * std::log2(pitchHz / 440.0f);
    int pitchClass = static_cast<int>(std::round(midiNote)) % 12;
    if (pitchClass < 0) pitchClass += 12;

    // Accumulate into chroma with exponential decay
    for (int i = 0; i < 12; ++i)
        chromaAccum[i] *= chromaDecay;
    chromaAccum[pitchClass] += 1.0f;

    // Correlate with all 24 key profiles (12 major + 12 minor)
    float bestCorr = -999.0f;
    int bestRoot = 0;
    bool bestMinor = false;

    for (int root = 0; root < 12; ++root)
    {
        // Major key correlation
        float corrMaj = 0.0f;
        float corrMin = 0.0f;
        float sumChroma = 0.0f, sumMaj = 0.0f, sumMin = 0.0f;

        for (int i = 0; i < 12; ++i)
        {
            int rotated = (i + root) % 12;
            corrMaj += chromaAccum[rotated] * majorProfile[i];
            corrMin += chromaAccum[rotated] * minorProfile[i];
            sumChroma += chromaAccum[rotated] * chromaAccum[rotated];
            sumMaj += majorProfile[i] * majorProfile[i];
            sumMin += minorProfile[i] * minorProfile[i];
        }

        // Normalize (Pearson correlation)
        float denomMaj = std::sqrt(sumChroma * sumMaj);
        float denomMin = std::sqrt(sumChroma * sumMin);
        if (denomMaj > 1e-10f) corrMaj /= denomMaj;
        if (denomMin > 1e-10f) corrMin /= denomMin;

        if (corrMaj > bestCorr) { bestCorr = corrMaj; bestRoot = root; bestMinor = false; }
        if (corrMin > bestCorr) { bestCorr = corrMin; bestRoot = root; bestMinor = true; }
    }

    // Only update if confidence is reasonable
    if (bestCorr > 0.3f)
    {
        KeyInfo info;
        info.rootNote = bestRoot;
        info.isMinor = bestMinor;
        info.confidence = std::clamp(bestCorr, 0.0f, 1.0f);
        detectedKey.store(info);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Rebuild the per-bin key mask based on detected key
// ═══════════════════════════════════════════════════════════════════
void SpectralDelay::rebuildKeyMask()
{
    auto key = detectedKey.load();
    if (key.rootNote < 0 || key.confidence < 0.3f)
    {
        keyMask.fill(1.0f);  // No filtering if no key detected
        return;
    }

    // Build the scale (7 notes for major/minor)
    // Major intervals: 0,2,4,5,7,9,11
    // Minor intervals: 0,2,3,5,7,8,10
    static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* scale = key.isMinor ? minorScale : majorScale;

    // Build set of in-key pitch classes
    bool inKey[12] = {};
    for (int i = 0; i < 7; ++i)
        inKey[(key.rootNote + scale[i]) % 12] = true;

    // Map each FFT bin to its nearest pitch class and set mask
    float binHz = static_cast<float>(sampleRate) / static_cast<float>(FFT_SIZE);
    for (int bin = 1; bin < NUM_BINS; ++bin)
    {
        float freq = bin * binHz;
        if (freq < 30.0f || freq > 8000.0f)
        {
            keyMask[bin] = 1.0f;  // Don't filter sub-bass or very high frequencies
            continue;
        }

        // Convert bin frequency to pitch class
        float midiNote = 69.0f + 12.0f * std::log2(freq / 440.0f);
        int pc = static_cast<int>(std::round(midiNote)) % 12;
        if (pc < 0) pc += 12;

        keyMask[bin] = inKey[pc] ? 1.0f : 0.15f;  // Out-of-key bins attenuated to 15%
    }
    keyMask[0] = 1.0f;  // DC bin always passes
}
