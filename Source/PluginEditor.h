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

private:
    void timerCallback() override;

    GhostDelayProcessor& processor;

    juce::Image background;
    // LED images removed (bypass handled by DAW)

    // 8 filmstrip knobs
    std::unique_ptr<FilmstripKnob> knobTime, knobFdbk, knobMix, knobFreeze;
    std::unique_ptr<FilmstripKnob> knobTilt, knobSpread, knobDir, knobEnv;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attTime, attFdbk, attMix, attFreeze, attTilt, attSpread, attDir, attEnv;

    // Ghost animation
    GhostRenderer ghostRenderer;

    // Spectral analyzer display
    SpectrumDisplay spectrumDisplay;

    // Bypass removed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
