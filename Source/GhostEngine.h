#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>
#include "EnigmaFilter.h"

/**
 * Ghost Delay v6.0 — Reverb → Enigma Filter
 *
 * Signal chain: DRY → FDN Reverb → Enigma Phaser → MIX
 *
 * TOP ROW:    SIZE, DECAY, TONE, MIX
 * BOTTOM ROW: DEPTH, FEEDBACK, RATE, ENIGMA MIX
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

    // Active parameters (top row)
    void setTime(float v)     { targetSize = v; }      // SIZE
    void setFeedback(float v) { targetDecay = v; }     // DECAY
    void setDecay(float v)    { targetTone = v; }      // TONE
    void setTone(float v)     { targetReverbMix = v; } // MIX (repurposed)

    // Bottom row — Enigma filter controls
    void setRate(float v)   { targetEnigmaDepth = v; }     // DEPTH (sweep width)
    void setDepth(float v)  { targetEnigmaFeedback = v; }  // FEEDBACK (resonance)
    void setSpread(float v) { targetEnigmaRate = v; }      // RATE (wobble speed)
    void setMix(float v)    { targetEnigmaMix = v; }       // MIX (effect blend)

    // UI queries
    float getSweepPosition() const  { return sweepPos.load(); }
    float getSweepFrequency() const { return sweepFreq.load(); }
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ═══════════════════════════════════════════════════════════
    // PARAMETER SMOOTHING (~30ms prevents clicks on knob moves)
    // ═══════════════════════════════════════════════════════════
    float targetSize = 0.5f, targetDecay = 0.5f;
    float targetTone = 0.5f, targetReverbMix = 0.5f;

    float smoothSize = 0.5f, smoothDecay = 0.5f;
    float smoothTone = 0.5f, smoothReverbMix = 0.5f;

    float smoothCoeff = 0.999f;

    inline float smooth(float current, float target)
    {
        return current + (target - current) * (1.0f - smoothCoeff);
    }

    // ═══════════════════════════════════════════════════════════
    // 8-CHANNEL FDN REVERB (Valhalla-inspired)
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
    // Different per-line emphasis creates stereo width without panning artifacts
    static constexpr float outWeightL[FDN_ORDER] = {
        0.40f, 0.30f, 0.20f, 0.10f, 0.10f, 0.20f, 0.30f, 0.40f
    };
    static constexpr float outWeightR[FDN_ORDER] = {
        0.10f, 0.20f, 0.30f, 0.40f, 0.40f, 0.30f, 0.20f, 0.10f
    };

    // Hard-coded diffusion coefficient (good general-purpose value)
    static constexpr float DIFFUSION_COEFF = 0.6f;

    // ═══════════════════════════════════════════════════════════
    // ENIGMA FILTER (post-reverb modulation)
    // ═══════════════════════════════════════════════════════════
    EnigmaFilter enigmaL, enigmaR;

    float targetEnigmaDepth    = 0.5f, smoothEnigmaDepth    = 0.5f;
    float targetEnigmaFeedback = 0.3f, smoothEnigmaFeedback = 0.3f;
    float targetEnigmaRate     = 0.3f, smoothEnigmaRate     = 0.3f;
    float targetEnigmaMix      = 0.0f, smoothEnigmaMix      = 0.0f;
};
