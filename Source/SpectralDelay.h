#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <vector>

/**
 * AGGRESSIVE spectral delay engine — FFT-based with per-bin delay, self-oscillating
 * feedback, spectral gating, frequency mirroring, cross-bin smearing, phase scattering,
 * and audio-based pitch detection with key-aware harmonic filtering.
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
    void setKeyAware(float v) { keyAware = v; }    // 0=off, 1=full key filtering

    void getSpectrum(float* dest, int numBins) const;

    // Pitch detection results for UI display
    struct KeyInfo {
        int rootNote = -1;          // MIDI note of detected root (0-11, -1 = none)
        bool isMinor = false;       // true = minor, false = major
        float confidence = 0.0f;    // 0-1 how confident the detection is
    };
    KeyInfo getDetectedKey() const { return detectedKey.load(); }

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
    float keyAware   = 0.0f;

    // ── Pitch detection (YIN + Krumhansl-Kessler) ────────────────
    static constexpr int YIN_BUFFER_SIZE = 2048;
    std::array<float, YIN_BUFFER_SIZE> yinBuffer {};
    int yinWritePos = 0;
    int yinSampleCount = 0;
    static constexpr int YIN_HOP = 512;             // Run pitch detection every 512 samples
    static constexpr float YIN_THRESHOLD = 0.15f;

    // Chroma accumulator (12 pitch classes, running average)
    std::array<float, 12> chromaAccum {};
    float chromaDecay = 0.92f;                       // Smooth over ~0.5s at typical hop rates

    // Key detection result (atomic for thread-safe UI reads)
    struct AtomicKeyInfo {
        std::atomic<int> rootNote { -1 };
        std::atomic<bool> isMinor { false };
        std::atomic<float> confidence { 0.0f };
        KeyInfo load() const { return { rootNote.load(), isMinor.load(), confidence.load() }; }
        void store(const KeyInfo& k) { rootNote.store(k.rootNote); isMinor.store(k.isMinor); confidence.store(k.confidence); }
    };
    AtomicKeyInfo detectedKey;

    // Key-aware bin mask: 1.0 = in-key, 0.15 = out-of-key (per bin)
    std::array<float, 513> keyMask {};

    void processHop(int channel);
    float detectPitchYIN(const float* buffer, int length) const;
    void updateChromaAndKey(float pitchHz);
    void rebuildKeyMask();

    // Krumhansl-Kessler key profiles (major and minor)
    static constexpr float majorProfile[12] = {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
        2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
    };
    static constexpr float minorProfile[12] = {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
        2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
    };
};
