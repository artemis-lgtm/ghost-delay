#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <random>

/**
 * SpectralFreeze — FFT-based spectral capture and hold
 *
 * Captures the frequency content of the input (reverb output) and
 * holds it indefinitely, creating ghostly sustained textures.
 *
 * FREEZE:  0 = pass-through, 1 = full spectral hold
 * DRIFT:   slow evolution of frozen spectrum (per-bin LFO modulation)
 * SCATTER: phase randomization (dissolves coherent spectrum into noise)
 *
 * Uses WOLA (Weighted Overlap-Add) with Hann window, 75% overlap.
 * FFT size 2048 (~46ms window at 44.1kHz, good frequency resolution).
 */
class SpectralFreeze
{
public:
    static constexpr int FFT_ORDER  = 11;
    static constexpr int FFT_SIZE   = 1 << FFT_ORDER;       // 2048
    static constexpr int HOP_SIZE   = FFT_SIZE / 4;         // 512
    static constexpr int NUM_BINS   = FFT_SIZE / 2 + 1;     // 1025
    static constexpr int ACCUM_SIZE = FFT_SIZE * 4;          // 8192 (safe overlap-add)

    SpectralFreeze();
    ~SpectralFreeze() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process one sample. Handles internal buffering and overlap-add. */
    float processSample(float input);

    void setFreeze(float f)  { targetFreeze = f; }
    void setDrift(float d)   { targetDrift = d; }
    void setScatter(float s) { targetScatter = s; }

private:
    void processFrame();

    juce::dsp::FFT fft { FFT_ORDER };

    // Hann analysis/synthesis window
    float window[FFT_SIZE] {};

    // Input circular buffer
    float inputRing[FFT_SIZE] {};
    int   inputWritePos = 0;
    int   hopCounter    = 0;

    // Output overlap-add accumulator (circular)
    float outputAccum[ACCUM_SIZE] {};
    int   outputReadPos  = 0;
    int   outputWritePos = 0;

    // FFT work buffer (JUCE needs 2*N for real-only transforms)
    float fftWork[FFT_SIZE * 2] {};

    // Stored (frozen) spectrum
    float storedMag  [NUM_BINS] {};
    float storedPhase[NUM_BINS] {};

    // Per-bin drift LFO phase accumulators
    float driftPhase[NUM_BINS] {};

    // Smoothed parameters
    float freeze = 0.0f, targetFreeze = 0.0f;
    float drift  = 0.0f, targetDrift  = 0.0f;
    float scatter = 0.0f, targetScatter = 0.0f;

    double sr = 44100.0;

    // Hann^2 COLA normalization for 75% overlap = 1/1.5
    static constexpr float OUTPUT_SCALE = 2.0f / 3.0f;

    // Phase randomizer for scatter
    std::mt19937 rng { 42 };
    std::uniform_real_distribution<float> phaseDist {
        -3.14159265f, 3.14159265f
    };
};
