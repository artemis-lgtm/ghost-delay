#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Sweep position display for the top screen.
 * Shows an animated glowing bar that sweeps left-to-right representing
 * the current bandpass filter position (60 Hz left → 900 Hz right).
 * Positioned at exact Blender-projected coordinates.
 */
class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    SpectrumDisplay();
    ~SpectrumDisplay() override;

    void paint(juce::Graphics& g) override;

    // Feed sweep data (called from editor's timer)
    void setSweepPosition(float pos);      // 0–1 (left to right)
    void setSweepFrequency(float freqHz);  // For text display
    void setAudioLevel(float level);       // For glow intensity

private:
    void timerCallback() override;

    float displayPos    = 0.0f;   // Smoothed sweep position
    float targetPos     = 0.0f;
    float displayFreq   = 200.0f;
    float displayLevel  = 0.0f;
    float trailPos      = 0.0f;   // Trailing glow

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
