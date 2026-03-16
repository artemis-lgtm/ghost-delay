#include "PluginProcessor.h"
#include "PluginEditor.h"

GhostDelayProcessor::GhostDelayProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

GhostDelayProcessor::~GhostDelayProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
GhostDelayProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Top row (active)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("time", 1), "SIZE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("feedback", 1), "DECAY",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decay", 1), "TONE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("tone", 1), "MIX",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));

    // Bottom row (reserved, inactive — kept for preset compatibility)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rate", 1), "RATE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("depth", 1), "DEPTH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("spread", 1), "SPREAD",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "MIX_GLOBAL",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Reverb bypass removed — MIX knob at 0 provides clean pass-through

    return { params.begin(), params.end() };
}

void GhostDelayProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);
}

void GhostDelayProcessor::releaseResources()
{
    engine.reset();
}

void GhostDelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Feed active parameters to engine
    engine.setTime(*apvts.getRawParameterValue("time"));
    engine.setFeedback(*apvts.getRawParameterValue("feedback"));
    engine.setDecay(*apvts.getRawParameterValue("decay"));
    engine.setTone(*apvts.getRawParameterValue("tone"));

    // Bottom row params not used but safe to call (stubs)
    engine.setRate(*apvts.getRawParameterValue("rate"));
    engine.setDepth(*apvts.getRawParameterValue("depth"));
    engine.setSpread(*apvts.getRawParameterValue("spread"));
    engine.setMix(*apvts.getRawParameterValue("mix"));

    engine.process(buffer, getPlayHead());
}

void GhostDelayProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GhostDelayProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* GhostDelayProcessor::createEditor()
{
    return new GhostDelayEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GhostDelayProcessor();
}
