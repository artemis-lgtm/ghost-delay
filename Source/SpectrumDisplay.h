#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
 * Real-time spectral analyzer display for the top screen.
 * Draws teal frequency bars that react to the spectral delay output.
 * Shows detected musical key when pitch detection is active.
 * Positioned at exact Blender-projected coordinates.
 */
class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    SpectrumDisplay();
    ~SpectrumDisplay() override;

    void paint(juce::Graphics& g) override;

    // Feed spectrum data (called from editor's timer)
    void updateSpectrum(const float* data, int numBins);

    // Feed detected key info for display
    void setDetectedKey(int rootNote, bool isMinor, float confidence);

private:
    void timerCallback() override;

    static constexpr int NUM_BARS = 48;  // number of frequency bars
    std::array<float, 48> barValues {};   // current bar heights (0-1)
    std::array<float, 48> barPeaks {};    // peak hold values
    std::array<float, 48> barTargets {};  // target values (smoothing)

    // Key display
    int displayRootNote = -1;
    bool displayIsMinor = false;
    float displayConfidence = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
