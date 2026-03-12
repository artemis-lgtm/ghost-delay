#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Valhalla-inspired flat LookAndFeel for Ghost Delay.
 * Everything drawn programmatically — no images, no filmstrips.
 * Dark background, teal accents, clean sans-serif labels.
 */
class GhostLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GhostLookAndFeel()
    {
        // Color palette
        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, knobTrack);
        setColour(juce::Slider::thumbColourId, pointer);
        setColour(juce::Label::textColourId, textLight);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        auto radius = (float)juce::jmin(width, height) * 0.4f;
        auto centreX = (float)x + (float)width * 0.5f;
        auto centreY = (float)y + (float)height * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer track (dark ring)
        g.setColour(knobTrack);
        g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 2.0f);

        // Value arc (teal)
        juce::Path arcPath;
        arcPath.addCentredArc(centreX, centreY, radius, radius,
                              0.0f, rotaryStartAngle, angle, true);
        g.setColour(accent);
        g.strokePath(arcPath, juce::PathStrokeType(3.0f));

        // Center dot
        float dotSize = radius * 0.25f;
        g.setColour(knobFill);
        g.fillEllipse(centreX - dotSize, centreY - dotSize, dotSize * 2.0f, dotSize * 2.0f);

        // Pointer line (from center toward edge)
        float pointerLength = radius * 0.8f;
        float pointerX = centreX + pointerLength * std::cos(angle - juce::MathConstants<float>::halfPi);
        float pointerY = centreY + pointerLength * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(pointer);
        g.drawLine(centreX, centreY, pointerX, pointerY, 2.5f);
    }

    // Colors
    juce::Colour bgDark      { 0xff0d0d1a };   // Very dark blue-black
    juce::Colour bgPanel      { 0xff161625 };   // Panel sections
    juce::Colour bgSection    { 0xff1c1c30 };   // Section backgrounds
    juce::Colour accent       { 0xff00d4aa };   // Teal green
    juce::Colour accentDim    { 0xff007a60 };   // Dimmer teal
    juce::Colour pointer      { 0xffe0e0ff };   // White-blue pointer
    juce::Colour knobTrack    { 0xff333355 };   // Knob outer track
    juce::Colour knobFill     { 0xff222240 };   // Knob center
    juce::Colour textLight    { 0xffc8c8e0 };   // Label text
    juce::Colour textDim      { 0xff666688 };   // Dim text (section headers, units)
    juce::Colour textBright   { 0xffe8e8ff };   // Bright text (title)
    juce::Colour spectrumLow  { 0xff003344 };   // Spectrum dark
    juce::Colour spectrumHigh { 0xff00ffaa };   // Spectrum bright
};
