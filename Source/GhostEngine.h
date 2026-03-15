#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Delay v3.1 — Valhalla-quality FDN Reverb + Enigma Modulation
 *
 * Architecture based on:
 *   - Stautner/Puckette (1982): FDN reverb with rotation matrix
 *   - Jot (1992): Frequency-dependent decay via absorption filters
 *   - Sean Costello / Valhalla DSP: Modulated allpasses, scaled mod depth,
 *     no-net-pitch-change modulation, dense input diffusion
 *   - Julius O. Smith: Mutually prime delay lengths, Hadamard mixing
 *
 * Signal chain:
 *   Input → 4x Cascaded Input Diffusers (modulated) →
 *   8-channel FDN (Hadamard matrix, per-line LP, modulated allpasses) →
 *   L/R output taps → Enigma comb modulation → Mix → Output
 *
 * Top row:  SIZE, DECAY, TONE, DIFF
 * Bottom:   RATE, DEPTH, NOTCH, MIX
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

    // Parameter setters (mapped from APVTS IDs)
    void setTime(float v)     { size = v; }
    void setFeedback(float v) { decay = v; }
    void setDecay(float v)    { fdnTone = v; }
    void setTone(float v)     { diff = v; }
    void setRate(float v)     { rate = v; }
    void setDepth(float v)    { depth = v; }
    void setSpread(float v)   { notch = v; }
    void setMix(float v)      { mix = v; }

    // UI queries
    float getSweepPosition() const  { return sweepPos.load(); }
    float getSweepFrequency() const { return sweepFreq.load(); }
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ═══════════════════════════════════════════════════════════
    // 8-CHANNEL FDN (single network, stereo I/O)
    // ═══════════════════════════════════════════════════════════
    static constexpr int FDN_ORDER = 8;
    static constexpr int MAX_DELAY = 12000;  // ~250ms at 48kHz

    struct DelayLine
    {
        std::array<float, MAX_DELAY> buf {};
        int wp = 0, len = 1000;
        void clear() { buf.fill(0.0f); wp = 0; }
        void write(float s) { buf[wp] = s; wp = (wp + 1) % len; }
        float read() const { return buf[wp]; }
        float readInterp(float delay) const
        {
            float rp = (float)wp - delay;
            if (rp < 0.0f) rp += (float)len;
            int i0 = (int)rp, i1 = (i0 + 1) % len;
            float f = rp - (float)i0;
            return buf[i0 % len] * (1.0f - f) + buf[i1] * f;
        }
    };

    std::array<DelayLine, FDN_ORDER> fdn;

    // Per-line absorption filter (one-pole LP in feedback)
    struct AbsFilter { float s = 0.0f; void reset() { s = 0.0f; } };
    std::array<AbsFilter, FDN_ORDER> absF;

    // ═══════════════════════════════════════════════════════════
    // INPUT DIFFUSERS (4 cascaded, modulated allpasses)
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
            // Multi-rate LFO for no-net-pitch-change
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

    // Input diffusers: 2 per channel (L/R) × 2 stages = 4 total per channel
    std::array<ModAllpass, NUM_DIFFUSERS> diffL, diffR;

    // FDN internal modulated allpasses (one per delay line)
    std::array<ModAllpass, FDN_ORDER> fdnAP;

    // ═══════════════════════════════════════════════════════════
    // ENIGMA COMB FILTER
    // ═══════════════════════════════════════════════════════════
    static constexpr int MAX_COMB = 4096;
    struct Comb
    {
        std::array<float, MAX_COMB> buf {};
        int wp = 0;
        void clear() { buf.fill(0.0f); wp = 0; }
        float process(float in, float delay, float fb)
        {
            float rp = (float)wp - delay;
            if (rp < 0.0f) rp += (float)MAX_COMB;
            int i0 = (int)rp, i1 = (i0 + 1) % MAX_COMB;
            float f = rp - (float)i0;
            float del = buf[i0] * (1.0f - f) + buf[i1] * f;
            buf[wp] = in + del * fb;
            wp = (wp + 1) % MAX_COMB;
            return del;
        }
    };
    Comb combL, combR;

    double lfoPhase = 0.0;
    float hpStateL = 0.0f, hpStateR = 0.0f, hpCoeff = 0.0f;
    juce::AudioBuffer<float> wetBuffer;

    // Parameters (0-1)
    float size = 0.5f, decay = 0.5f, fdnTone = 0.5f, diff = 0.7f;
    float rate = 0.3f, depth = 0.5f, notch = 0.5f, mix = 0.5f;

    double hostBPM = 120.0;
    bool hostPlaying = false;
    double ppqPosition = 0.0;
    bool hasPPQ = false;
    double sampleRate = 44100.0;

    std::atomic<float> sweepPos{0.0f}, sweepFreq{200.0f}, rmsLevel{0.0f};

    // Mutually prime base delay lengths (samples at 44.1kHz)
    // Chosen for maximum echo density and no common factors
    static constexpr int baseLens[FDN_ORDER] = {
        1087, 1283, 1481, 1669, 1873, 2081, 2293, 2539
    };

    // Input diffuser lengths (increasing, mutually prime)
    static constexpr int diffLens[NUM_DIFFUSERS] = { 142, 271, 379, 521 };

    // FDN internal allpass lengths
    static constexpr int fdnAPLens[FDN_ORDER] = {
        211, 263, 331, 397, 461, 541, 613, 701
    };

    // Per-line LFO rates (all different for decorrelation)
    static constexpr float modRates[FDN_ORDER] = {
        0.13f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f
    };

    // L/R input distribution weights (spread input across all 8 lines)
    static constexpr float inWeightL[FDN_ORDER] = {
        0.50f, 0.35f, 0.25f, 0.15f, 0.10f, 0.20f, 0.30f, 0.40f
    };
    static constexpr float inWeightR[FDN_ORDER] = {
        0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.35f, 0.25f, 0.15f
    };

    // L/R output tap weights (different from input for decorrelation)
    static constexpr float outWeightL[FDN_ORDER] = {
        0.40f, 0.30f, 0.20f, 0.10f, -0.10f, -0.20f, 0.30f, 0.40f
    };
    static constexpr float outWeightR[FDN_ORDER] = {
        -0.10f, 0.20f, 0.30f, 0.40f, 0.40f, 0.30f, -0.20f, 0.10f
    };
};
