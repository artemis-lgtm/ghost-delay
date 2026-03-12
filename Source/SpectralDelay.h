#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <vector>

/**
 * AGGRESSIVE spectral delay engine — FFT-based with per-bin delay, self-oscillating
 * feedback, spectral gating, frequency mirroring, cross-bin smearing, and phase scattering.
 */
class SpectralDelay
{
public:
    SpectralDelay();
    ~SpectralDelay() = default;

    void prepare(double sampleRate, int blockSize);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

    void setTime(float v)     { time = v; }
    void setFeedback(float v) { feedback = v; }
    void setMix(float v)      { mix = v; }
    void setFreeze(float v)   { freeze = v; }      // Continuous 0-1 crossfade
    void setTilt(float v)     { tilt = v; }
    void setSpread(float v)   { spread = v; }
    void setDirection(float v){ direction = v; }
    void setEnvelope(float v) { envelope = v; }

    void getSpectrum(float* dest, int numBins) const;

private:
    static constexpr int FFT_ORDER = 10;
    static constexpr int FFT_SIZE = 1 << FFT_ORDER;       // 1024
    static constexpr int HOP_SIZE = FFT_SIZE / 4;          // 256
    static constexpr int NUM_BINS = FFT_SIZE / 2 + 1;      // 513
    static constexpr int MAX_DELAY_FRAMES = 512;            // ~3s of spectral delay

    juce::dsp::FFT fft { FFT_ORDER };
    juce::dsp::WindowingFunction<float> window { FFT_SIZE,
        juce::dsp::WindowingFunction<float>::hann };

    struct ChannelState
    {
        std::vector<float> inputBuffer;
        std::vector<float> outputBuffer;
        int inputWritePos = 0;
        int outputWritePos = 0;

        std::vector<std::array<std::complex<float>, 513>> delayLine;
        int delayWritePos = 0;

        std::array<std::complex<float>, 513> frozenFrame {};
        std::array<float, 513> envelopeFollower {};
    };

    std::array<ChannelState, 2> channels;
    std::array<float, 2048> fftWorkspace {};       // 2 * FFT_SIZE for JUCE FFT
    std::array<float, 513>  currentSpectrum {};

    double sampleRate = 44100.0;
    int samplesUntilNextHop = 0;
    int hopCount = 0;

    // Fixed random phase offsets per bin (for spread chaos)
    std::array<float, 513> binPhaseOffsets {};

    // Params
    float time      = 0.3f;
    float feedback   = 0.4f;
    float mix        = 0.5f;
    float freeze     = 0.0f;
    float tilt       = 0.5f;
    float spread     = 0.0f;
    float direction  = 1.0f;
    float envelope   = 0.5f;

    void processHop(int channel);
};
