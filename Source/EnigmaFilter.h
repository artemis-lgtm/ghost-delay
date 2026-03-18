#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>

/**
 * EnigmaFilter -- 12-stage cascaded allpass phaser with feedback
 *
 * Enigma-style lush modulated filter sweep. LFO sweeps a cascade
 * of first-order allpass filters, creating moving harmonic notches.
 *
 * Controls: DEPTH (sweep width), FEEDBACK (resonance), RATE (LFO speed)
 * Mono instance -- create two with phase offset for stereo.
 */
class EnigmaFilter
{
public:
    static constexpr int NUM_STAGES = 12;

    EnigmaFilter() = default;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& s : ap)
            s = 0.0f;
        feedbackState = 0.0f;
        lfoPhase = 0.0;
    }

    void setPhaseOffset(double offset) { phaseOffset = offset; }

    float processSample(float input, float depth, float feedback,
                        float rate, double hostBPM, bool hostPlaying)
    {
        // -- LFO --
        double lfoHz;
        if (hostBPM > 20.0 && hostPlaying)
        {
            // Tempo-synced: 0=8bars, 0.25=2bars, 0.5=1bar, 0.75=half, 1=quarter
            double beats = 32.0 * std::pow(0.03125, (double)rate);
            lfoHz = hostBPM / (60.0 * std::max(beats, 0.25));
        }
        else
        {
            lfoHz = 0.05 * std::pow(160.0, (double)rate);  // 0.05-8 Hz
        }

        lfoPhase += lfoHz / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;

        double phase = lfoPhase + phaseOffset;
        if (phase >= 1.0) phase -= 1.0;

        // Sine LFO -> 0-1
        float lfo = (float)(std::sin(phase * juce::MathConstants<double>::twoPi) + 1.0) * 0.5f;

        // -- Sweep frequency (exponential) --
        // depth=0: narrow (400-600 Hz), depth=1: full (100-4000 Hz)
        float minFreq = 500.0f - depth * 400.0f;
        float maxFreq = 500.0f + depth * 3500.0f;
        float ratio = maxFreq / std::max(minFreq, 1.0f);
        float centerFreq = minFreq * std::pow(ratio, lfo);

        // -- Allpass coefficient --
        float w = juce::MathConstants<float>::pi * std::min(centerFreq, (float)(sr * 0.45)) / (float)sr;
        float a = (1.0f - std::tan(w)) / (1.0f + std::tan(w));

        // -- Inject feedback (clamped at 0.92) --
        float fb = std::clamp(feedback * 0.92f, 0.0f, 0.92f);
        float x = std::tanh(input + feedbackState * fb);

        // -- 12-stage allpass cascade --
        // First-order allpass: y[n] = -a*x[n] + z[n]; z[n+1] = x[n] + a*y[n]
        for (int i = 0; i < NUM_STAGES; ++i)
        {
            float y = -a * x + ap[i];
            ap[i] = x + a * y;
            x = y;
        }

        feedbackState = x;
        return x;
    }

private:
    std::array<float, NUM_STAGES> ap {};   // allpass delay states
    float feedbackState = 0.0f;
    double lfoPhase = 0.0;
    double phaseOffset = 0.0;
    double sr = 44100.0;
};
