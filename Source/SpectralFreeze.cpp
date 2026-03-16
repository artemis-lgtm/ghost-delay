#include "SpectralFreeze.h"

static constexpr float PI     = 3.14159265358979f;
static constexpr float TWO_PI = 6.28318530717959f;

SpectralFreeze::SpectralFreeze()
{
    // Hann window
    for (int i = 0; i < FFT_SIZE; ++i)
        window[i] = 0.5f * (1.0f - std::cos(TWO_PI * (float)i / (float)FFT_SIZE));
}

void SpectralFreeze::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void SpectralFreeze::reset()
{
    std::fill(inputRing,   inputRing   + FFT_SIZE,       0.0f);
    std::fill(outputAccum, outputAccum + ACCUM_SIZE,     0.0f);
    std::fill(fftWork,     fftWork     + FFT_SIZE * 2,   0.0f);
    std::fill(storedMag,   storedMag   + NUM_BINS,       0.0f);
    std::fill(storedPhase, storedPhase + NUM_BINS,       0.0f);
    std::fill(driftPhase,  driftPhase  + NUM_BINS,       0.0f);

    inputWritePos  = 0;
    hopCounter     = 0;
    outputReadPos  = 0;
    outputWritePos = 0;

    freeze  = 0.0f;
    drift   = 0.0f;
    scatter = 0.0f;
}

float SpectralFreeze::processSample(float input)
{
    // Write to input ring buffer
    inputRing[inputWritePos] = input;
    inputWritePos = (inputWritePos + 1) % FFT_SIZE;

    // Count toward next hop
    hopCounter++;
    if (hopCounter >= HOP_SIZE)
    {
        hopCounter = 0;
        processFrame();
    }

    // Read from output accumulator
    float output = outputAccum[outputReadPos];
    outputAccum[outputReadPos] = 0.0f;  // Clear after reading (essential for OLA)
    outputReadPos = (outputReadPos + 1) % ACCUM_SIZE;

    return output;
}

void SpectralFreeze::processFrame()
{
    // ── Smooth parameters (per-frame, ~10ms at typical hop rate) ──
    const float sc = 0.15f;
    freeze  += (targetFreeze  - freeze)  * sc;
    drift   += (targetDrift   - drift)   * sc;
    scatter += (targetScatter - scatter) * sc;

    // ── 1. Fill FFT buffer with windowed input ──────────────────
    for (int i = 0; i < FFT_SIZE; ++i)
    {
        int readIdx = (inputWritePos + i) % FFT_SIZE;
        fftWork[i] = inputRing[readIdx] * window[i];
    }
    // Zero imaginary part
    for (int i = FFT_SIZE; i < FFT_SIZE * 2; ++i)
        fftWork[i] = 0.0f;

    // ── 2. Forward FFT ──────────────────────────────────────────
    fft.performRealOnlyForwardTransform(fftWork);

    // ── 3. Extract magnitude and phase ──────────────────────────
    float currentMag  [NUM_BINS];
    float currentPhase[NUM_BINS];

    for (int i = 0; i < NUM_BINS; ++i)
    {
        float re = fftWork[i * 2];
        float im = fftWork[i * 2 + 1];
        currentMag[i]   = std::sqrt(re * re + im * im);
        currentPhase[i] = std::atan2(im, re);
    }

    // ── 4. Update stored spectrum ───────────────────────────────
    // freeze=0: updateRate=1 → stored tracks input (pass-through)
    // freeze=1: updateRate=0 → stored holds indefinitely
    float updateRate = 1.0f - freeze;

    for (int i = 0; i < NUM_BINS; ++i)
    {
        storedMag[i] += (currentMag[i] - storedMag[i]) * updateRate;

        // Phase: track current when not frozen, hold when frozen
        if (updateRate > 0.05f)
            storedPhase[i] = currentPhase[i];
    }

    // ── 5. Build output spectrum ────────────────────────────────
    float outMag  [NUM_BINS];
    float outPhase[NUM_BINS];

    // Drift: per-bin magnitude modulation via slow LFOs
    // Creates breathing, evolving texture in the frozen spectrum
    float hopsPerSec = (float)sr / (float)HOP_SIZE;

    for (int i = 0; i < NUM_BINS; ++i)
    {
        // Each bin drifts at a unique rate (0.05–0.55 Hz range)
        float binRate = 0.05f + (float)i / (float)NUM_BINS * 0.5f;
        driftPhase[i] += binRate * TWO_PI * drift / hopsPerSec;
        if (driftPhase[i] > TWO_PI)
            driftPhase[i] -= TWO_PI;

        float mod = 1.0f + drift * 0.4f * std::sin(driftPhase[i]);
        outMag[i] = storedMag[i] * mod;
    }

    // Scatter: phase randomization (ghostly dissolution)
    for (int i = 0; i < NUM_BINS; ++i)
    {
        if (scatter > 0.01f)
        {
            float rndPhase = phaseDist(rng);
            outPhase[i] = storedPhase[i] * (1.0f - scatter) + rndPhase * scatter;
        }
        else
        {
            outPhase[i] = storedPhase[i];
        }
    }

    // ── 6. Reconstruct complex spectrum ─────────────────────────
    for (int i = 0; i < NUM_BINS; ++i)
    {
        fftWork[i * 2]     = outMag[i] * std::cos(outPhase[i]);
        fftWork[i * 2 + 1] = outMag[i] * std::sin(outPhase[i]);
    }

    // ── 7. Inverse FFT ─────────────────────────────────────────
    fft.performRealOnlyInverseTransform(fftWork);

    // ── 8. Synthesis window + overlap-add ───────────────────────
    for (int i = 0; i < FFT_SIZE; ++i)
    {
        int outIdx = (outputWritePos + i) % ACCUM_SIZE;
        outputAccum[outIdx] += fftWork[i] * window[i] * OUTPUT_SCALE;
    }

    outputWritePos = (outputWritePos + HOP_SIZE) % ACCUM_SIZE;
}
