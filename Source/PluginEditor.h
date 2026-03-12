#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "FilmstripKnob.h"
#include "GhostRenderer.h"
#include "SpectrumDisplay.h"

class GhostDelayEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit GhostDelayEditor(GhostDelayProcessor&);
    ~GhostDelayEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    GhostDelayProcessor& processor;
    juce::Image backgroundImage;

    std::unique_ptr<FilmstripKnob> knobSpread, knobDir, knobTime, knobFdbk;
    std::unique_ptr<FilmstripKnob> knobEnv, knobFreeze, knobTilt, knobMix;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attSpread, attDir, attTime, attFdbk,
        attEnv, attFreeze, attTilt, attMix;

    GhostRenderer ghostRenderer;
    SpectrumDisplay spectrumTop;  // LED screen at top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
