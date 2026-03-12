#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Filmstrip-based knob using Blender-rendered frames.
 * Vertical filmstrip PNG: N frames stacked, each frame = square.
 */
class FilmstripKnob : public juce::Slider
{
public:
    FilmstripKnob(const juce::String& name, const void* imageData, size_t imageSize, int numFrames);

    void paint(juce::Graphics& g) override;

private:
    juce::Image filmstrip;
    int frameCount;
    int frameHeight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilmstripKnob)
};
