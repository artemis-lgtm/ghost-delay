#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Reverb v7.0 — dedicated FDN reverb
 *
 * Signal chain: DRY → diffusion → 8-line FDN (w/ shimmer return) → TONE
 *               → GRIT → WIDTH → DUCK → MIX
 *
 * TOP ROW:    SIZE, DECAY, TONE, MIX
 * BOTTOM ROW: SHIMMER, DUCK, WIDTH, GRIT
 *
 * v7.0 audit fixes (council pass 2026-06-11):
 *  - FDN delay lines: fixed wrap at MAX_DELAY + smoothed fractional read tap
 *    (was: per-sample integer `% len` re-wrap → stale-buffer clicks)
 *  - fbGain capped 0.96, line mod depth capped 4 samples, per-line tanh
 *    removed (was: pinned at tanh knee at max DECAY → "blown out" screech)
 *  - No heap allocation on the audio thread (dry copy eliminated)
 *  - MIX applied with per-sample smoothing at the application point
 */
class GhostEngine
{
public:
    GhostEngine();
    ~GhostEngine() = default;

    void prepare(double sampleRate, int blockSize);
    void process(juce::AudioBuffer<float>& buffer,
                 juce::AudioPlayHead* playHead);
    void reset();

    // Top row
    void setSize(float v)  { targetSize = v; }
    void setDecay(float v) { targetDecay = v; }
    void setTone(float v)  { targetTone = v; }
    void setMix(float v)   { targetMix = v; }

    // Bottom row — reverb-native effects
    void setShimmer(float v) { targetShimmer = v; }  // octave-up tail regen
    void setDuck(float v)    { targetDuck = v; }     // input ducks the wet
    void setWidth(float v)   { targetWidth = v; }    // M/S width 0..150%
    void setGrit(float v)    { targetGrit = v; }     // wet saturation + darken

    // UI queries
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ═══════════════════════════════════════════════════════════
    // PARAMETER SMOOTHING (~30ms prevents clicks on knob moves)
    // ═══════════════════════════════════════════════════════════
    float targetSize = 0.5f,  targetDecay = 0.4f;
    float targetTone = 0.6f,  targetMix = 0.35f;
    float targetShimmer = 0.0f, targetDuck = 0.0f;
    float targetWidth = 0.667f, targetGrit = 0.0f;

    float smoothSize = 0.5f,  smoothDecay = 0.4f;
    float smoothTone = 0.6f,  smoothMix = 0.35f;
    float smoothShimmer = 0.0f, smoothDuck = 0.0f;
    float smoothWidth = 0.667f, smoothGrit = 0.0f;

    float smoothCoeff = 0.999f;

    inline float smooth(float current, float target)
    {
        return current + (target - current) * (1.0f - smoothCoeff);
    }

    // ═══════════════════════════════════════════════════════════
    // 8-CHANNEL FDN REVERB
    // Delay lines wrap at MAX_DELAY always; the *read tap* is a
    // smoothed fractional offset. Changing SIZE moves the tap, it
    // never re-wraps the buffer (the v6 click bug).
    // ═══════════════════════════════════════════════════════════
    static constexpr int FDN_ORDER = 8;
    static constexpr int MAX_DELAY = 16384;   // power of two for cheap wrap

    struct DelayLine
    {
        std::array<float, MAX_DELAY> buf {};
        int wp = 0;
        void clear() { buf.fill(0.0f); wp = 0; }
        void write(float s) { buf[(size_t)wp] = s; wp = (wp + 1) & (MAX_DELAY - 1); }
        // Fractional read: delay measured in samples back from the NEXT
        // write position (so delay D reads the sample written D samples ago).
        float read(float delaySamples) const
        {
            float rp = (float)wp - delaySamples;
            if (rp < 0.0f) rp += (float)MAX_DELAY;
            int i0 = (int)rp;
            int i1 = (i0 + 1) & (MAX_DELAY - 1);
            float f = rp - (float)i0;
            return buf[(size_t)(i0 & (MAX_DELAY - 1))] * (1.0f - f)
                 + buf[(size_t)i1] * f;
        }
    };

    std::array<DelayLine, FDN_ORDER> fdn;
    std::array<float, FDN_ORDER> fdnDelay {};       // smoothed fractional delay per line

    // Absorption filter (one-pole LP per delay line in feedback loop)
    struct AbsFilter { float s = 0.0f; void reset() { s = 0.0f; } };
    std::array<AbsFilter, FDN_ORDER> absF;

    // ═══════════════════════════════════════════════════════════
    // INPUT DIFFUSERS (4 cascaded modulated allpasses)
    // ═══════════════════════════════════════════════════════════
    static constexpr int NUM_DIFFUSERS = 4;
    static constexpr int MAX_AP = 4096;

    struct ModAllpass
    {
        std::array<float, MAX_AP> buf {};
        int wp = 0, baseLen = 500;
        float phase = 0.0f;
        void clear() { buf.fill(0.0f); wp = 0; }

        float process(float in, float coeff, float modSamples, float modHz, double sr)
        {
            phase += (float)(modHz / sr);
            if (phase >= 1.0f) phase -= 1.0f;
            float mod = modSamples * std::sin(phase * juce::MathConstants<float>::twoPi);
            float readDelay = std::clamp((float)baseLen + mod, 1.0f, (float)(MAX_AP - 2));
            float rp = (float)wp - readDelay;
            if (rp < 0.0f) rp += (float)MAX_AP;
            int i0 = (int)rp, i1 = (i0 + 1) % MAX_AP;
            float f = rp - (float)i0;
            float delayed = buf[(size_t)(i0 % MAX_AP)] * (1.0f - f) + buf[(size_t)i1] * f;
            float out = in * (-coeff) + delayed;
            buf[(size_t)wp] = in + delayed * coeff;
            wp = (wp + 1) % MAX_AP;
            return out;
        }
    };

    std::array<ModAllpass, NUM_DIFFUSERS> diffL, diffR;
    std::array<ModAllpass, FDN_ORDER> fdnAP;

    // ═══════════════════════════════════════════════════════════
    // TONE FILTER (direct LP on wet output)
    // ═══════════════════════════════════════════════════════════
    struct ToneFilter
    {
        float s = 0.0f;
        void reset() { s = 0.0f; }
        float process(float in, float coeff)
        {
            s += coeff * (in - s);
            return s;
        }
    };
    ToneFilter toneFiltL, toneFiltR;

    // ═══════════════════════════════════════════════════════════
    // SHIMMER — dual-tap granular octave-up shifter on the tail,
    // returned into the tank input. No FFT, two interpolated reads.
    // ═══════════════════════════════════════════════════════════
    static constexpr int SHIM_BUF = 8192;        // power of two
    std::array<float, SHIM_BUF> shimBuf {};
    int   shimWp = 0;
    float shimPhase = 0.0f;
    float shimWindow = 2048.0f;                  // samples, set in prepare()
    float shimOut = 0.0f;                        // previous-sample output (loop-safe)

    float shimmerRead(float delaySamples) const
    {
        float rp = (float)shimWp - std::max(delaySamples, 1.0f);
        if (rp < 0.0f) rp += (float)SHIM_BUF;
        int i0 = (int)rp, i1 = (i0 + 1) & (SHIM_BUF - 1);
        float f = rp - (float)i0;
        return shimBuf[(size_t)(i0 & (SHIM_BUF - 1))] * (1.0f - f)
             + shimBuf[(size_t)i1] * f;
    }

    // ═══════════════════════════════════════════════════════════
    // DUCK — envelope follower on the dry input gates the wet
    // ═══════════════════════════════════════════════════════════
    float duckEnv = 0.0f;
    float duckAttack = 0.0f, duckRelease = 0.0f;  // coeffs set in prepare()
    float duckGainSm = 1.0f;                      // smoothed gain (anti-zipper)

    // ═══════════════════════════════════════════════════════════
    // GRIT — wet drive + progressive darkening
    // ═══════════════════════════════════════════════════════════
    ToneFilter gritLpL, gritLpR;

    // ═══════════════════════════════════════════════════════════
    // COMMON
    // ═══════════════════════════════════════════════════════════
    float hpStateL = 0.0f, hpStateR = 0.0f, hpCoeff = 0.0f;
    juce::AudioBuffer<float> wetBuffer;
    double hostBPM = 120.0;
    bool hostPlaying = false;
    double sampleRate = 44100.0;

    std::atomic<float> rmsLevel{0.0f};

    // FDN delay lengths (mutually prime, samples at 44.1kHz)
    static constexpr int baseLens[FDN_ORDER] = {
        1087, 1283, 1481, 1669, 1873, 2081, 2293, 2539
    };
    static constexpr int diffLens[NUM_DIFFUSERS] = { 142, 271, 379, 521 };
    static constexpr int fdnAPLens[FDN_ORDER] = {
        211, 263, 331, 397, 461, 541, 613, 701
    };
    // 8 uncorrelated LFO rates for delay line modulation (Hz)
    static constexpr float modRates[FDN_ORDER] = {
        0.13f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f
    };
    // Input distribution weights (mono-summed, then distributed)
    static constexpr float inWeight[FDN_ORDER] = {
        0.40f, 0.30f, 0.25f, 0.20f, 0.20f, 0.25f, 0.30f, 0.40f
    };
    // Output tap weights (positive only, symmetric sums for L/R balance)
    static constexpr float outWeightL[FDN_ORDER] = {
        0.40f, 0.30f, 0.20f, 0.10f, 0.10f, 0.20f, 0.30f, 0.40f
    };
    static constexpr float outWeightR[FDN_ORDER] = {
        0.10f, 0.20f, 0.30f, 0.40f, 0.40f, 0.30f, 0.20f, 0.10f
    };

    static constexpr float DIFFUSION_COEFF = 0.6f;

    // Stability: max feedback gain (was 0.985 — sat at the tanh knee and
    // screeched; 0.96 + time-varying allpasses stays musical and stable)
    static constexpr float MAX_FB = 0.96f;
};
