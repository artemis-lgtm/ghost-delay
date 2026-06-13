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

    // Bottom row — reverb-native effects (legacy param IDs kept for state compat)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rate", 1), "SHIMMER",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // octave-up tail regen

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("depth", 1), "DUCK",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // input ducks the wet

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("spread", 1), "WIDTH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.667f)); // M/S width 0..150%

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "GRIT",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // wet saturation + darken

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

    // ~5ms per scope bin regardless of sample rate -> full ring covers ~1.3s
    scopeChunk = juce::jmax(64, (int) (sampleRate * 0.005));
    scopeAccumCount = 0;
    scopeAccumPeak = 0.0f;
}

void GhostDelayProcessor::releaseResources()
{
    engine.reset();
}

void GhostDelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // FORCE STEREO with hysteresis: if input is mono (ch1 silent), duplicate
    // ch0 to ch1. Latched with a hold timer so the copy doesn't flicker on/off
    // block-to-block when ch1 hovers near the threshold (v6 click source #4).
    if (buffer.getNumChannels() >= 2)
    {
        const float* ch1 = buffer.getReadPointer(1);
        float ch1Energy = 0.0f;
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i)
            ch1Energy += ch1[i] * ch1[i];

        if (ch1Energy > 1e-8f)
            stereoHoldBlocks = 200;            // ~2s at 512/44.1k: stay stereo
        else if (stereoHoldBlocks > 0)
            --stereoHoldBlocks;

        if (stereoHoldBlocks == 0)
            buffer.copyFrom(1, 0, buffer, 0, 0, n);
    }

    // Top row
    engine.setSize(*apvts.getRawParameterValue("time"));
    engine.setDecay(*apvts.getRawParameterValue("feedback"));
    engine.setTone(*apvts.getRawParameterValue("decay"));
    engine.setMix(*apvts.getRawParameterValue("tone"));

    // Bottom row
    engine.setShimmer(*apvts.getRawParameterValue("rate"));
    engine.setDuck(*apvts.getRawParameterValue("depth"));
    engine.setWidth(*apvts.getRawParameterValue("spread"));
    engine.setGrit(*apvts.getRawParameterValue("mix"));

    engine.process(buffer, getPlayHead());

    // Feed the scope ring (post-engine, so the LED screen shows the output)
    {
        const int n = buffer.getNumSamples();
        const int nCh = buffer.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < nCh; ++ch)
                s = juce::jmax(s, std::abs(buffer.getSample(ch, i)));
            scopeAccumPeak = juce::jmax(scopeAccumPeak, s);
            if (++scopeAccumCount >= scopeChunk)
            {
                const int w = scopeWrite.load(std::memory_order_relaxed);
                scope[(size_t) (w & (scopeSize - 1))].store(scopeAccumPeak,
                                                            std::memory_order_relaxed);
                scopeWrite.store(w + 1, std::memory_order_relaxed);
                scopeAccumCount = 0;
                scopeAccumPeak = 0.0f;
            }
        }
    }
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
