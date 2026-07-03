#include "../Source/GhostEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdio>
#include <cmath>

int main()
{
    int fails = 0;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double sr : rates)
    {
        GhostEngine eng;
        eng.prepare(sr, 512);
        eng.setAmount(1.0f); eng.setRate(1.0f); eng.setFilter(0.5f); eng.setMix(0.5f);
        eng.setCrush(1.0f); eng.setNoise(0.8f); eng.setWidth(1.0f); eng.setDrive(1.0f);

        juce::AudioBuffer<float> buf(2, 512);
        float peak = 0.0f; double energy = 0.0; bool bad = false;
        const float tp = juce::MathConstants<float>::twoPi;
        for (int blk = 0; blk < 400; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                float t = (float) ((blk * 512 + i)) / (float) sr;
                float s = 0.5f * std::sin(tp * 220.0f * t);
                buf.setSample(0, i, s); buf.setSample(1, i, s);
            }
            eng.process(buf, nullptr);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                {
                    float v = buf.getSample(c, i);
                    if (! std::isfinite(v)) bad = true;
                    peak = juce::jmax(peak, std::fabs(v));
                    energy += (double) v * v;
                }
        }
        bool silent = energy < 1e-9, explode = peak > 8.0f;
        printf("sr=%.0f  peak=%.3f  energy=%.1f  finite=%s  silent=%s  explode=%s\n",
               sr, peak, energy, bad ? "NO" : "yes", silent ? "YES" : "no", explode ? "YES" : "no");
        if (bad || silent || explode) ++fails;
    }
    printf(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL(%d)\n", fails);
    return fails;
}
