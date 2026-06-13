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

    // Top row: SIZE, DECAY, TONE, MIX
    std::unique_ptr<FilmstripKnob> knobSize, knobDecay, knobTone, knobMix;

    // Bottom row: SHIMMER, DUCK, WIDTH, GRIT
    std::unique_ptr<FilmstripKnob> knobShimmer, knobDuck, knobWidth, knobGrit;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attSize, attDecay, attTone, attMix,
        attShimmer, attDuck, attWidth, attGrit;

    // Ghost animation
    GhostRenderer ghostRenderer;

    // Live output waveform on the top LED strip (real audio tap)
    WaveDisplay waveDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayEditor)
};
