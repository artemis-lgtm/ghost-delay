#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <atomic>

/**
 * Ghost Delay v3.0 — Valhalla-style Reverb + Enigma-style Modulation
 *
 * Signal chain:
 *   Input → FDN Reverb (SIZE, DECAY, TONE, DIFF) →
 *   Modulated Comb Filter / Notch Sweep (RATE, DEPTH, NOTCH) →
 *   Mix with dry → Output
 *
 * Top row (REVERB):
 *   SIZE  — Room size (allpass delay lengths)
 *   DECAY — Tail length (feedback gain, independent of size)
 *   TONE  — Feedback loop low-pass (each repeat gets darker)
 *   DIFF  — Diffusion (allpass coefficient — smeared vs distinct)
 *
 * Bottom row (MODULATION):
 *   RATE  — LFO speed for comb/notch sweep
 *   DEPTH — How deep the comb notches cut
 *   NOTCH — Comb frequency / notch spacing
 *   MIX   — Dry/wet
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

    // ── Parameter setters (mapped from APVTS) ───────────────
    void setTime(float v)     { size = v; }       // "time" param ID → SIZE
    void setFeedback(float v) { decay = v; }      // "feedback" → DECAY
    void setDecay(float v)    { fdnTone = v; }    // "decay" → TONE
    void setTone(float v)     { diff = v; }       // "tone" → DIFF
    void setRate(float v)     { rate = v; }       // "rate" → RATE
    void setDepth(float v)    { depth = v; }      // "depth" → DEPTH
    void setSpread(float v)   { notch = v; }      // "spread" → NOTCH
    void setMix(float v)      { mix = v; }        // "mix" → MIX

    // ── UI queries ──────────────────────────────────────────
    float getSweepPosition() const  { return sweepPos.load(); }
    float getSweepFrequency() const { return sweepFreq.load(); }
    float getCurrentRMSLevel() const { return rmsLevel.load(); }

private:
    // ═══════════════════════════════════════════════════════════
    // FDN REVERB (4-channel feedback delay network)
    // ═══════════════════════════════════════════════════════════

    static constexpr int FDN_SIZE = 4;
    static constexpr int MAX_FDN_DELAY = 8192;

    // Delay lines for FDN
    struct FDNDelay
    {
        std::array<float, MAX_FDN_DELAY> buffer {};
        int writePos = 0;
        int length = 1000;

        void clear() { buffer.fill(0.0f); writePos = 0; }

        void write(float sample)
        {
            buffer[writePos] = sample;
            writePos = (writePos + 1) % length;
        }

        float read() const
        {
            return buffer[writePos]; // read from write position = full delay
        }

        float readAt(float delaySamples) const
        {
            float readPos = (float)writePos - delaySamples;
            if (readPos < 0.0f) readPos += (float)length;
            int idx0 = (int)readPos;
            int idx1 = (idx0 + 1) % length;
            float frac = readPos - (float)idx0;
            return buffer[idx0 % length] * (1.0f - frac) + buffer[idx1] * frac;
        }
    };

    // 4 delay lines per channel (L/R)
    std::array<FDNDelay, FDN_SIZE> fdnL;
    std::array<FDNDelay, FDN_SIZE> fdnR;

    // Allpass diffusers (input diffusion, Valhalla-style)
    struct Allpass
    {
        std::array<float, 4096> buffer {};
        int writePos = 0;
        int length = 500;

        void clear() { buffer.fill(0.0f); writePos = 0; }

        float process(float input, float coeff)
        {
            float delayed = buffer[writePos];
            float feedfwd = input * (-coeff) + delayed;
            buffer[writePos] = input + delayed * coeff;
            writePos = (writePos + 1) % length;
            return feedfwd;
        }
    };

    // 2 input diffusers per channel
    std::array<Allpass, 2> diffuserL;
    std::array<Allpass, 2> diffuserR;

    // One-pole low-pass in FDN feedback loop (per delay line, per channel)
    struct OnePoleLP
    {
        float state = 0.0f;
        float process(float input, float coeff)
        {
            state += coeff * (input - state);
            return state;
        }
        void reset() { state = 0.0f; }
    };

    std::array<OnePoleLP, FDN_SIZE> fdnLPL;
    std::array<OnePoleLP, FDN_SIZE> fdnLPR;

    // Modulated allpass inside FDN (for lush detuning)
    struct ModAllpass
    {
        std::array<float, 4096> buffer {};
        int writePos = 0;
        int length = 300;
        float phase = 0.0f;

        void clear() { buffer.fill(0.0f); writePos = 0; phase = 0.0f; }

        float process(float input, float coeff, float modDepth, float modRate, double sr)
        {
            // Modulated read position
            phase += (float)(modRate / sr);
            if (phase >= 1.0f) phase -= 1.0f;
            float modOffset = modDepth * std::sin(phase * juce::MathConstants<float>::twoPi);

            float readDelay = (float)length + modOffset;
            float readPos = (float)writePos - readDelay;
            if (readPos < 0.0f) readPos += (float)buffer.size();

            int idx0 = (int)readPos;
            int idx1 = (idx0 + 1) % (int)buffer.size();
            float frac = readPos - (float)idx0;
            float delayed = buffer[idx0 % buffer.size()] * (1.0f - frac)
                          + buffer[idx1] * frac;

            float feedfwd = input * (-coeff) + delayed;
            buffer[writePos] = input + delayed * coeff;
            writePos = (writePos + 1) % (int)buffer.size();
            return feedfwd;
        }
    };

    // One mod-allpass per FDN channel
    std::array<ModAllpass, FDN_SIZE> modAPL;
    std::array<ModAllpass, FDN_SIZE> modAPR;

    // ═══════════════════════════════════════════════════════════
    // ENIGMA-STYLE MODULATED COMB FILTER
    // ═══════════════════════════════════════════════════════════

    static constexpr int MAX_COMB_DELAY = 4096;
    struct CombFilter
    {
        std::array<float, MAX_COMB_DELAY> buffer {};
        int writePos = 0;

        void clear() { buffer.fill(0.0f); writePos = 0; }

        float process(float input, float delaySamples, float feedback)
        {
            // Read with interpolation
            float readPos = (float)writePos - delaySamples;
            if (readPos < 0.0f) readPos += (float)MAX_COMB_DELAY;
            int idx0 = (int)readPos;
            int idx1 = (idx0 + 1) % MAX_COMB_DELAY;
            float frac = readPos - (float)idx0;
            float delayed = buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;

            // Write with feedback
            buffer[writePos] = input + delayed * feedback;
            writePos = (writePos + 1) % MAX_COMB_DELAY;

            // Output: notch = input - delayed, comb = input + delayed
            // Blend based on depth: 0 = passthrough, 1 = full comb effect
            return delayed;
        }
    };

    CombFilter combL, combR;

    // ═══════════════════════════════════════════════════════════
    // LFO for modulation section
    // ═══════════════════════════════════════════════════════════
    double lfoPhase = 0.0;

    // DC-blocking
    float hpStateL = 0.0f, hpStateR = 0.0f, hpCoeff = 0.0f;

    // Pre-allocated wet buffer
    juce::AudioBuffer<float> wetBuffer;

    // ── Parameters (0–1) ────────────────────────────────────
    float size   = 0.5f;   // Room size
    float decay  = 0.5f;   // Tail length
    float fdnTone = 0.5f;  // Feedback LP
    float diff   = 0.7f;   // Diffusion
    float rate   = 0.3f;   // Mod LFO speed
    float depth  = 0.5f;   // Mod depth
    float notch  = 0.5f;   // Comb frequency
    float mix    = 0.5f;   // Dry/wet

    // Host transport
    double hostBPM = 120.0;
    bool hostPlaying = false;
    double ppqPosition = 0.0;
    bool hasPPQ = false;

    double sampleRate = 44100.0;

    // UI feedback
    std::atomic<float> sweepPos  { 0.0f };
    std::atomic<float> sweepFreq { 200.0f };
    std::atomic<float> rmsLevel  { 0.0f };

    // ── Helpers ─────────────────────────────────────────────
    void updateFDNDelayLengths();

    // Base delay times (in samples at 44.1kHz) — mutually prime for maximal density
    static constexpr int baseLengths[FDN_SIZE] = { 1087, 1283, 1543, 1823 };
    // Diffuser lengths
    static constexpr int diffLengths[2] = { 142, 379 };
    // Mod allpass lengths
    static constexpr int modAPLengths[FDN_SIZE] = { 251, 331, 409, 503 };
};
