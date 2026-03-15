#include "GhostEngine.h"
#include <algorithm>

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

    // Parameter smoothing: ~30ms time constant
    // coeff = exp(-1 / (tau * sr)), tau = 0.03s
    smoothCoeff = std::exp(-1.0f / (0.03f * (float)sr));

    // Initialize smooth params to targets
    smoothSize = targetSize;   smoothDecay = targetDecay;
    smoothTone = targetTone;   smoothDiff  = targetDiff;
    smoothRate = targetRate;   smoothDepth = targetDepth;
    smoothNotch = targetNotch; smoothMix   = targetMix;

    for (int i = 0; i < FDN_ORDER; ++i)
    {
        fdn[i].len = std::clamp((int)(baseLens[i] * srScale), 4, MAX_DELAY - 1);
        fdn[i].clear();
        absF[i].reset();
    }

    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        int len = std::clamp((int)(diffLens[i] * srScale), 4, MAX_AP - 1);
        diffL[i].baseLen = len;  diffR[i].baseLen = len;
        diffL[i].clear();        diffR[i].clear();
        diffL[i].phase = (float)i * 0.25f;
        diffR[i].phase = (float)i * 0.25f + 0.5f;
    }

    for (int i = 0; i < FDN_ORDER; ++i)
    {
        int len = std::clamp((int)(fdnAPLens[i] * srScale), 4, MAX_AP - 1);
        fdnAP[i].baseLen = len;
        fdnAP[i].clear();
        fdnAP[i].phase = (float)i * 0.125f;
    }

    for (int i = 0; i < ENIGMA_STAGES; ++i)
    {
        enigmaL[i].clear();
        enigmaR[i].clear();
    }
    enigmaFbL = enigmaFbR = 0.0f;
    enigmaFiltL = enigmaFiltR = 0.0f;
    toneFiltL.reset();
    toneFiltR.reset();

    float hpFreq = 20.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = hpStateR = 0.0f;

    lfoPhase1 = lfoPhase2 = 0.0;
    wetBuffer.setSize(2, blockSize);
    wetBuffer.clear();
}

void GhostEngine::reset()
{
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        fdn[i].clear(); absF[i].reset(); fdnAP[i].clear();
    }
    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        diffL[i].clear(); diffR[i].clear();
    }
    for (int i = 0; i < ENIGMA_STAGES; ++i)
    {
        enigmaL[i].clear(); enigmaR[i].clear();
    }
    enigmaFbL = enigmaFbR = 0.0f;
    enigmaFiltL = enigmaFiltR = 0.0f;
    toneFiltL.reset(); toneFiltR.reset();
    hpStateL = hpStateR = 0.0f;
    lfoPhase1 = lfoPhase2 = 0.0;
}

void GhostEngine::process(juce::AudioBuffer<float>& buffer,
                           juce::AudioPlayHead* playHead)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), 2);

    hasPPQ = false;
    if (playHead != nullptr)
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBPM = std::max(*bpm, 20.0);
            hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition())
            { ppqPosition = *ppq; hasPPQ = true; }
        }
    }

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.clear();

    float srScale = (float)(sampleRate / 44100.0);
    float lastSweepFreq = 200.0f;

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING (with parameter smoothing)
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        // ── Smooth all parameters (prevents glitches) ───────
        smoothSize  = smooth(smoothSize,  targetSize);
        smoothDecay = smooth(smoothDecay, targetDecay);
        smoothTone  = smooth(smoothTone,  targetTone);
        smoothDiff  = smooth(smoothDiff,  targetDiff);
        smoothRate  = smooth(smoothRate,  targetRate);
        smoothDepth = smooth(smoothDepth, targetDepth);
        smoothNotch = smooth(smoothNotch, targetNotch);
        smoothMix   = smooth(smoothMix,   targetMix);

        // ── Derived parameters (from smoothed values) ───────

        // SIZE → FDN delay scale
        float sizeScale = 0.15f + smoothSize * 2.85f;

        // DECAY → feedback gain (quadratic)
        float fbGain = smoothDecay * smoothDecay * 0.985f;

        // TONE → absorption in feedback + direct LP on wet output
        // Absorption: 0.02 (dark) to 0.92 (bright)
        float absCoeff = 0.02f + smoothTone * 0.90f;
        // Direct tone filter: maps 0-1 to LP cutoff coefficient
        // 0 = very dark (coeff 0.05), 1 = wide open (coeff 0.98)
        float toneLP = 0.05f + smoothTone * 0.93f;

        // DIFF → allpass coefficient (0 = bypass/no diffusion, 1 = max smear)
        float apCoeff = smoothDiff * 0.85f;

        // Modulation depth scaled by size
        float baseModDepth = 1.0f + smoothSize * 8.0f;

        // ENIGMA parameters
        float lfo1Hz = 0.03f + smoothRate * 7.97f;
        float lfo2Hz = lfo1Hz * 0.7071f;
        double lfo1Inc = lfo1Hz / sampleRate;
        double lfo2Inc = lfo2Hz / sampleRate;

        // DEPTH: allpass coefficient (capped at 0.75 for stability)
        float enigmaCoeff = smoothDepth * 0.75f;
        // Feedback: quadratic, capped at 0.45 (safe)
        float enigmaFeedback = smoothDepth * smoothDepth * 0.45f;

        // NOTCH: sweep position (0.5x to 5.0x delay multiplier)
        float notchScale = 0.5f + smoothNotch * 4.5f;

        // Feedback filter coefficient
        float enigmaFiltCoeff = 0.3f + smoothNotch * 0.4f;

        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;

        // ── 1. Input diffusion ──────────────────────────────
        float dL = inL, dR = inR;
        if (apCoeff > 0.01f)  // Skip if DIFF ≈ 0 (saves CPU + makes bypass obvious)
        {
            for (int d = 0; d < NUM_DIFFUSERS; ++d)
            {
                float modD = baseModDepth * ((float)diffLens[d] / 521.0f);
                float modRate = 0.1f + (float)d * 0.07f;
                dL = diffL[d].process(dL, apCoeff, modD, modRate, sampleRate);
                dR = diffR[d].process(dR, apCoeff, modD, modRate, sampleRate);
            }
        }

        // ── 2. Update FDN delay lengths ─────────────────────
        for (int i = 0; i < FDN_ORDER; ++i)
            fdn[i].len = std::clamp((int)(baseLens[i] * srScale * sizeScale), 4, MAX_DELAY - 1);

        // ── 3. FDN Reverb ───────────────────────────────────
        float rd[FDN_ORDER];
        for (int i = 0; i < FDN_ORDER; ++i)
            rd[i] = fdn[i].read();

        // Hadamard 8x8
        constexpr float H = 0.35355339f;
        float a0 = rd[0]+rd[1]+rd[2]+rd[3], a1 = rd[0]-rd[1]+rd[2]-rd[3];
        float a2 = rd[0]+rd[1]-rd[2]-rd[3], a3 = rd[0]-rd[1]-rd[2]+rd[3];
        float b0 = rd[4]+rd[5]+rd[6]+rd[7], b1 = rd[4]-rd[5]+rd[6]-rd[7];
        float b2 = rd[4]+rd[5]-rd[6]-rd[7], b3 = rd[4]-rd[5]-rd[6]+rd[7];

        float mx[FDN_ORDER];
        mx[0]=(a0+b0)*H; mx[1]=(a1+b1)*H; mx[2]=(a2+b2)*H; mx[3]=(a3+b3)*H;
        mx[4]=(a0-b0)*H; mx[5]=(a1-b1)*H; mx[6]=(a2-b2)*H; mx[7]=(a3-b3)*H;

        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float fb = mx[i] * fbGain;
            absF[i].s += absCoeff * (fb - absF[i].s);
            fb = absF[i].s;
            float lineModDepth = std::clamp(baseModDepth * ((float)fdn[i].len / 2000.0f), 0.5f, 20.0f);
            fb = fdnAP[i].process(fb, apCoeff * 0.4f, lineModDepth, modRates[i], sampleRate);
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

        // ── 4. TONE: Direct LP filter on wet signal ─────────
        // This makes TONE immediately audible (not just decay character)
        reverbL = toneFiltL.process(reverbL, toneLP);
        reverbR = toneFiltR.process(reverbR, toneLP);

        // ── 5. Enigma Notch Processor ───────────────────────
        float lfo1 = (float)std::sin(lfoPhase1 * juce::MathConstants<double>::twoPi);
        float lfo2 = (float)std::sin(lfoPhase2 * juce::MathConstants<double>::twoPi);
        lfoPhase1 += lfo1Inc;
        lfoPhase2 += lfo2Inc;
        if (lfoPhase1 >= 1.0) lfoPhase1 -= 1.0;
        if (lfoPhase2 >= 1.0) lfoPhase2 -= 1.0;

        float lfoVal = lfo1 * 0.7f + lfo2 * 0.3f;

        if (smoothDepth > 0.005f)
        {
            float procL = reverbL + enigmaFbL;
            float procR = reverbR + enigmaFbR;

            for (int i = 0; i < ENIGMA_STAGES; ++i)
            {
                float stagePhase = (float)i * 0.1667f;
                float stageLfo = std::sin((float)(lfoPhase1 + stagePhase) *
                                          juce::MathConstants<float>::twoPi) * 0.7f
                               + std::sin((float)(lfoPhase2 + stagePhase * 0.618f) *
                                          juce::MathConstants<float>::twoPi) * 0.3f;

                float baseDelay = enigmaBaseDelays[i] * notchScale * srScale;
                float modAmount = baseDelay * 0.4f;
                float delayL = std::clamp(baseDelay + stageLfo * modAmount, 1.0f, (float)(MAX_ENIGMA_AP - 2));
                float delayR = std::clamp(baseDelay + enigmaStereoOffset[i] * srScale
                             + stageLfo * modAmount, 1.0f, (float)(MAX_ENIGMA_AP - 2));

                procL = enigmaL[i].process(procL, enigmaCoeff, delayL);
                procR = enigmaR[i].process(procR, enigmaCoeff, delayR);
            }

            // Filtered feedback (safe)
            enigmaFiltL += enigmaFiltCoeff * (procL - enigmaFiltL);
            enigmaFiltR += enigmaFiltCoeff * (procR - enigmaFiltR);
            enigmaFbL = std::tanh(enigmaFiltL * enigmaFeedback);
            enigmaFbR = std::tanh(enigmaFiltR * enigmaFeedback);

            // Track sweep
            float centerDelay = enigmaBaseDelays[2] * notchScale * srScale
                              + lfoVal * enigmaBaseDelays[2] * notchScale * srScale * 0.4f;
            if (centerDelay > 0.5f)
                lastSweepFreq = (float)sampleRate / centerDelay;

            // Blend (smooth depth prevents pop)
            reverbL = reverbL * (1.0f - smoothDepth) + procL * smoothDepth;
            reverbR = reverbR * (1.0f - smoothDepth) + procR * smoothDepth;
        }

        // ── 6. Output soft-clip (prevents ANY loud signal) ──
        reverbL = std::tanh(reverbL);
        reverbR = std::tanh(reverbR);

        wetBuffer.setSample(0, s, reverbL);
        wetBuffer.setSample(1, s, reverbR);
    }

    // ── 7. Mix dry/wet + DC blocking ────────────────────────
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
            // Use smoothed mix for glitch-free blend
            float m = dry[s] * (1.0f - smoothMix) + wet[s] * smoothMix;
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
