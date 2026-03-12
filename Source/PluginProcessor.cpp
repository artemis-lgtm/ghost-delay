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
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("feedback", 1), "FDBK",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "MIX",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("freeze", 1), "FREEZE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("tilt", 1), "TILT",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("spread", 1), "SPREAD",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("direction", 1), "DIR",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("envelope", 1), "ENV",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Key-aware harmonic filtering (no visible knob -- host automation only)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("keyAware", 1), "KEY",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

void GhostDelayProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    spectralDelay.prepare(sampleRate, samplesPerBlock);
}

void GhostDelayProcessor::releaseResources()
{
    spectralDelay.reset();
}

void GhostDelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassed.load())
        return;

    // Update DSP params from APVTS
    spectralDelay.setTime(*apvts.getRawParameterValue("time"));
    spectralDelay.setFeedback(*apvts.getRawParameterValue("feedback"));
    spectralDelay.setMix(*apvts.getRawParameterValue("mix"));
    spectralDelay.setFreeze(*apvts.getRawParameterValue("freeze"));
    spectralDelay.setTilt(*apvts.getRawParameterValue("tilt"));
    spectralDelay.setSpread(*apvts.getRawParameterValue("spread"));
    spectralDelay.setDirection(*apvts.getRawParameterValue("direction"));
    spectralDelay.setEnvelope(*apvts.getRawParameterValue("envelope"));
    spectralDelay.setKeyAware(*apvts.getRawParameterValue("keyAware"));

    spectralDelay.process(buffer);

    // Calculate RMS for UI
    float rms = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        rms += buffer.getRMSLevel(ch, 0, buffer.getNumSamples());
    rms /= static_cast<float>(buffer.getNumChannels());
    rmsLevel.store(rms);
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
