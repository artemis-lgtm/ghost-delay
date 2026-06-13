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
    smoothSize    = targetSize;    smoothDecay = targetDecay;
    smoothTone    = targetTone;    smoothMix   = targetMix;
    smoothShimmer = targetShimmer; smoothDuck  = targetDuck;
    smoothWidth   = targetWidth;   smoothGrit  = targetGrit;

    // FDN delay lines — buffers wrap at MAX_DELAY, only the read tap moves
    float sizeScale = 0.15f + smoothSize * 2.85f;
    for (int i = 0; i < FDN_ORDER; ++i)
    {
        fdn[i].clear();
        absF[i].reset();
        fdnDelay[i] = std::clamp(baseLens[i] * srScale * sizeScale,
                                 4.0f, (float)(MAX_DELAY - 8));
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

    // Tone + grit filters
    toneFiltL.reset(); toneFiltR.reset();
    gritLpL.reset();   gritLpR.reset();

    // DC blocker
    float hpFreq = 20.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = hpStateR = 0.0f;

    // Shimmer: ~46ms grain window, scaled to sample rate
    shimBuf.fill(0.0f);
    shimWp = 0; shimPhase = 0.0f; shimOut = 0.0f;
    shimWindow = std::clamp(2048.0f * srScale, 512.0f, (float)(SHIM_BUF - 8));

    // Duck envelope: 5ms attack, 250ms release
    duckAttack  = 1.0f - std::exp(-1.0f / (0.005f * (float)sr));
    duckRelease = 1.0f - std::exp(-1.0f / (0.25f  * (float)sr));
    duckEnv = 0.0f; duckGainSm = 1.0f;

    // Preallocate — never allocate on the audio thread (v6 did, every block)
    wetBuffer.setSize(2, std::max(blockSize, 64));
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
    gritLpL.reset();   gritLpR.reset();
    hpStateL = hpStateR = 0.0f;
    shimBuf.fill(0.0f); shimWp = 0; shimPhase = 0.0f; shimOut = 0.0f;
    duckEnv = 0.0f; duckGainSm = 1.0f;
}

void GhostEngine::process(juce::AudioBuffer<float>& buffer,
                           juce::AudioPlayHead* playHead)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), 2);

    // Read host transport
    if (playHead != nullptr)
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBPM = std::max(*bpm, 20.0);
            hostPlaying = pos->getIsPlaying();
        }
    }

    // Defensive only — sized in prepare(); a host exceeding its declared
    // block size is the only way this allocates.
    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(2, numSamples, false, false, true);

    float srScale = (float)(sampleRate / 44100.0);

    // Smoothing for the delay-tap glide: slower than parameter smoothing
    // (~80ms) so SIZE sweeps sound like tape varispeed, not zipper.
    const float tapCoeff = std::exp(-1.0f / (0.08f * (float)sampleRate));

    float rms = 0.0f;

    // ═══════════════════════════════════════════════════════════
    // PER-SAMPLE PROCESSING (single pass: reverb → fx → mix)
    // ═══════════════════════════════════════════════════════════
    for (int s = 0; s < numSamples; ++s)
    {
        // ── Smooth parameters ───────────────────────────────
        smoothSize    = smooth(smoothSize,    targetSize);
        smoothDecay   = smooth(smoothDecay,   targetDecay);
        smoothTone    = smooth(smoothTone,    targetTone);
        smoothMix     = smooth(smoothMix,     targetMix);
        smoothShimmer = smooth(smoothShimmer, targetShimmer);
        smoothDuck    = smooth(smoothDuck,    targetDuck);
        smoothWidth   = smooth(smoothWidth,   targetWidth);
        smoothGrit    = smooth(smoothGrit,    targetGrit);

        // ── Derived parameters ──────────────────────────────
        float sizeScale = 0.15f + smoothSize * 2.85f;

        // DECAY: quadratic, capped at MAX_FB (0.96). Shimmer feeds energy
        // back into the tank, so its amount trims feedback to compensate.
        float fbGain = smoothDecay * smoothDecay * MAX_FB;
        fbGain *= (1.0f - 0.22f * smoothShimmer);

        float absCoeff = 0.02f + smoothTone * 0.90f;
        float toneLP   = 0.05f + smoothTone * 0.93f;
        float baseModDepth = 0.5f + smoothSize * 6.0f;

        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;
        float inMono = (inL + inR) * 0.5f;

        // ── Duck envelope (from dry input) ──────────────────
        float inAbs = std::abs(inMono);
        duckEnv += (inAbs > duckEnv ? duckAttack : duckRelease) * (inAbs - duckEnv);

        // ── 1. Input diffusion ──────────────────────────────
        float dL = inMono, dR = inMono;
        for (int d = 0; d < NUM_DIFFUSERS; ++d)
        {
            float modD = baseModDepth * ((float)diffLens[d] / 521.0f);
            float modRate = 0.1f + (float)d * 0.07f;
            dL = diffL[d].process(dL, DIFFUSION_COEFF, modD, modRate, sampleRate);
            dR = diffR[d].process(dR, DIFFUSION_COEFF, modD, modRate, sampleRate);
        }

        // ── 2. Glide FDN read taps toward SIZE target ───────
        // Fractional, smoothed, interpolated — buffers never re-wrap.
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float targetLen = std::clamp(baseLens[i] * srScale * sizeScale,
                                         4.0f, (float)(MAX_DELAY - 8));
            fdnDelay[i] = targetLen + (fdnDelay[i] - targetLen) * tapCoeff;
        }

        // ── 3. FDN: read from delay lines ───────────────────
        float rd[FDN_ORDER];
        for (int i = 0; i < FDN_ORDER; ++i)
            rd[i] = fdn[i].read(fdnDelay[i]);

        // ── 4. Hadamard 8x8 mixing matrix ──────────────────
        constexpr float H = 0.35355339f;  // 1/sqrt(8)
        float a0 = rd[0]+rd[1]+rd[2]+rd[3], a1 = rd[0]-rd[1]+rd[2]-rd[3];
        float a2 = rd[0]+rd[1]-rd[2]-rd[3], a3 = rd[0]-rd[1]-rd[2]+rd[3];
        float b0 = rd[4]+rd[5]+rd[6]+rd[7], b1 = rd[4]-rd[5]+rd[6]-rd[7];
        float b2 = rd[4]+rd[5]-rd[6]-rd[7], b3 = rd[4]-rd[5]-rd[6]+rd[7];

        float mx[FDN_ORDER];
        mx[0]=(a0+b0)*H; mx[1]=(a1+b1)*H; mx[2]=(a2+b2)*H; mx[3]=(a3+b3)*H;
        mx[4]=(a0-b0)*H; mx[5]=(a1-b1)*H; mx[6]=(a2-b2)*H; mx[7]=(a3-b3)*H;

        // ── 5. Feedback processing + write back ─────────────
        // No per-line tanh (v6 bug: pinned the loop at the saturation knee).
        // Stability comes from fbGain < 1 and the absorption LP; a single
        // hard safety clamp guards against transient mod-energy overshoot.
        float shimReturn = shimOut * smoothShimmer * 0.35f;
        for (int i = 0; i < FDN_ORDER; ++i)
        {
            float fb = mx[i] * fbGain;

            // Absorption filter (one-pole LP in feedback loop)
            absF[i].s += absCoeff * (fb - absF[i].s);
            fb = absF[i].s;

            // Modulated allpass (depth capped at 4 — was 15, audible warble
            // and transient loop-gain overshoot at large sizes)
            float lineModDepth = std::clamp(
                baseModDepth * (fdnDelay[i] / 2000.0f), 0.3f, 4.0f);
            fb = fdnAP[i].process(fb, DIFFUSION_COEFF * 0.4f,
                                   lineModDepth, modRates[i], sampleRate);

            // Safety clamp only (linear region untouched)
            fb = std::clamp(fb, -4.0f, 4.0f);

            float inputSig = ((dL + dR) * 0.5f + shimReturn) * inWeight[i];
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

        // ── 8. SHIMMER: dual-tap octave-up on the tail ──────
        // Write the (post-tone) tail into the grain buffer, read two
        // crossfaded taps whose delay ramps W→0 (rate 2x = octave up).
        shimBuf[(size_t)shimWp] = (reverbL + reverbR) * 0.5f;
        shimWp = (shimWp + 1) & (SHIM_BUF - 1);
        shimPhase += 1.0f / shimWindow;
        if (shimPhase >= 1.0f) shimPhase -= 1.0f;
        float p2 = shimPhase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        float g1 = std::sin(shimPhase * juce::MathConstants<float>::pi);
        float g2 = std::sin(p2        * juce::MathConstants<float>::pi);
        shimOut = shimmerRead(shimWindow * (1.0f - shimPhase)) * g1
                + shimmerRead(shimWindow * (1.0f - p2))        * g2;

        // ── 9. GRIT: wet drive + progressive darkening ──────
        if (smoothGrit > 0.001f)
        {
            float drive = 1.0f + smoothGrit * 5.0f;
            float norm  = 1.0f / std::tanh(drive);
            // LP closes from wide-open toward ~5kHz as grit rises
            float gritLP = 1.0f - smoothGrit * 0.72f;
            float gl = std::tanh(reverbL * drive) * norm;
            float gr = std::tanh(reverbR * drive) * norm;
            reverbL = gritLpL.process(gl, gritLP);
            reverbR = gritLpR.process(gr, gritLP);
        }
        else
        {
            // Keep filter states warm to avoid steps when grit engages
            gritLpL.process(reverbL, 1.0f);
            gritLpR.process(reverbR, 1.0f);
        }

        // ── 10. WIDTH: M/S scale on the wet ─────────────────
        {
            float width = smoothWidth * 1.5f;   // 0..1.5, default knob ⇒ 1.0
            float m = (reverbL + reverbR) * 0.5f;
            float sd = (reverbL - reverbR) * 0.5f * width;
            reverbL = m + sd;
            reverbR = m - sd;
        }

        // ── 11. DUCK: dry input pushes the wet down ─────────
        {
            float duckGain = 1.0f - smoothDuck * std::min(duckEnv * 4.0f, 1.0f);
            duckGainSm += 0.005f * (duckGain - duckGainSm);  // anti-zipper
            reverbL *= duckGainSm;
            reverbR *= duckGainSm;
        }

        // ── 12. Output soft-clip (safety) ───────────────────
        reverbL = std::tanh(reverbL);
        reverbR = std::tanh(reverbR);

        // ── 13. Mix dry/wet (per-sample smoothed) + DC block ─
        // Dry stays true stereo (v6 mono-summed it — collapsed stereo
        // sources). Mono-in-stereo-buffer is handled by the processor's
        // channel-copy with hysteresis, so inR is always meaningful here.
        float outL = inL * (1.0f - smoothMix) + reverbL * smoothMix;
        float outR = inR * (1.0f - smoothMix) + reverbR * smoothMix;

        float hpOutL = outL - hpStateL;
        hpStateL = outL - hpCoeff * hpOutL;
        float hpOutR = outR - hpStateR;
        hpStateR = outR - hpCoeff * hpOutR;

        buffer.setSample(0, s, hpOutL);
        if (buffer.getNumChannels() > 1)
            buffer.setSample(1, s, hpOutR);

        rms += hpOutL * hpOutL + hpOutR * hpOutR;
    }

    rms = std::sqrt(rms / (float)(numSamples * 2));
    rmsLevel.store(rms);
}
