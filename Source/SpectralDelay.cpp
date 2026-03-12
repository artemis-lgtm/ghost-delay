#include "SpectralDelay.h"
#include <cmath>
#include <algorithm>
#include <random>

SpectralDelay::SpectralDelay()
{
    // Generate fixed random phase offsets for stereo spread chaos
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 6.283185f);
    for (auto& p : binPhaseOffsets)
        p = dist(rng);
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
