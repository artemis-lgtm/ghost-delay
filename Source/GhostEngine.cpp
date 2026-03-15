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

    // Input diffusers (4 per channel)
    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        int len = std::clamp((int)(diffLens[i] * srScale), 4, MAX_AP - 1);
        diffL[i].baseLen = len;
        diffR[i].baseLen = len;
        diffL[i].clear();
        diffR[i].clear();
        // Stagger phases for L/R decorrelation
        diffL[i].phase = (float)i * 0.25f;
        diffR[i].phase = (float)i * 0.25f + 0.5f;
    }

    // FDN internal allpasses (one per delay line)
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        int len = std::clamp((int)(fdnAPLens[i] * srScale), 4, MAX_AP - 1);
        fdnAP[i].baseLen = len;
        fdnAP[i].clear();
        fdnAP[i].phase = (float)i * 0.125f;  // 8 evenly staggered phases
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
    combL.clear();
    combR.clear();
    hpStateL = hpStateR = 0.0f;
    lfoPhase = 0.0;
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

    // ── Update FDN delay lengths from SIZE ──────────────────
    float srScale = (float)(sampleRate / 44100.0);
    float sizeScale = 0.15f + size * 2.85f;  // 0.15x → 3.0x
    for (int i = 0; i < FDN_ORDER; ++i)
        fdn[i].len = std::clamp((int)(baseLens[i] * srScale * sizeScale), 4, MAX_DELAY - 1);

    // ── Derived parameters ──────────────────────────────────

    // DECAY → feedback gain
    // Quadratic curve: gentle at low values, steep near max
    // Max 0.985 to prevent infinite ringing
    float fbGain = decay * decay * 0.985f;

    // TONE → absorption filter coefficient (LP in feedback loop)
    // 0 = very dark (coefficient 0.02), 1 = bright (0.92)
    // This is frequency-dependent decay: high frequencies die faster when dark
    float absCoeff = 0.02f + fdnTone * 0.90f;

    // DIFF → allpass coefficient
    float apCoeff = 0.15f + diff * 0.70f;  // 0.15 → 0.85

    // Internal modulation depth — scaled by delay length (Valhalla principle)
    // Longer delays = more modulation depth for lush detuning
    // Shorter delays = less modulation to avoid metallic chorusing
    float baseModDepth = 1.0f + size * 8.0f;  // 1–9 samples

    // ── Enigma modulation params ────────────────────────────
    float combLfoHz = 0.05f + rate * 5.95f;
    double lfoInc = (double)combLfoHz / sampleRate;
    float combDelayMs = 2.0f + notch * 28.0f;
    float combDelaySamples = combDelayMs * 0.001f * (float)sampleRate;
    combDelaySamples = std::clamp(combDelaySamples, 2.0f, (float)(MAX_COMB - 2));
    float combFeedback = depth * 0.85f;
    float combSweepRange = combDelaySamples * 0.4f;

    float lastFreq = 200.0f;

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;

        // ── Step 1: Input diffusion (4 cascaded modulated allpasses) ──
        // Mod depth scales with diffuser length (Valhalla principle)
        float dL = inL, dR = inR;
        for (int d = 0; d < NUM_DIFFUSERS; ++d)
        {
            float modD = baseModDepth * ((float)diffLens[d] / 521.0f);  // Scale by relative length
            float modRate = 0.1f + (float)d * 0.07f;  // 0.1, 0.17, 0.24, 0.31 Hz — very slow
            dL = diffL[d].process(dL, apCoeff, modD, modRate, sampleRate);
            dR = diffR[d].process(dR, apCoeff, modD, modRate, sampleRate);
        }

        // ── Step 2: Read from all 8 FDN delay lines ─────────
        float rd[FDN_ORDER];
        for (int i = 0; i < FDN_ORDER; ++i)
            rd[i] = fdn[i].read();

        // ── Step 3: Hadamard 8x8 mixing matrix ─────────────
        // H8 = H2 ⊗ H4 = recursive Hadamard construction
        // Normalized by 1/sqrt(8) ≈ 0.3536 for energy preservation
        constexpr float H = 0.35355339f;  // 1/sqrt(8)

        // First: apply H4 to pairs (0-3) and (4-7)
        float a0 = (rd[0] + rd[1] + rd[2] + rd[3]);
        float a1 = (rd[0] - rd[1] + rd[2] - rd[3]);
        float a2 = (rd[0] + rd[1] - rd[2] - rd[3]);
        float a3 = (rd[0] - rd[1] - rd[2] + rd[3]);
        float b0 = (rd[4] + rd[5] + rd[6] + rd[7]);
        float b1 = (rd[4] - rd[5] + rd[6] - rd[7]);
        float b2 = (rd[4] + rd[5] - rd[6] - rd[7]);
        float b3 = (rd[4] - rd[5] - rd[6] + rd[7]);

        // Then: H2 on each pair
        float mx[FDN_ORDER];
        mx[0] = (a0 + b0) * H;
        mx[1] = (a1 + b1) * H;
        mx[2] = (a2 + b2) * H;
        mx[3] = (a3 + b3) * H;
        mx[4] = (a0 - b0) * H;
        mx[5] = (a1 - b1) * H;
        mx[6] = (a2 - b2) * H;
        mx[7] = (a3 - b3) * H;

        // ── Step 4: Feedback processing per delay line ──────
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float fb = mx[i] * fbGain;

            // Absorption filter: one-pole LP in feedback loop
            // Each recirculation loses more high frequency — like real rooms
            absF[i].s += absCoeff * (fb - absF[i].s);
            fb = absF[i].s;

            // Modulated allpass in feedback loop
            // Mod depth proportional to FDN delay length (Valhalla principle)
            float lineModDepth = baseModDepth * ((float)fdn[i].len / 2000.0f);
            lineModDepth = std::clamp(lineModDepth, 0.5f, 20.0f);
            fb = fdnAP[i].process(fb, apCoeff * 0.4f, lineModDepth,
                                   modRates[i], sampleRate);

            // Soft clip
            fb = std::tanh(fb);

            // Inject input distributed across all lines (not just first!)
            float inputSig = dL * inWeightL[i] + dR * inWeightR[i];
            fdn[i].write(fb + inputSig);
        }

        // ── Step 5: Output taps (weighted sum for L/R) ──────
        float reverbL = 0.0f, reverbR = 0.0f;
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            reverbL += rd[i] * outWeightL[i];
            reverbR += rd[i] * outWeightR[i];
        }
        // Normalize (output weights sum to roughly ±2.0)
        reverbL *= 0.40f;
        reverbR *= 0.40f;

        // ── Step 6: Enigma comb modulation ──────────────────
        float lfoVal = (float)std::sin(lfoPhase * juce::MathConstants<double>::twoPi);
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;

        float modDelay = combDelaySamples + lfoVal * combSweepRange;
        modDelay = std::clamp(modDelay, 2.0f, (float)(MAX_COMB - 2));
        lastFreq = (float)sampleRate / modDelay;

        if (depth > 0.001f)
        {
            float cL = combL.process(reverbL, modDelay, combFeedback);
            float cR = combR.process(reverbR, modDelay + 3.7f, combFeedback);
            reverbL = reverbL * (1.0f - depth) + cL * depth;
            reverbR = reverbR * (1.0f - depth) + cR * depth;
        }

        wetBuffer.setSample(0, s, reverbL);
        wetBuffer.setSample(1, s, reverbR);
    }

    // ── Step 7: Mix dry/wet + DC blocking ───────────────────
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
    sweepFreq.store(lastFreq);
    sweepPos.store(std::clamp((lastFreq - 30.0f) / 470.0f, 0.0f, 1.0f));
}
