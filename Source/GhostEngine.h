#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Delay v3.2 — Valhalla FDN Reverb + Enigma Notch Processor
 *
 * Architecture:
 *   TOP ROW = Reverb (Valhalla-inspired 8-channel FDN)
 *   BOTTOM ROW = Enigma-style notch processor
 *
 * The Enigma section is based on deep analysis of the Waves Enigma
 * manual and architecture:
 *   - Multi-stage allpass chain (6 stages = 3 notch pairs)
 *   - LFO-modulated allpass delays (sweep notches through spectrum)
 *   - Feedback path through the allpass chain (creates resonant peaks)
 *   - Feedback path filtered for tonal shaping
 *   - Stereo offset between L/R chains for natural imaging
 *
 * Signal chain:
 *   Input → Input Diffusers → 8-ch FDN Reverb →
 *   Enigma Notch Processor (6-stage allpass with feedback) →
 *   Mix with dry → Output
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

    // Parameter setters
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
    // ENIGMA NOTCH PROCESSOR
    // 6-stage allpass chain with feedback (like Waves Enigma)
    // Creates 3 notch pairs that sweep through the spectrum
    // ═══════════════════════════════════════════════════════════
    static constexpr int ENIGMA_STAGES = 6;
    static constexpr int MAX_ENIGMA_AP = 2048;

    struct EnigmaAllpass
    {
        std::array<float, MAX_ENIGMA_AP> buf {};
        int wp = 0;
        void clear() { buf.fill(0.0f); wp = 0; }

        // Process with fractional delay (interpolated read)
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

    // 6 allpass stages per channel
    std::array<EnigmaAllpass, ENIGMA_STAGES> enigmaL, enigmaR;

    // Enigma feedback state (filtered)
    float enigmaFbL = 0.0f, enigmaFbR = 0.0f;

    // Enigma feedback filter (one-pole LP to shape feedback tone)
    float enigmaFiltL = 0.0f, enigmaFiltR = 0.0f;

    // Dual LFO for richer sweep (two uncorrelated sine waves)
    double lfoPhase1 = 0.0, lfoPhase2 = 0.0;

    // Base delay times for the 6 enigma stages (samples at 44.1kHz)
    // Staggered for maximum notch spread
    static constexpr float enigmaBaseDelays[ENIGMA_STAGES] = {
        8.0f, 13.0f, 21.0f, 34.0f, 55.0f, 89.0f  // Fibonacci-ish for non-harmonic spacing
    };

    // Phase offsets per stage for stereo decorrelation
    static constexpr float enigmaStereoOffset[ENIGMA_STAGES] = {
        1.3f, 2.1f, 3.4f, 5.5f, 8.9f, 14.4f
    };

    // ═══════════════════════════════════════════════════════════
    // COMMON STATE
    // ═══════════════════════════════════════════════════════════
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

    // FDN delay lengths (mutually prime, samples at 44.1kHz)
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
