#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Delay v2.3 — Underwater Ghost
 *
 * Signal chain:
 *   Input → Stereo Delay (tempo-synced, with feedback) →
 *   Reverb (Freeverb, heavy damping) → Subtle Saturation →
 *   Global Low-Pass (TONE controls cutoff, 200 Hz–2 kHz) →
 *   Animated Bandpass Sweep (100–400 Hz, slow LFO) →
 *   Blend with LP-filtered signal (DEPTH) →
 *   Stereo Spread → Mix with dry → HP filter → Output
 *
 * 8 Parameters:
 *   TIME     — Delay time (quantized subdivisions when synced)
 *   FEEDBACK — Delay repeats with tanh saturation
 *   DECAY    — Reverb room size / tail length
 *   TONE     — Wet signal low-pass cutoff (dark ← → less dark, never bright)
 *   RATE     — Sweep LFO speed (tempo-synced via PPQ)
 *   DEPTH    — Blend between LP-filtered and swept+LP-filtered reverb
 *   SPREAD   — Stereo width (delay offset + reverb width + LFO phase offset)
 *   MIX      — Dry/wet
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

    // ── Parameter setters ───────────────────────────────────
    void setTime(float v)     { time = v; }
    void setFeedback(float v) { feedback = v; }
    void setDecay(float v)    { decay = v; }
    void setTone(float v)     { tone = v; }
    void setRate(float v)     { rate = v; }
    void setDepth(float v)    { depth = v; }
    void setSpread(float v)   { spread = v; }
    void setMix(float v)      { mix = v; }

    // ── UI queries ──────────────────────────────────────────
    float getSweepPosition() const  { return sweepPos.load(); }
    float getSweepFrequency() const { return sweepFreq.load(); }
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ── Delay ───────────────────────────────────────────────
    static constexpr int MAX_DELAY_SAMPLES = 192000; // ~4s at 48kHz
    std::array<float, MAX_DELAY_SAMPLES> delayL {};
    std::array<float, MAX_DELAY_SAMPLES> delayR {};
    int delayWriteL = 0;
    int delayWriteR = 0;
    float smoothDelayL = 0.0f;
    float smoothDelayR = 0.0f;

    // ── Reverb ──────────────────────────────────────────────
    juce::Reverb reverb;

    // ── SVF (Cytomic topology) ──────────────────────────────
    struct SVF
    {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;

        /** Process one sample, return bandpass output. */
        float processBandpass(float input, float cutoffHz, float q, double sr)
        {
            float w = juce::MathConstants<float>::pi * std::min(cutoffHz, (float)(sr * 0.49f)) / (float)sr;
            float g = std::tan(w);
            float k = 1.0f / std::max(q, 0.1f);
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            float v3 = input - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;

            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            return v1;   // bandpass
        }

        /** Process one sample, return low-pass output. */
        float processLowpass(float input, float cutoffHz, float q, double sr)
        {
            float w = juce::MathConstants<float>::pi * std::min(cutoffHz, (float)(sr * 0.49f)) / (float)sr;
            float g = std::tan(w);
            float k = 1.0f / std::max(q, 0.1f);
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            float v3 = input - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;

            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            return v2;   // low-pass
        }

        void reset() { ic1eq = ic2eq = 0.0f; }
    };

    // Bandpass sweep SVFs
    SVF svfL, svfR;
    // Global low-pass SVFs (TONE control)
    SVF lpfL, lpfR;

    // ── DC-blocking high-pass (1-pole at 30 Hz) ────────────
    float hpStateL = 0.0f;
    float hpStateR = 0.0f;
    float hpCoeff  = 0.0f;

    // ── LFO ─────────────────────────────────────────────────
    double lfoPhase = 0.0;

    // ── Pre-allocated wet buffer ────────────────────────────
    juce::AudioBuffer<float> wetBuffer;

    // ── Parameters (0–1) ────────────────────────────────────
    float time     = 0.5f;
    float feedback = 0.4f;
    float decay    = 0.5f;
    float tone     = 0.5f;
    float rate     = 0.5f;
    float depth    = 0.7f;
    float spread   = 0.3f;
    float mix      = 0.5f;

    // ── Host transport ──────────────────────────────────────
    double hostBPM    = 120.0;
    bool hostPlaying  = false;
    double ppqPosition = 0.0;
    bool hasPPQ       = false;

    double sampleRate = 44100.0;

    // ── UI feedback (atomic for thread safety) ──────────────
    std::atomic<float> sweepPos  { 0.0f };
    std::atomic<float> sweepFreq { 200.0f };
    std::atomic<float> rmsLevel  { 0.0f };

    // ── Helpers ─────────────────────────────────────────────
    float getDelayTimeSamples(int channel) const;
    double getSweepPeriodBeats() const;
    float  getSweepRateHz() const;
    void   updateReverbParams();

    static float readDelay(const float* buf, int writePos, float delaySamples);

    // ── Tempo-sync subdivision table (in beats) ─────────────
    static constexpr int NUM_SUBS = 9;
    static constexpr float delaySubs[NUM_SUBS] = {
        0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f
    };
};
