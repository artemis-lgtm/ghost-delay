#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GhostEngine.h"

class GhostDelayProcessor : public juce::AudioProcessor
{
public:
    GhostDelayProcessor();
    ~GhostDelayProcessor() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ghost Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // For UI
    float getCurrentRMSLevel() const { return engine.getCurrentRMSLevel(); }
    float getSweepPosition() const   { return engine.getSweepPosition(); }
    float getSweepFrequency() const  { return engine.getSweepFrequency(); }

    // Bypass
    bool isBypassed() const { return bypassed.load(); }
    void setBypassed(bool b) { bypassed.store(b); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GhostEngine engine;
    std::atomic<bool> bypassed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayProcessor)
};
