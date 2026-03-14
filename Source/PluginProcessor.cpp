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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("time", 1), "TIME",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("feedback", 1), "FDBK",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decay", 1), "DECAY",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("tone", 1), "TONE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rate", 1), "RATE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("depth", 1), "DEPTH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("spread", 1), "SPREAD",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "MIX",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

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

    if (bypassed.load())
        return;

    // Feed parameters from APVTS to engine
    engine.setTime(*apvts.getRawParameterValue("time"));
    engine.setFeedback(*apvts.getRawParameterValue("feedback"));
    engine.setDecay(*apvts.getRawParameterValue("decay"));
    engine.setTone(*apvts.getRawParameterValue("tone"));
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
