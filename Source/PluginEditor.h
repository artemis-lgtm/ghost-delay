#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "FilmstripKnob.h"
#include "GhostRenderer.h"
#include "SpectrumDisplay.h"
#include "SelfCapture.h"

class GhostDelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GhostDelayEditor(GhostDelayProcessor&);
    ~GhostDelayEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    GhostDelayProcessor& processor;
    juce::Image background;

    // Top row: SIZE, DECAY, TONE, MIX
    std::unique_ptr<FilmstripKnob> knobSize, knobDecay, knobTone, knobMix;

    // Bottom row: FREEZE, DRIFT, SCATTER, DEPTH
    std::unique_ptr<FilmstripKnob> knobFreeze, knobDrift, knobScatter, knobDepth;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attSize, attDecay, attTone, attMix,
        attFreeze, attDrift, attScatter, attDepth;

    // Ghost animation
    GhostRenderer ghostRenderer;

    // Sweep display
    SpectrumDisplay spectrumDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
