#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

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
        juce::ParameterID("amount", 1), "AMOUNT",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rate", 1), "RATE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("filter", 1), "FILTER",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1), "MIX",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));

    // Bottom row — texture layer
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("crush", 1), "CRUSH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // bit crush + sample hold

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("noise", 1), "NOISE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f));   // tape hiss + hum bed

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("width", 1), "WIDTH",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.667f)); // M/S width 0..150%

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("drive", 1), "DRIVE",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));   // tape saturation drive

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
    engine.setAmount(*apvts.getRawParameterValue("amount"));
    engine.setRate(*apvts.getRawParameterValue("rate"));
    engine.setFilter(*apvts.getRawParameterValue("filter"));
    engine.setMix(*apvts.getRawParameterValue("mix"));

    // Bottom row
    engine.setCrush(*apvts.getRawParameterValue("crush"));
    engine.setNoise(*apvts.getRawParameterValue("noise"));
    engine.setWidth(*apvts.getRawParameterValue("width"));
    engine.setDrive(*apvts.getRawParameterValue("drive"));

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
    state.setProperty("program", currentProgram, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GhostDelayProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        currentProgram = (int) state.getProperty("program", 0);
        apvts.replaceState(state);
    }
}

// ---- factory presets (Austin 6/14) -------------------------------------
// Programs are loaded via the host's built-in preset selector; the plugin
// faceplate is unchanged. Defaults are restored before each preset applies
// its own overrides, so non-listed params snap back rather than carrying
// over from the previous selection.

int GhostDelayProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String GhostDelayProcessor::getProgramName(int index)
{
    const auto& presets = getFactoryPresets();
    if (index >= 0 && index < (int) presets.size())
        return presets[(size_t) index].name;
    return {};
}

void GhostDelayProcessor::setCurrentProgram(int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;

    // Hosts replay the saved program number after setStateInformation —
    // re-applying the factory preset would wipe the user's restored tweaks.
    if (index == currentProgram)
        return;

    currentProgram = index;

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            rp->setValueNotifyingHost(rp->getDefaultValue());

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter(id))
            rp->setValueNotifyingHost(rp->convertTo0to1(value));
}

juce::AudioProcessorEditor* GhostDelayProcessor::createEditor()
{
    return new GhostDelayEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GhostDelayProcessor();
}
