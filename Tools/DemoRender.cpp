// Offline Haunted Love demo: dry loop, then the same loop with the engine's
// AMOUNT/RATE automated along a ghost-drag path across the pad.
// Output: stereo 48k WAV to the path given as argv[1].
#include "../Source/GhostEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double SR = 48000.0;
constexpr int BLOCK = 512;
constexpr float TP = juce::MathConstants<float>::twoPi;

// Soft keys voice: sine + gentle harmonics with attack/release envelope
float keyVoice(float t, float noteLen, float hz)
{
    if (t < 0.0f || t >= noteLen) return 0.0f;
    const float atk = 0.06f, rel = 0.8f;
    float env = juce::jmin(1.0f, t / atk);
    if (t > noteLen - rel)
        env *= juce::jmax(0.0f, (noteLen - t) / rel);
    const float ph = TP * hz * t;
    return env * (0.55f * std::sin(ph)
                + 0.22f * std::sin(2.0f * ph)
                + 0.08f * std::sin(3.0f * ph)
                + 0.05f * std::sin(4.02f * ph));
}

float bassVoice(float t, float noteLen, float hz)
{
    if (t < 0.0f || t >= noteLen) return 0.0f;
    float env = juce::jmin(1.0f, t / 0.01f) * std::exp(-1.8f * t / noteLen);
    const float ph = TP * hz * t;
    return env * (0.7f * std::sin(ph) + 0.12f * std::sin(2.0f * ph));
}

float pluck(float t, float hz)
{
    if (t < 0.0f) return 0.0f;
    float env = std::exp(-5.0f * t);
    if (env < 0.001f) return 0.0f;
    const float ph = TP * hz * t;
    return env * (0.5f * std::sin(ph) + 0.2f * std::sin(2.0f * ph + 0.4f));
}

float mtof(int m) { return 440.0f * std::pow(2.0f, (m - 69) / 12.0f); }

// 8-bar loop @ 84 BPM in C minor: Cm9 - Ab maj7 - Eb - Bb add9, 2 bars each
void renderLoop(juce::AudioBuffer<float>& out)
{
    const float bpm = 84.0f, beat = 60.0f / bpm, bar = 4.0f * beat;
    const float len = 8.0f * bar;
    const int n = (int) (len * SR);
    out.setSize(2, n);
    out.clear();

    struct Chord { float start; int notes[4]; int bass; };
    const Chord prog[] = {
        { 0 * bar, { 60, 63, 67, 74 }, 36 },   // Cm9
        { 2 * bar, { 56, 60, 63, 67 }, 44 },   // Abmaj7
        { 4 * bar, { 58, 63, 67, 70 }, 39 },   // Eb
        { 6 * bar, { 58, 62, 65, 72 }, 46 },   // Bb add9
    };
    // Sparse melody plucks (midi, beat offset)
    const std::pair<int, float> mel[] = {
        { 79, 1.0f }, { 75, 3.5f }, { 74, 6.0f }, { 72, 7.5f },
        { 75, 9.0f }, { 79, 11.5f }, { 82, 14.0f }, { 79, 15.5f },
        { 75, 17.0f }, { 74, 19.5f }, { 72, 22.0f }, { 70, 23.5f },
        { 72, 25.0f }, { 74, 27.5f }, { 75, 30.0f },
    };

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) SR;
        float l = 0.0f, r = 0.0f;
        for (const auto& c : prog)
        {
            const float lt = t - c.start;
            if (lt < 0.0f || lt >= 2.0f * bar + 0.8f) continue;
            for (int k = 0; k < 4; ++k)
            {
                const float v = 0.16f * keyVoice(lt, 2.0f * bar, mtof(c.notes[k]));
                // alternate notes lean L/R for width
                l += v * (k % 2 == 0 ? 1.0f : 0.7f);
                r += v * (k % 2 == 0 ? 0.7f : 1.0f);
            }
            const float b = 0.30f * bassVoice(lt, 2.0f * bar, mtof(c.bass));
            l += b; r += b;
        }
        for (const auto& m : mel)
        {
            const float v = 0.14f * pluck(t - m.second * beat, mtof(m.first));
            l += v; r += v;
        }
        out.setSample(0, i, l);
        out.setSample(1, i, r);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: DemoRender out.wav\n"); return 1; }

    juce::AudioBuffer<float> loop;
    renderLoop(loop);
    const int loopLen = loop.getNumSamples();

    GhostEngine eng;
    eng.prepare(SR, BLOCK);
    eng.setFilter(0.6f);
    eng.setMix(0.9f);
    eng.setCrush(0.25f);
    eng.setNoise(0.35f);
    eng.setWidth(0.8f);
    eng.setDrive(0.45f);

    // Ghost-drag path over the wet pass (normalized pad coords, y=0 top).
    // Subtle start -> drag right (deep slow warble) -> drag up (fast flutter)
    // -> settle back toward the middle.
    const struct { float at, x, y; } path[] = {
        { 0.00f, 0.12f, 0.80f },
        { 0.25f, 0.85f, 0.80f },
        { 0.45f, 0.85f, 0.12f },
        { 0.65f, 0.30f, 0.25f },
        { 0.85f, 0.55f, 0.55f },
        { 1.00f, 0.55f, 0.55f },
    };
    auto padAt = [&](float f, float& x, float& y)
    {
        for (size_t k = 1; k < std::size(path); ++k)
            if (f <= path[k].at)
            {
                const float u = (f - path[k-1].at) / (path[k].at - path[k-1].at);
                x = path[k-1].x + u * (path[k].x - path[k-1].x);
                y = path[k-1].y + u * (path[k].y - path[k-1].y);
                return;
            }
        x = path[std::size(path)-1].x; y = path[std::size(path)-1].y;
    };

    const int gap = (int) (1.0 * SR);
    const int wetLen = 2 * loopLen;               // loop twice under automation
    juce::AudioBuffer<float> outBuf(2, loopLen + gap + wetLen);
    outBuf.clear();

    // Part 1: dry
    for (int c = 0; c < 2; ++c)
        outBuf.copyFrom(c, 0, loop, c, 0, loopLen);

    // Part 2: wet, ghost tracked around the pad
    juce::AudioBuffer<float> blk(2, BLOCK);
    for (int pos = 0; pos < wetLen; pos += BLOCK)
    {
        const int nsm = juce::jmin(BLOCK, wetLen - pos);
        const float f = (float) pos / (float) wetLen;
        float gx, gy;
        padAt(f, gx, gy);
        eng.setAmount(gx);
        eng.setRate(1.0f - gy);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < nsm; ++i)
                blk.setSample(c, i, loop.getSample(c, (pos + i) % loopLen));
        blk.setSize(2, nsm, true, false, true);
        eng.process(blk, nullptr);
        for (int c = 0; c < 2; ++c)
            outBuf.copyFrom(c, loopLen + gap + pos, blk, c, 0, nsm);
        blk.setSize(2, BLOCK, false, false, true);
    }

    // Gentle safety ceiling
    for (int c = 0; c < 2; ++c)
    {
        auto* d = outBuf.getWritePointer(c);
        for (int i = 0; i < outBuf.getNumSamples(); ++i)
            d[i] = std::tanh(d[i] * 1.1f) * 0.9f;
    }

    juce::File outFile { juce::String(argv[1]) };
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(
        new juce::FileOutputStream(outFile), SR, 2, 24, {}, 0));
    if (w == nullptr) { std::fprintf(stderr, "writer fail\n"); return 2; }
    w->writeFromAudioSampleBuffer(outBuf, 0, outBuf.getNumSamples());
    w.reset();
    std::printf("wrote %s (%.1f s)\n", argv[1], outBuf.getNumSamples() / SR);
    return 0;
}
