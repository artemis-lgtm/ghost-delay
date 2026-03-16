#include "GhostEngine.h"
#include <algorithm>

constexpr int   GhostEngine::baseLens[FDN_ORDER];
constexpr int   GhostEngine::diffLens[NUM_DIFFUSERS];
constexpr int   GhostEngine::fdnAPLens[FDN_ORDER];
constexpr float GhostEngine::modRates[FDN_ORDER];
constexpr float GhostEngine::inWeight[FDN_ORDER];
constexpr float GhostEngine::outWeightL[FDN_ORDER];
constexpr float GhostEngine::outWeightR[FDN_ORDER];

GhostEngine::GhostEngine() {}

void GhostEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    float srScale = (float)(sr / 44100.0);

    // Parameter smoothing: ~30ms time constant
    smoothCoeff = std::exp(-1.0f / (0.03f * (float)sr));
    smoothSize = targetSize;
    smoothDecay = targetDecay;
    smoothTone = targetTone;
    smoothReverbMix = targetReverbMix;

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
        diffL[i].baseLen = len;  diffR[i].baseLen = len;
        diffL[i].clear();        diffR[i].clear();
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

    // Tone filter
    toneFiltL.reset();
    toneFiltR.reset();

    // DC blocker
    float hpFreq = 20.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = hpStateR = 0.0f;

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
    toneFiltL.reset(); toneFiltR.reset();
    hpStateL = hpStateR = 0.0f;
}

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
            { ppqPosition = *ppq; hasPPQ = true; }
        }
    }

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.clear();

    float srScale = (float)(sampleRate / 44100.0);

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        // ── Smooth parameters ───────────────────────────────
        smoothSize      = smooth(smoothSize,      targetSize);
        smoothDecay     = smooth(smoothDecay,     targetDecay);
        smoothTone      = smooth(smoothTone,      targetTone);
        smoothReverbMix = smooth(smoothReverbMix, targetReverbMix);

        // ── Derived parameters ──────────────────────────────

        // SIZE: scales FDN delay lengths (0.15x to 3.0x)
        float sizeScale = 0.15f + smoothSize * 2.85f;

        // DECAY: feedback gain (quadratic for musical feel)
        float fbGain = smoothDecay * smoothDecay * 0.985f;

        // TONE: absorption in feedback loop + direct LP on output
        float absCoeff = 0.02f + smoothTone * 0.90f;
        float toneLP   = 0.05f + smoothTone * 0.93f;

        // Modulation depth proportional to room size (Valhalla principle)
        float baseModDepth = 0.5f + smoothSize * 6.0f;

        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;

        // Sum to mono for reverb input (prevents L/R separation artifacts)
        float inMono = (inL + inR) * 0.5f;

        // ── 1. Input diffusion (fixed coefficient) ──────────
        // Feed mono into both diffusion chains for stereo decorrelation
        float dL = inMono, dR = inMono;
        for (int d = 0; d < NUM_DIFFUSERS; ++d)
        {
            float modD = baseModDepth * ((float)diffLens[d] / 521.0f);
            float modRate = 0.1f + (float)d * 0.07f;
            dL = diffL[d].process(dL, DIFFUSION_COEFF, modD, modRate, sampleRate);
            dR = diffR[d].process(dR, DIFFUSION_COEFF, modD, modRate, sampleRate);
        }

        // ── 2. Update FDN delay lengths from SIZE ───────────
        for (int i = 0; i < FDN_ORDER; ++i)
            fdn[i].len = std::clamp((int)(baseLens[i] * srScale * sizeScale),
                                    4, MAX_DELAY - 1);

        // ── 3. FDN: read from delay lines ───────────────────
        float rd[FDN_ORDER];
        for (int i = 0; i < FDN_ORDER; ++i)
            rd[i] = fdn[i].read();

        // ── 4. Hadamard 8x8 mixing matrix ──────────────────
        // H8 = H2 ⊗ H4, normalized by 1/sqrt(8)
        constexpr float H = 0.35355339f;  // 1/sqrt(8)
        float a0 = rd[0]+rd[1]+rd[2]+rd[3], a1 = rd[0]-rd[1]+rd[2]-rd[3];
        float a2 = rd[0]+rd[1]-rd[2]-rd[3], a3 = rd[0]-rd[1]-rd[2]+rd[3];
        float b0 = rd[4]+rd[5]+rd[6]+rd[7], b1 = rd[4]-rd[5]+rd[6]-rd[7];
        float b2 = rd[4]+rd[5]-rd[6]-rd[7], b3 = rd[4]-rd[5]-rd[6]+rd[7];

        float mx[FDN_ORDER];
        mx[0]=(a0+b0)*H; mx[1]=(a1+b1)*H; mx[2]=(a2+b2)*H; mx[3]=(a3+b3)*H;
        mx[4]=(a0-b0)*H; mx[5]=(a1-b1)*H; mx[6]=(a2-b2)*H; mx[7]=(a3-b3)*H;

        // ── 5. Feedback processing + write back ─────────────
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float fb = mx[i] * fbGain;

            // Absorption filter (one-pole LP in feedback loop — Jot methodology)
            absF[i].s += absCoeff * (fb - absF[i].s);
            fb = absF[i].s;

            // Modulated allpass in feedback (breaks up metallic modes)
            float lineModDepth = std::clamp(
                baseModDepth * ((float)fdn[i].len / 2000.0f), 0.3f, 15.0f);
            fb = fdnAP[i].process(fb, DIFFUSION_COEFF * 0.4f,
                                   lineModDepth, modRates[i], sampleRate);

            // Soft-clip in feedback to prevent runaway
            fb = std::tanh(fb);

            // Inject diffused input into each delay line (mono-summed, balanced)
            float inputSig = (dL + dR) * 0.5f * inWeight[i];
            fdn[i].write(fb + inputSig);
        }

        // ── 6. Output tap (stereo decorrelation) ────────────
        float reverbL = 0.0f, reverbR = 0.0f;
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            reverbL += rd[i] * outWeightL[i];
            reverbR += rd[i] * outWeightR[i];
        }
        reverbL *= 0.35f;
        reverbR *= 0.35f;

        // ── 7. TONE: direct LP on wet output ────────────────
        reverbL = toneFiltL.process(reverbL, toneLP);
        reverbR = toneFiltR.process(reverbR, toneLP);

        // ── 8. Output soft-clip ─────────────────────────────
        reverbL = std::tanh(reverbL);
        reverbR = std::tanh(reverbR);

        wetBuffer.setSample(0, s, reverbL);
        wetBuffer.setSample(1, s, reverbR);
    }

    // ── 9. Mix dry/wet using reverb mix + DC block ──────────
    // Save dry signal first (prevents channel overwrite issues on mono sources)
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    const int outChannels = buffer.getNumChannels();
    const int mixChannels = std::min(outChannels, 2);
    float rms = 0.0f;

    for (int ch = 0; ch < mixChannels; ++ch)
    {
        const int dryCh = (ch < numChannels) ? ch : 0;
        const float* dry = dryBuffer.getReadPointer(dryCh);
        const int wetCh = std::min(ch, wetBuffer.getNumChannels() - 1);
        const float* wet = wetBuffer.getReadPointer(wetCh);
        float* out = buffer.getWritePointer(ch);
        float& hp = (ch == 0) ? hpStateL : hpStateR;

        for (int s = 0; s < numSamples; ++s)
        {
            float m = dry[s] * (1.0f - smoothReverbMix) + wet[s] * smoothReverbMix;

            // DC blocker (20 Hz HP)
            float hpOut = m - hp;
            hp = m - hpCoeff * hpOut;
            out[s] = hpOut;
            rms += out[s] * out[s];
        }
    }

    rms = std::sqrt(rms / (float)(numSamples * std::max(numChannels, 1)));
    rmsLevel.store(rms);
}
