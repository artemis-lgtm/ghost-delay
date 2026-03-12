#include "FilmstripKnob.h"

FilmstripKnob::FilmstripKnob(const juce::String& name,
                               const void* imageData, size_t imageSize,
                               int numFrames)
    : juce::Slider(name),
      frameCount(numFrames)
{
    filmstrip = juce::ImageCache::getFromMemory(imageData, static_cast<int>(imageSize));
    frameHeight = filmstrip.getHeight() / frameCount;

    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(0.0, 1.0, 0.001);
}

void FilmstripKnob::paint(juce::Graphics& g)
{
    if (filmstrip.isNull() || frameCount <= 0)
        return;

    // Map slider value (0-1) to frame index
    double val = (getValue() - getMinimum()) / (getMaximum() - getMinimum());
    int frameIdx = static_cast<int>(val * (frameCount - 1));
    frameIdx = juce::jlimit(0, frameCount - 1, frameIdx);

    int srcY = frameIdx * frameHeight;
    int srcW = filmstrip.getWidth();

    // Draw the frame scaled to the component bounds
    auto bounds = getLocalBounds();
    g.drawImage(filmstrip,
                bounds.getX(), bounds.getY(),
                bounds.getWidth(), bounds.getHeight(),
                0, srcY, srcW, frameHeight);
}
