#include "GhostEngine.h"
#include <algorithm>

// Static constexpr definitions
constexpr int   GhostEngine::baseLens[FDN_ORDER];
constexpr int   GhostEngine::diffLens[NUM_DIFFUSERS];
constexpr int   GhostEngine::fdnAPLens[FDN_ORDER];
constexpr float GhostEngine::modRates[FDN_ORDER];
constexpr float GhostEngine::inWeightL[FDN_ORDER];
constexpr float GhostEngine::inWeightR[FDN_ORDER];
constexpr float GhostEngine::outWeightL[FDN_ORDER];
constexpr float GhostEngine::outWeightR[FDN_ORDER];
constexpr float GhostEngine::enigmaBaseDelays[ENIGMA_STAGES];
constexpr float GhostEngine::enigmaStereoOffset[ENIGMA_STAGES];

GhostEngine::GhostEngine() {}

void GhostEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    float srScale = (float)(sr / 44100.0);

    // FDN delay lines
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        fdn[i].len = std::clamp((int)(baseLens[i] * srScale), 4, MAX_DELAY - 1);
        fdn[i].clear();
        absF[i].reset();
    }

    // Input diffusers
    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        int len = std::clamp((int)(diffLens[i] * srScale), 4, MAX_AP - 1);
        diffL[i].baseLen = len;
        diffR[i].baseLen = len;
        diffL[i].clear();
        diffR[i].clear();
        diffL[i].phase = (float)i * 0.25f;
        diffR[i].phase = (float)i * 0.25f + 0.5f;
    }

    // FDN internal allpasses
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        int len = std::clamp((int)(fdnAPLens[i] * srScale), 4, MAX_AP - 1);
        fdnAP[i].baseLen = len;
        fdnAP[i].clear();
        fdnAP[i].phase = (float)i * 0.125f;
    }

    // Enigma allpass stages
    for (int i = 0; i < ENIGMA_STAGES; ++i)
    {
        enigmaL[i].clear();
        enigmaR[i].clear();
    }
    enigmaFbL = enigmaFbR = 0.0f;
    enigmaFiltL = enigmaFiltR = 0.0f;

    // DC blocker
    float hpFreq = 20.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = hpStateR = 0.0f;

    lfoPhase1 = 0.0;
    lfoPhase2 = 0.0;
    wetBuffer.setSize(2, blockSize);
    wetBuffer.clear();
}

void GhostEngine::reset()
{
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        fdn[i].clear();
        absF[i].reset();
        fdnAP[i].clear();
    }
    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        diffL[i].clear();
        diffR[i].clear();
    }
    for (int i = 0; i < ENIGMA_STAGES; ++i)
    {
        enigmaL[i].clear();
        enigmaR[i].clear();
    }
    enigmaFbL = enigmaFbR = 0.0f;
    enigmaFiltL = enigmaFiltR = 0.0f;
    hpStateL = hpStateR = 0.0f;
    lfoPhase1 = lfoPhase2 = 0.0;
}

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

    // ── FDN delay lengths from SIZE ─────────────────────────
    float srScale = (float)(sampleRate / 44100.0);
    float sizeScale = 0.15f + size * 2.85f;
    for (int i = 0; i < FDN_ORDER; ++i)
        fdn[i].len = std::clamp((int)(baseLens[i] * srScale * sizeScale), 4, MAX_DELAY - 1);

    // ── Reverb parameters ───────────────────────────────────
    float fbGain = decay * decay * 0.985f;
    float absCoeff = 0.02f + fdnTone * 0.90f;
    float apCoeff = 0.15f + diff * 0.70f;
    float baseModDepth = 1.0f + size * 8.0f;

    // ── Enigma parameters ───────────────────────────────────

    // RATE: primary LFO speed (0.03–8 Hz)
    // + secondary LFO at irrational ratio for complex non-repeating sweep
    float lfo1Hz = 0.03f + rate * 7.97f;
    float lfo2Hz = lfo1Hz * 0.7071f;  // sqrt(2)/2 ratio — never syncs
    double lfo1Inc = lfo1Hz / sampleRate;
    double lfo2Inc = lfo2Hz / sampleRate;

    // DEPTH: controls two things simultaneously (like Enigma's interplay):
    //   1. Allpass coefficient (notch depth: 0 = no notching, 0.95 = deep)
    //   2. Feedback amount through the allpass chain (0 = none, 0.6 = resonant)
    float enigmaCoeff = depth * 0.95f;         // allpass coefficient (notch depth)
    float enigmaFeedback = depth * depth * 0.6f; // quadratic for safe feedback

    // NOTCH: sweep center frequency
    // Maps to a multiplier on the base delay times
    // Low notch = low frequencies emphasized, high = high frequencies
    // Range: 0.3x to 4.0x of base delays
    float notchScale = 0.3f + notch * 3.7f;

    // Enigma feedback filter coefficient (shapes the feedback tone)
    // Darker feedback prevents harsh buildup — fixed at a musical value
    float enigmaFiltCoeff = 0.4f + notch * 0.3f;  // darker at low notch, brighter at high

    // Track sweep for UI
    float lastSweepFreq = 200.0f;

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;

        // ── 1. Input diffusion ──────────────────────────────
        float dL = inL, dR = inR;
        for (int d = 0; d < NUM_DIFFUSERS; ++d)
        {
            float modD = baseModDepth * ((float)diffLens[d] / 521.0f);
            float modRate = 0.1f + (float)d * 0.07f;
            dL = diffL[d].process(dL, apCoeff, modD, modRate, sampleRate);
            dR = diffR[d].process(dR, apCoeff, modD, modRate, sampleRate);
        }

        // ── 2. FDN Reverb ───────────────────────────────────
        float rd[FDN_ORDER];
        for (int i = 0; i < FDN_ORDER; ++i)
            rd[i] = fdn[i].read();

        // Hadamard 8x8
        constexpr float H = 0.35355339f;
        float a0 = (rd[0] + rd[1] + rd[2] + rd[3]);
        float a1 = (rd[0] - rd[1] + rd[2] - rd[3]);
        float a2 = (rd[0] + rd[1] - rd[2] - rd[3]);
        float a3 = (rd[0] - rd[1] - rd[2] + rd[3]);
        float b0 = (rd[4] + rd[5] + rd[6] + rd[7]);
        float b1 = (rd[4] - rd[5] + rd[6] - rd[7]);
        float b2 = (rd[4] + rd[5] - rd[6] - rd[7]);
        float b3 = (rd[4] - rd[5] - rd[6] + rd[7]);

        float mx[FDN_ORDER];
        mx[0] = (a0 + b0) * H;  mx[1] = (a1 + b1) * H;
        mx[2] = (a2 + b2) * H;  mx[3] = (a3 + b3) * H;
        mx[4] = (a0 - b0) * H;  mx[5] = (a1 - b1) * H;
        mx[6] = (a2 - b2) * H;  mx[7] = (a3 - b3) * H;

        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float fb = mx[i] * fbGain;
            absF[i].s += absCoeff * (fb - absF[i].s);
            fb = absF[i].s;
            float lineModDepth = baseModDepth * ((float)fdn[i].len / 2000.0f);
            lineModDepth = std::clamp(lineModDepth, 0.5f, 20.0f);
            fb = fdnAP[i].process(fb, apCoeff * 0.4f, lineModDepth,
                                   modRates[i], sampleRate);
            fb = std::tanh(fb);
            float inputSig = dL * inWeightL[i] + dR * inWeightR[i];
            fdn[i].write(fb + inputSig);
        }

        float reverbL = 0.0f, reverbR = 0.0f;
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            reverbL += rd[i] * outWeightL[i];
            reverbR += rd[i] * outWeightR[i];
        }
        reverbL *= 0.40f;
        reverbR *= 0.40f;

        // ── 3. Enigma Notch Processor ───────────────────────
        // Dual LFO for complex sweep (no repeating pattern)
        float lfo1 = (float)std::sin(lfoPhase1 * juce::MathConstants<double>::twoPi);
        float lfo2 = (float)std::sin(lfoPhase2 * juce::MathConstants<double>::twoPi);
        lfoPhase1 += lfo1Inc;
        lfoPhase2 += lfo2Inc;
        if (lfoPhase1 >= 1.0) lfoPhase1 -= 1.0;
        if (lfoPhase2 >= 1.0) lfoPhase2 -= 1.0;

        // Combined LFO: primary + secondary at lower amplitude
        // Range: -1.0 to +1.0 but with complex shape
        float lfoVal = lfo1 * 0.7f + lfo2 * 0.3f;

        if (depth > 0.001f)
        {
            // Add feedback from previous iteration into input
            // (Enigma feeds the feedback section output back into the notch input)
            float procL = reverbL + enigmaFbL;
            float procR = reverbR + enigmaFbR;

            // Process through 6 allpass stages
            // Each stage's delay time is modulated by the LFO
            // The delay times are staggered (Fibonacci-based) for non-harmonic notch spacing
            for (int i = 0; i < ENIGMA_STAGES; ++i)
            {
                // Per-stage LFO with phase offset for rich sweep
                float stagePhase = (float)i * 0.1667f;  // 1/6 spacing
                float stageLfo = std::sin((float)(lfoPhase1 + stagePhase) *
                                          juce::MathConstants<float>::twoPi) * 0.7f
                               + std::sin((float)(lfoPhase2 + stagePhase * 0.618f) *
                                          juce::MathConstants<float>::twoPi) * 0.3f;

                // Calculate modulated delay time for this stage
                float baseDelay = enigmaBaseDelays[i] * notchScale * srScale;
                // LFO modulates the delay: ±50% of base
                float modAmount = baseDelay * 0.5f;
                float delayL = baseDelay + stageLfo * modAmount;
                float delayR = baseDelay + enigmaStereoOffset[i] * srScale
                             + stageLfo * modAmount;

                procL = enigmaL[i].process(procL, enigmaCoeff, delayL);
                procR = enigmaR[i].process(procR, enigmaCoeff, delayR);
            }

            // Store feedback (filtered to prevent harsh buildup)
            // One-pole LP on the feedback path, like Enigma's feedback filter
            enigmaFiltL += enigmaFiltCoeff * (procL - enigmaFiltL);
            enigmaFiltR += enigmaFiltCoeff * (procR - enigmaFiltR);
            enigmaFbL = std::tanh(enigmaFiltL * enigmaFeedback);
            enigmaFbR = std::tanh(enigmaFiltR * enigmaFeedback);

            // Track sweep center frequency for UI
            float centerDelay = enigmaBaseDelays[2] * notchScale * srScale
                              + lfoVal * enigmaBaseDelays[2] * notchScale * srScale * 0.5f;
            if (centerDelay > 0.5f)
                lastSweepFreq = (float)sampleRate / centerDelay;

            // Blend: depth controls how much enigma vs clean reverb
            // At low depth: subtle phasing. At high depth: deep Enigma character
            reverbL = reverbL * (1.0f - depth) + procL * depth;
            reverbR = reverbR * (1.0f - depth) + procR * depth;
        }

        wetBuffer.setSample(0, s, reverbL);
        wetBuffer.setSample(1, s, reverbR);
    }

    // ── 4. Mix dry/wet + DC blocking ────────────────────────
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
        float& hp = (ch == 0) ? hpStateL : hpStateR;

        for (int s = 0; s < numSamples; ++s)
        {
            float m = dry[s] * (1.0f - mix) + wet[s] * mix;
            float hpOut = m - hp;
            hp = m - hpCoeff * hpOut;
            out[s] = hpOut;
            rms += out[s] * out[s];
        }
    }

    rms = std::sqrt(rms / (float)(numSamples * std::max(numChannels, 1)));
    rmsLevel.store(rms);
    sweepFreq.store(lastSweepFreq);
    sweepPos.store(std::clamp((lastSweepFreq - 30.0f) / 470.0f, 0.0f, 1.0f));
}
