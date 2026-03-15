#include "PluginEditor.h"
#include "BinaryData.h"

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Load Blender-rendered background
    background = juce::ImageCache::getFromMemory(
        BinaryData::background_png, BinaryData::background_pngSize);

    // Create filmstrip knobs
    auto makeKnob = [this](const char* name, const void* data, size_t size)
    {
        auto k = std::make_unique<FilmstripKnob>(name, data, size, 128);
        addAndMakeVisible(k.get());
        return k;
    };

    // Top row: SIZE, DECAY, TONE, MIX
    knobSize  = makeKnob("SIZE",  BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobDecay = makeKnob("DECAY", BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobTone  = makeKnob("TONE",  BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobMix   = makeKnob("MIX",   BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);

    // Attach to APVTS (top row)
    auto& apvts = processor.getAPVTS();
    attSize  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "time",     *knobSize);
    attDecay = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "feedback", *knobDecay);
    attTone  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "decay",    *knobTone);
    attMix   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "tone",     *knobMix);

    // Bypass button bounds (to the left of top row knobs)
    bypassBounds = { 12, 163, 38, 38 };

    // Ghost renderer
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    addAndMakeVisible(ghostRenderer);

    // Sweep display
    addAndMakeVisible(spectrumDisplay);

    startTimerHz(30);
    setSize(471, 596);
}

GhostDelayEditor::~GhostDelayEditor()
{
    stopTimer();
}

void GhostDelayEditor::timerCallback()
{
    ghostRenderer.setAudioLevel(processor.getCurrentRMSLevel());
    spectrumDisplay.setSweepPosition(processor.getSweepPosition());
    spectrumDisplay.setSweepFrequency(processor.getSweepFrequency());
    spectrumDisplay.setAudioLevel(processor.getCurrentRMSLevel());

    // Check bypass state for repaint
    bool active = *processor.getAPVTS().getRawParameterValue("reverb") > 0.5f;
    if (active != lastBypassState)
    {
        lastBypassState = active;
        repaint(bypassBounds);
    }

    // Self-capture trigger
    juce::File trigger("/tmp/ghost_capture_trigger");
    if (trigger.existsAsFile())
    {
        trigger.deleteFile();
        SelfCapture::capture(this);
    }
}

void GhostDelayEditor::mouseDown(const juce::MouseEvent& event)
{
    if (bypassBounds.contains(event.getPosition()))
    {
        auto* param = processor.getAPVTS().getParameter("reverb");
        float current = param->getValue();
        param->setValueNotifyingHost(current > 0.5f ? 0.0f : 1.0f);
        repaint(bypassBounds);
    }
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Background
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // Ghost area (animated overlay)
    g.setColour(juce::Colour(0x1a, 0x4a, 0x3a));
    g.fillRect(69, 389, 331, 99);

    // ── Bypass button ───────────────────────────────────────
    bool active = *processor.getAPVTS().getRawParameterValue("reverb") > 0.5f;
    auto teal = juce::Colour(0x0d, 0x94, 0x88);
    auto btnF = bypassBounds.toFloat();

    if (active)
    {
        // Glow halo
        g.setColour(teal.withAlpha(0.25f));
        g.fillRoundedRectangle(btnF.expanded(3), 10.0f);
        // Button fill
        g.setColour(teal);
        g.fillRoundedRectangle(btnF, 8.0f);
        // Label
        g.setColour(juce::Colours::white);
    }
    else
    {
        // Dim button
        g.setColour(juce::Colour(0x28, 0x28, 0x28));
        g.fillRoundedRectangle(btnF, 8.0f);
        g.setColour(juce::Colour(0x44, 0x44, 0x44));
        g.drawRoundedRectangle(btnF, 8.0f, 1.0f);
        // Dim label
        g.setColour(juce::Colour(0x55, 0x55, 0x55));
    }
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("REV", bypassBounds, juce::Justification::centred);

    // ── Knob labels (top row only) ──────────────────────────
    g.setColour(juce::Colour(0xcc, 0xcc, 0xdd));
    g.setFont(juce::FontOptions(11.0f));

    int labelY = 221;
    g.drawText("SIZE",   95 - 40, labelY, 80, 14, juce::Justification::centred);
    g.drawText("DECAY", 188 - 40, labelY, 80, 14, juce::Justification::centred);
    g.drawText("TONE",  281 - 40, labelY, 80, 14, juce::Justification::centred);
    g.drawText("MIX",   374 - 40, labelY, 80, 14, juce::Justification::centred);

    // No bottom row labels
}

void GhostDelayEditor::resized()
{
    int ks = 80;
    int hk = ks / 2;

    // Top row: SIZE, DECAY, TONE, MIX
    knobSize  ->setBounds( 95 - hk, 177 - hk, ks, ks);
    knobDecay ->setBounds(188 - hk, 177 - hk, ks, ks);
    knobTone  ->setBounds(281 - hk, 177 - hk, ks, ks);
    knobMix   ->setBounds(374 - hk, 177 - hk, ks, ks);

    // Ghost renderer
    ghostRenderer.setBounds(67, 387, 335, 103);
    ghostRenderer.setSpriteOffset(67, 387);

    // Sweep display
    spectrumDisplay.setBounds(108, 76, 254, 35);
}
