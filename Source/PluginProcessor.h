#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
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

    const juce::String getName() const override { return "Ghost Machine"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // For UI
    float getCurrentRMSLevel() const { return engine.getCurrentRMSLevel(); }

    // Live waveform tap for the top LED screen: lock-free ring of chunk
    // peaks (post-engine, so the screen shows what's actually coming out).
    static constexpr int scopeSize = 256;
    float getScopeSample(int i) const
    {
        return scope[(size_t) (i & (scopeSize - 1))].load(std::memory_order_relaxed);
    }
    int getScopeWritePos() const { return scopeWrite.load(std::memory_order_relaxed); }

    // Bypass
    bool isBypassed() const { return bypassed.load(); }
    void setBypassed(bool b) { bypassed.store(b); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GhostEngine engine;
    std::atomic<bool> bypassed { false };
    int stereoHoldBlocks = 0;   // force-stereo hysteresis counter

    // Scope ring buffer (audio thread writes, UI thread reads)
    std::array<std::atomic<float>, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    int scopeChunk = 256;       // samples per scope bin (set in prepareToPlay)
    int scopeAccumCount = 0;
    float scopeAccumPeak = 0.0f;

    int currentProgram = 0;     // selected factory preset index (Austin 6/14)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostDelayProcessor)
};
