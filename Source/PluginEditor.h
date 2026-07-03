#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "FilmstripKnob.h"
#include "GhostRenderer.h"
#include "WaveDisplay.h"
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

    // Face knobs (Haunted Love plate): MIX, RATE, FILTER, CRUSH.
    // amount/noise/width/drive have no on-face UI — host-automatable only.
    std::unique_ptr<FilmstripKnob> knobMix, knobRate, knobFilter, knobCrush;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attMix, attRate, attFilter, attCrush;

    // Preset selector over the baked plate box
    juce::ComboBox presetBox;

    // Ghost animation + XY pad (drag ghost: x=amount, y=rate)
    GhostRenderer ghostRenderer;
    juce::RangedAudioParameter* paramAmount = nullptr;
    juce::RangedAudioParameter* paramRate = nullptr;

    // Live output waveform on the top LED strip (real audio tap)
    WaveDisplay waveDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
