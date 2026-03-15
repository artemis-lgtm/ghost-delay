#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Delay v3.3 — Stability + Audible Controls
 *
 * Fixes from v3.2:
 *   - Parameter smoothing on ALL knobs (30ms one-pole)
 *   - Output soft-clipping prevents ANY loud transients
 *   - TONE: added direct LP filter on wet output (immediately audible)
 *   - DIFF: wider range, bypasses diffusers at 0
 *   - NOTCH: longer base delays for audible mid-range effect
 *
 * Architecture unchanged:
 *   TOP ROW = Reverb (Valhalla 8-ch FDN)
 *   BOTTOM ROW = Enigma-style notch processor
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

    void setTime(float v)     { targetSize = v; }
    void setFeedback(float v) { targetDecay = v; }
    void setDecay(float v)    { targetTone = v; }
    void setTone(float v)     { targetDiff = v; }
    void setRate(float v)     { targetRate = v; }
    void setDepth(float v)    { targetDepth = v; }
    void setSpread(float v)   { targetNotch = v; }
    void setMix(float v)      { targetMix = v; }

    float getSweepPosition() const  { return sweepPos.load(); }
    float getSweepFrequency() const { return sweepFreq.load(); }
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ═══════════════════════════════════════════════════════════
    // PARAMETER SMOOTHING (prevents glitches on fast knob moves)
    // ═══════════════════════════════════════════════════════════
    float targetSize = 0.5f, targetDecay = 0.5f, targetTone = 0.5f, targetDiff = 0.7f;
    float targetRate = 0.3f, targetDepth = 0.0f, targetNotch = 0.5f, targetMix = 0.5f;

    float smoothSize = 0.5f, smoothDecay = 0.5f, smoothTone = 0.5f, smoothDiff = 0.7f;
    float smoothRate = 0.3f, smoothDepth = 0.0f, smoothNotch = 0.5f, smoothMix = 0.5f;

    float smoothCoeff = 0.999f;  // Set in prepare() based on sample rate

    inline float smooth(float current, float target)
    {
        return current + (target - current) * (1.0f - smoothCoeff);
    }

    // ═══════════════════════════════════════════════════════════
    // 8-CHANNEL FDN REVERB
    // ═══════════════════════════════════════════════════════════
    static constexpr int FDN_ORDER = 8;
    static constexpr int MAX_DELAY = 12000;

    struct DelayLine
    {
        std::array<float, MAX_DELAY> buf {};
        int wp = 0, len = 1000;
        void clear() { buf.fill(0.0f); wp = 0; }
        void write(float s) { buf[wp] = s; wp = (wp + 1) % len; }
        float read() const { return buf[wp]; }
    };

    std::array<DelayLine, FDN_ORDER> fdn;

    struct AbsFilter { float s = 0.0f; void reset() { s = 0.0f; } };
    std::array<AbsFilter, FDN_ORDER> absF;

    // ═══════════════════════════════════════════════════════════
    // INPUT DIFFUSERS
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
            float readDelay = (float)baseLen + mod;
            readDelay = std::clamp(readDelay, 1.0f, (float)(MAX_AP - 2));
            float rp = (float)wp - readDelay;
            if (rp < 0.0f) rp += (float)MAX_AP;
            int i0 = (int)rp, i1 = (i0 + 1) % MAX_AP;
            float f = rp - (float)i0;
            float delayed = buf[i0 % MAX_AP] * (1.0f - f) + buf[i1] * f;
            float out = in * (-coeff) + delayed;
            buf[wp] = in + delayed * coeff;
            wp = (wp + 1) % MAX_AP;
            return out;
        }
    };

    std::array<ModAllpass, NUM_DIFFUSERS> diffL, diffR;
    std::array<ModAllpass, FDN_ORDER> fdnAP;

    // ═══════════════════════════════════════════════════════════
    // TONE FILTER (direct LP on wet output — immediately audible)
    // ═══════════════════════════════════════════════════════════
    struct ToneFilter
    {
        float s = 0.0f;
        void reset() { s = 0.0f; }
        // Simple one-pole LP: coeff 0..1, lower = darker
        float process(float in, float coeff)
        {
            s += coeff * (in - s);
            return s;
        }
    };
    ToneFilter toneFiltL, toneFiltR;

    // ═══════════════════════════════════════════════════════════
    // ENIGMA NOTCH PROCESSOR
    // ═══════════════════════════════════════════════════════════
    static constexpr int ENIGMA_STAGES = 6;
    static constexpr int MAX_ENIGMA_AP = 2048;

    struct EnigmaAllpass
    {
        std::array<float, MAX_ENIGMA_AP> buf {};
        int wp = 0;
        void clear() { buf.fill(0.0f); wp = 0; }

        float process(float in, float coeff, float delaySamples)
        {
            delaySamples = std::clamp(delaySamples, 1.0f, (float)(MAX_ENIGMA_AP - 2));
            float rp = (float)wp - delaySamples;
            if (rp < 0.0f) rp += (float)MAX_ENIGMA_AP;
            int i0 = (int)rp;
            int i1 = (i0 + 1) % MAX_ENIGMA_AP;
            float frac = rp - (float)i0;
            float delayed = buf[i0 % MAX_ENIGMA_AP] * (1.0f - frac) + buf[i1] * frac;
            float out = -coeff * in + delayed;
            buf[wp] = in + coeff * delayed;
            wp = (wp + 1) % MAX_ENIGMA_AP;
            return out;
        }
    };

    std::array<EnigmaAllpass, ENIGMA_STAGES> enigmaL, enigmaR;
    float enigmaFbL = 0.0f, enigmaFbR = 0.0f;
    float enigmaFiltL = 0.0f, enigmaFiltR = 0.0f;
    double lfoPhase1 = 0.0, lfoPhase2 = 0.0;

    // Longer base delays for audible mid-range notches
    static constexpr float enigmaBaseDelays[ENIGMA_STAGES] = {
        22.0f, 37.0f, 59.0f, 97.0f, 151.0f, 241.0f
    };

    static constexpr float enigmaStereoOffset[ENIGMA_STAGES] = {
        1.7f, 2.9f, 4.7f, 7.3f, 11.3f, 17.9f
    };

    // ═══════════════════════════════════════════════════════════
    // COMMON
    // ═══════════════════════════════════════════════════════════
    float hpStateL = 0.0f, hpStateR = 0.0f, hpCoeff = 0.0f;
    juce::AudioBuffer<float> wetBuffer;
    double hostBPM = 120.0;
    bool hostPlaying = false;
    double ppqPosition = 0.0;
    bool hasPPQ = false;
    double sampleRate = 44100.0;

    std::atomic<float> sweepPos{0.0f}, sweepFreq{200.0f}, rmsLevel{0.0f};

    static constexpr int baseLens[FDN_ORDER] = {
        1087, 1283, 1481, 1669, 1873, 2081, 2293, 2539
    };
    static constexpr int diffLens[NUM_DIFFUSERS] = { 142, 271, 379, 521 };
    static constexpr int fdnAPLens[FDN_ORDER] = {
        211, 263, 331, 397, 461, 541, 613, 701
    };
    static constexpr float modRates[FDN_ORDER] = {
        0.13f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f
    };
    static constexpr float inWeightL[FDN_ORDER] = {
        0.50f, 0.35f, 0.25f, 0.15f, 0.10f, 0.20f, 0.30f, 0.40f
    };
    static constexpr float inWeightR[FDN_ORDER] = {
        0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.35f, 0.25f, 0.15f
    };
    static constexpr float outWeightL[FDN_ORDER] = {
        0.40f, 0.30f, 0.20f, 0.10f, -0.10f, -0.20f, 0.30f, 0.40f
    };
    static constexpr float outWeightR[FDN_ORDER] = {
        -0.10f, 0.20f, 0.30f, 0.40f, 0.40f, 0.30f, -0.20f, 0.10f
    };
};
