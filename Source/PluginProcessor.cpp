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

    // Bottom row — Spectral Freeze
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rate", 1), "FREEZE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // default off

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("depth", 1), "DRIFT",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));   // subtle drift

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("spread", 1), "SCATTER",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // default off

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "DEPTH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));   // 50% freeze blend

    return { params.begin(), params.end() };
}

bool GhostDelayProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Accept mono->stereo, stereo->stereo, or mono->mono
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo()
        && mainOut != juce::AudioChannelSet::mono())
        return false;

    if (mainIn != juce::AudioChannelSet::stereo()
        && mainIn != juce::AudioChannelSet::mono())
        return false;

    return true;
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

    // FORCE STEREO: If input is mono (ch1 silent or missing), duplicate ch0 to ch1
    // This fixes the #1 user-reported bug: mono sources producing left-only dry audio
    if (buffer.getNumChannels() >= 2)
    {
        const float* ch0 = buffer.getReadPointer(0);
        const float* ch1 = buffer.getReadPointer(1);
        float ch1Energy = 0.0f;
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i)
            ch1Energy += ch1[i] * ch1[i];
        // If ch1 has less than -96dB of energy relative to being "empty", copy ch0
        if (ch1Energy < 1e-10f)
            buffer.copyFrom(1, 0, buffer, 0, 0, n);
    }

    // Feed active parameters to engine
    engine.setTime(*apvts.getRawParameterValue("time"));
    engine.setFeedback(*apvts.getRawParameterValue("feedback"));
    engine.setDecay(*apvts.getRawParameterValue("decay"));
    engine.setTone(*apvts.getRawParameterValue("tone"));

    // Bottom row — Spectral Freeze
    engine.setRate(*apvts.getRawParameterValue("rate"));       // FREEZE
    engine.setDepth(*apvts.getRawParameterValue("depth"));     // DRIFT
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
