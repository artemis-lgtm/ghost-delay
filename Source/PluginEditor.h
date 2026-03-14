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

    // 8 filmstrip knobs — v2.0 layout
    std::unique_ptr<FilmstripKnob> knobTime, knobFdbk, knobDecay, knobTone;
    std::unique_ptr<FilmstripKnob> knobRate, knobDepth, knobSpread, knobMix;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attTime, attFdbk, attDecay, attTone, attRate, attDepth, attSpread, attMix;

    // Ghost animation
    GhostRenderer ghostRenderer;

    // Sweep display (replaces old spectrum analyzer)
    SpectrumDisplay spectrumDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
