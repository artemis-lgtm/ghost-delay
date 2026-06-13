// Offline stress test for GhostEngine v7.0
// Verifies the two v6 bug classes are dead:
//   1. Clicks while sweeping SIZE (delay-line re-wrap bug)
//   2. Blowout at max DECAY (tanh-knee screech)
// Detection: max sample-to-sample delta in the wet signal (click proxy)
// and running peak (blowout proxy).

#include "../Source/GhostEngine.h"
#include <cstdio>
#include <cmath>

int main()
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    constexpr int nBlocksPerPhase = 750;   // ~8s per phase

    GhostEngine engine;
    engine.prepare(sr, block);

    juce::AudioBuffer<float> buf(2, block);

    float maxDelta = 0.0f, peak = 0.0f;
    float prevL = 0.0f;
    double sinePhase = 0.0;
    int sampleCount = 0;
    bool failed = false;

    struct Phase { const char* name; float mix; bool sweepSize; float decay; float shimmer; float grit; };
    Phase phases[] = {
        { "baseline (mix 50%, mid settings)",      0.5f, false, 0.4f, 0.0f, 0.0f },
        { "SIZE sweep full-range (click test)",    1.0f, true,  0.5f, 0.0f, 0.0f },
        { "max DECAY sustain (blowout test)",      1.0f, false, 1.0f, 0.0f, 0.0f },
        { "max DECAY + max SHIMMER (stability)",   1.0f, false, 1.0f, 1.0f, 0.0f },
        { "max everything (kitchen sink)",         1.0f, true,  1.0f, 1.0f, 1.0f },
    };

    for (auto& ph : phases)
    {
        float phaseMaxDelta = 0.0f, phasePeak = 0.0f;
        engine.setMix(ph.mix);
        engine.setDecay(ph.decay);
        engine.setShimmer(ph.shimmer);
        engine.setGrit(ph.grit);
        engine.setTone(0.7f);
        engine.setDuck(0.0f);
        engine.setWidth(0.667f);

        for (int b = 0; b < nBlocksPerPhase; ++b)
        {
            if (ph.sweepSize)
            {
                // sweep SIZE 0→1→0 over the phase, updated per block (worst case)
                float t = (float)b / (float)nBlocksPerPhase;
                engine.setSize(0.5f + 0.5f * std::sin(t * 6.28318f * 2.0f));
            }
            else
                engine.setSize(0.5f);

            // input: 220Hz sine bursts (200ms on, 200ms off)
            for (int i = 0; i < block; ++i)
            {
                bool on = ((sampleCount / 9600) % 2) == 0;
                float s = on ? 0.5f * (float)std::sin(sinePhase) : 0.0f;
                sinePhase += 2.0 * 3.14159265358979 * 220.0 / sr;
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
                ++sampleCount;
            }

            engine.process(buf, nullptr);

            for (int i = 0; i < block; ++i)
            {
                float L = buf.getSample(0, i);
                float R = buf.getSample(1, i);
                float d = std::fabs(L - prevL);
                prevL = L;
                if (d > phaseMaxDelta) phaseMaxDelta = d;
                float p = std::max(std::fabs(L), std::fabs(R));
                if (p > phasePeak) phasePeak = p;
                if (std::isnan(L) || std::isinf(L) || std::isnan(R) || std::isinf(R))
                {
                    std::printf("FAIL: NaN/Inf in phase '%s'\n", ph.name);
                    return 1;
                }
            }
        }

        // Thresholds: a click is a step >0.5 in one sample at these levels;
        // blowout is sustained peak >2.0 (output tanh should cap ~1.0).
        bool phaseFail = (phaseMaxDelta > 0.5f) || (phasePeak > 2.0f);
        std::printf("%-42s maxDelta=%.4f  peak=%.4f  %s\n",
                    ph.name, phaseMaxDelta, phasePeak, phaseFail ? "FAIL" : "PASS");
        if (phaseFail) failed = true;
        if (phaseMaxDelta > maxDelta) maxDelta = phaseMaxDelta;
        if (phasePeak > peak) peak = phasePeak;
    }

    std::printf("\nOverall: maxDelta=%.4f peak=%.4f -> %s\n",
                maxDelta, peak, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
