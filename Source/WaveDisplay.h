#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Live output waveform for the top LED strip. Reads the processor's
// lock-free scope ring at paint time; the editor's 30Hz timer repaints us.
// Oldest audio at the left, newest at the right; silence = flat center line.
class WaveDisplay : public juce::Component
{
public:
    explicit WaveDisplay(GhostDelayProcessor& p) : processor(p)
    {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // Opaque panel fill: covers the static wave baked into the faceplate
        // (sampled from the plate's analyzer window, ~RGB 15,80,66)
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0x12, 0x55, 0x46), b.getX(), b.getY(),
            juce::Colour(0x0c, 0x47, 0x3a), b.getX(), b.getBottom(), false));
        g.fillAll();
        const int N = GhostDelayProcessor::scopeSize;
        const int wpos = processor.getScopeWritePos();
        const float midY = b.getCentreY();
        const float half = b.getHeight() * 0.5f;
        const float bw = b.getWidth() / (float) N;

        const juce::Colour teal(0x5f, 0xe8, 0xd4);

        for (int i = 0; i < N; ++i)
        {
            // wpos is one past the newest sample -> wpos+i walks oldest->newest
            float v = processor.getScopeSample(wpos + i);
            v = juce::jlimit(0.0f, 1.0f, v * 1.4f);

            const float h = juce::jmax(0.6f, v * half * 0.95f);
            const float x = b.getX() + (float) i * bw;

            g.setColour(teal.withAlpha(0.30f + 0.70f * v));
            g.fillRect(x, midY - h, juce::jmax(0.5f, bw * 0.8f), h * 2.0f);
        }
    }

private:
    GhostDelayProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveDisplay)
};
