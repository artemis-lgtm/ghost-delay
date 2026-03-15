#include "GhostEngine.h"
#include <algorithm>

GhostEngine::GhostEngine() {}

void GhostEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;

    // Scale all delay lengths to current sample rate
    float srScale = (float)(sr / 44100.0);

    // FDN delay lines
    for (int i = 0; i < FDN_SIZE; ++i)
    {
        int len = std::clamp((int)(baseLengths[i] * srScale), 4, MAX_FDN_DELAY - 1);
        fdnL[i].length = len;
        fdnR[i].length = len;
        fdnL[i].clear();
        fdnR[i].clear();
        fdnLPL[i].reset();
        fdnLPR[i].reset();
    }

    // Input diffusers
    for (int i = 0; i < 2; ++i)
    {
        int len = std::clamp((int)(diffLengths[i] * srScale), 4, 4095);
        diffuserL[i].length = len;
        diffuserR[i].length = len;
        diffuserL[i].clear();
        diffuserR[i].clear();
    }

    // Modulated allpasses inside FDN
    for (int i = 0; i < FDN_SIZE; ++i)
    {
        int len = std::clamp((int)(modAPLengths[i] * srScale), 4, 4095);
        modAPL[i].length = len;
        modAPR[i].length = len;
        modAPL[i].clear();
        modAPR[i].clear();
        // Stagger phases for decorrelation
        modAPL[i].phase = (float)i * 0.25f;
        modAPR[i].phase = (float)i * 0.25f + 0.125f;
    }

    // Comb filters
    combL.clear();
    combR.clear();

    // DC blocker
    float hpFreq = 20.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = hpStateR = 0.0f;

    lfoPhase = 0.0;

    wetBuffer.setSize(2, blockSize);
    wetBuffer.clear();
}

void GhostEngine::reset()
{
    for (int i = 0; i < FDN_SIZE; ++i)
    {
        fdnL[i].clear();  fdnR[i].clear();
        fdnLPL[i].reset(); fdnLPR[i].reset();
        modAPL[i].clear(); modAPR[i].clear();
    }
    for (int i = 0; i < 2; ++i)
    {
        diffuserL[i].clear();
        diffuserR[i].clear();
    }
    combL.clear();
    combR.clear();
    hpStateL = hpStateR = 0.0f;
    lfoPhase = 0.0;
}

// ═════════════════════════════════════════════════════════════════
// Update FDN delay lengths based on SIZE parameter
// ═════════════════════════════════════════════════════════════════
void GhostEngine::updateFDNDelayLengths()
{
    float srScale = (float)(sampleRate / 44100.0);
    // SIZE scales delay lengths: 0.2x (small room) to 3.0x (massive hall)
    float sizeScale = 0.2f + size * 2.8f;

    for (int i = 0; i < FDN_SIZE; ++i)
    {
        int newLen = std::clamp((int)(baseLengths[i] * srScale * sizeScale), 4, MAX_FDN_DELAY - 1);
        // Don't resize mid-stream (causes glitches), just adjust modulation
        // Actually update — the write positions wrap naturally
        fdnL[i].length = newLen;
        fdnR[i].length = newLen;
    }
}

// ═════════════════════════════════════════════════════════════════
// Main processing
// ═════════════════════════════════════════════════════════════════
void GhostEngine::process(juce::AudioBuffer<float>& buffer,
                           juce::AudioPlayHead* playHead)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), 2);

    // Read host transport
    hasPPQ = false;
    if (playHead != nullptr)
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBPM = std::max(*bpm, 20.0);
            hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition())
            {
                ppqPosition = *ppq;
                hasPPQ = true;
            }
        }
    }

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.clear();

    // Update FDN sizes
    updateFDNDelayLengths();

    // ── Derived parameters ──────────────────────────────────

    // DECAY → feedback gain (0.0 → 0.98)
    float fbGain = decay * 0.98f;

    // TONE → LP coefficient for feedback loop
    // 0 = very dark (coeff ~0.05), 1 = bright (coeff ~0.95)
    float lpCoeff = 0.05f + fdnTone * 0.90f;

    // DIFF → allpass coefficient (0.1 low diffusion → 0.85 high diffusion)
    float apCoeff = 0.1f + diff * 0.75f;

    // Internal modulation for lushness (subtle pitch wobble in FDN)
    // Modulation depth scales with size for bigger = more lush
    float internalModDepth = 2.0f + size * 12.0f;    // 2–14 samples of wobble
    float internalModRate  = 0.3f + size * 0.7f;      // 0.3–1.0 Hz (slow, lush)

    // ── ENIGMA modulation params ────────────────────────────
    // RATE: LFO speed for comb sweep (0.05 Hz → 6 Hz)
    float combLfoHz = 0.05f + rate * 5.95f;
    double lfoInc = (double)combLfoHz / sampleRate;

    // NOTCH: base comb delay (2ms → 30ms = 20 Hz → 333 Hz fundamental)
    float combDelayMs = 2.0f + notch * 28.0f;
    float combDelaySamples = combDelayMs * 0.001f * (float)sampleRate;
    combDelaySamples = std::clamp(combDelaySamples, 2.0f, (float)(MAX_COMB_DELAY - 2));

    // DEPTH: comb feedback + blend intensity
    float combFeedback = depth * 0.85f;  // 0 → 0.85

    // Sweep range: modulate comb delay by +/- 40%
    float combSweepRange = combDelaySamples * 0.4f;

    // Track for UI
    float lastFreq = 200.0f;

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        // Read input (mono → duplicate to both channels)
        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;

        // ── Step 1: Input diffusion (smears transients) ─────
        float diffL = inL;
        float diffR = inR;
        for (int d = 0; d < 2; ++d)
        {
            diffL = diffuserL[d].process(diffL, apCoeff);
            diffR = diffuserR[d].process(diffR, apCoeff);
        }

        // ── Step 2: FDN Reverb ──────────────────────────────
        // Read from all 4 delay lines
        float readL[FDN_SIZE], readR[FDN_SIZE];
        for (int i = 0; i < FDN_SIZE; ++i)
        {
            readL[i] = fdnL[i].read();
            readR[i] = fdnR[i].read();
        }

        // Hadamard-like mixing matrix (energy-preserving)
        // Using a simple rotation: each output = sum of all inputs with signs
        float mixedL[FDN_SIZE], mixedR[FDN_SIZE];
        mixedL[0] = ( readL[0] + readL[1] + readL[2] + readL[3]) * 0.5f;
        mixedL[1] = ( readL[0] - readL[1] + readL[2] - readL[3]) * 0.5f;
        mixedL[2] = ( readL[0] + readL[1] - readL[2] - readL[3]) * 0.5f;
        mixedL[3] = ( readL[0] - readL[1] - readL[2] + readL[3]) * 0.5f;

        mixedR[0] = ( readR[0] + readR[1] + readR[2] + readR[3]) * 0.5f;
        mixedR[1] = ( readR[0] - readR[1] + readR[2] - readR[3]) * 0.5f;
        mixedR[2] = ( readR[0] + readR[1] - readR[2] - readR[3]) * 0.5f;
        mixedR[3] = ( readR[0] - readR[1] - readR[2] + readR[3]) * 0.5f;

        // Apply feedback gain, tone filtering, modulated allpass, then write back
        for (int i = 0; i < FDN_SIZE; ++i)
        {
            // Feedback gain
            float fbL = mixedL[i] * fbGain;
            float fbR = mixedR[i] * fbGain;

            // Tone: one-pole LP in feedback loop (each repeat gets darker)
            fbL = fdnLPL[i].process(fbL, lpCoeff);
            fbR = fdnLPR[i].process(fbR, lpCoeff);

            // Modulated allpass for lush detuning (Valhalla's secret sauce)
            fbL = modAPL[i].process(fbL, apCoeff * 0.5f, internalModDepth, internalModRate, sampleRate);
            fbR = modAPR[i].process(fbR, apCoeff * 0.5f, internalModDepth, internalModRate, sampleRate);

            // Soft clip to prevent runaway
            fbL = std::tanh(fbL);
            fbR = std::tanh(fbR);

            // Add diffused input and write to delay line
            float inputScale = (i == 0) ? 1.0f : 0.0f; // Only inject into first tap
            fdnL[i].write(fbL + diffL * inputScale);
            fdnR[i].write(fbR + diffR * inputScale);
        }

        // Sum FDN outputs for reverb signal
        float reverbL = 0.0f, reverbR = 0.0f;
        for (int i = 0; i < FDN_SIZE; ++i)
        {
            reverbL += readL[i];
            reverbR += readR[i];
        }
        reverbL *= 0.35f;  // Scale to avoid clipping
        reverbR *= 0.35f;

        // ── Step 3: Enigma-style modulated comb filter ──────
        // LFO modulates the comb delay time
        float lfoVal = (float)std::sin(lfoPhase * juce::MathConstants<double>::twoPi);
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;

        float modulatedDelay = combDelaySamples + lfoVal * combSweepRange;
        modulatedDelay = std::clamp(modulatedDelay, 2.0f, (float)(MAX_COMB_DELAY - 2));

        // Track frequency for UI
        lastFreq = (float)sampleRate / modulatedDelay;

        if (depth > 0.001f)
        {
            // Apply comb to reverb signal
            float combOutL = combL.process(reverbL, modulatedDelay, combFeedback);
            float combOutR = combR.process(reverbR, modulatedDelay + 3.0f, combFeedback); // Slight R offset for stereo

            // Blend: depth controls how much comb vs clean reverb
            reverbL = reverbL * (1.0f - depth) + combOutL * depth;
            reverbR = reverbR * (1.0f - depth) + combOutR * depth;
        }

        // ── Step 4: Write to wet buffer ─────────────────────
        wetBuffer.setSample(0, s, reverbL);
        wetBuffer.setSample(1, s, reverbR);
    }

    // ── Step 5: Mix dry/wet + DC blocking ───────────────────
    const int outChannels = buffer.getNumChannels();
    const int mixChannels = std::min(outChannels, 2);

    float rms = 0.0f;
    for (int ch = 0; ch < mixChannels; ++ch)
    {
        const int dryCh = (ch < numChannels) ? ch : 0;
        const float* dry = buffer.getReadPointer(dryCh);
        const int wetCh = std::min(ch, wetBuffer.getNumChannels() - 1);
        const float* wet = wetBuffer.getReadPointer(wetCh);
        float* out = buffer.getWritePointer(ch);
        float& hpState = (ch == 0) ? hpStateL : hpStateR;

        for (int s = 0; s < numSamples; ++s)
        {
            float mixed = dry[s] * (1.0f - mix) + wet[s] * mix;

            // DC blocking
            float hpOut = mixed - hpState;
            hpState = mixed - hpCoeff * hpOut;
            out[s] = hpOut;

            rms += out[s] * out[s];
        }
    }

    // Update UI
    rms = std::sqrt(rms / (float)(numSamples * std::max(numChannels, 1)));
    rmsLevel.store(rms);
    sweepFreq.store(lastFreq);
    float normPos = std::clamp((lastFreq - 30.0f) / 470.0f, 0.0f, 1.0f);
    sweepPos.store(normPos);
}
