#include "PluginEditor.h"
#include "BinaryData.h"

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Load Blender-rendered background
    background = juce::ImageCache::getFromMemory(
        BinaryData::background_png, BinaryData::background_pngSize);

    // Create filmstrip knobs (128 frames each, Blender-rendered)
    auto makeKnob = [this](const char* name, const void* data, size_t size)
    {
        auto k = std::make_unique<FilmstripKnob>(name, data, size, 128);
        addAndMakeVisible(k.get());
        return k;
    };

    // Top row: TIME, FDBK, DECAY, TONE
    knobTime  = makeKnob("TIME",  BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobFdbk  = makeKnob("FDBK",  BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobDecay = makeKnob("DECAY", BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobTone  = makeKnob("TONE",  BinaryData::knob_TILT_png,   BinaryData::knob_TILT_pngSize);

    // Bottom row: RATE, DEPTH, SPREAD, MIX
    knobRate   = makeKnob("RATE",   BinaryData::knob_DIR_png,    BinaryData::knob_DIR_pngSize);
    knobDepth  = makeKnob("DEPTH",  BinaryData::knob_ENV_png,    BinaryData::knob_ENV_pngSize);
    knobSpread = makeKnob("SPREAD", BinaryData::knob_SPREAD_png, BinaryData::knob_SPREAD_pngSize);
    knobMix    = makeKnob("MIX",    BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);

    // Attach to APVTS
    auto& apvts = processor.getAPVTS();
    attTime   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "time",     *knobTime);
    attFdbk   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "feedback", *knobFdbk);
    attDecay  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "decay",    *knobDecay);
    attTone   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "tone",     *knobTone);
    attRate   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "rate",     *knobRate);
    attDepth  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "depth",    *knobDepth);
    attSpread = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "spread",   *knobSpread);
    attMix    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix",      *knobMix);

    // Ghost renderer
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    addAndMakeVisible(ghostRenderer);

    // Sweep display
    addAndMakeVisible(spectrumDisplay);

    // Timer for UI updates
    startTimerHz(30);

    setSize(900, 600);
}

GhostDelayEditor::~GhostDelayEditor()
{
    stopTimer();
}

void GhostDelayEditor::timerCallback()
{
    // Feed audio level to ghost
    ghostRenderer.setAudioLevel(processor.getCurrentRMSLevel());

    // Feed sweep data to display
    spectrumDisplay.setSweepPosition(processor.getSweepPosition());
    spectrumDisplay.setSweepFrequency(processor.getSweepFrequency());
    spectrumDisplay.setAudioLevel(processor.getCurrentRMSLevel());

    // Self-capture trigger
    juce::File trigger("/tmp/ghost_capture_trigger");
    if (trigger.existsAsFile())
    {
        trigger.deleteFile();
        SelfCapture::capture(this);
    }
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Blender-rendered background
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // Cover baked-in ghost area for animated ghost overlay
    g.setColour(juce::Colour(0x1a, 0x4a, 0x3a));
    g.fillRect(284, 390, 331, 99);

    // ── Knob labels ─────────────────────────────────────────
    g.setColour(juce::Colour(0xcc, 0xcc, 0xdd));
    g.setFont(juce::Font(11.0f).boldened());

    // Top row labels (below knobs): y = 178 + 40 + 4 = 222
    int labelY1 = 222;
    g.drawText("TIME",  310 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("FDBK",  403 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("DECAY", 496 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("TONE",  589 - 40, labelY1, 80, 14, juce::Justification::centred);

    // Bottom row labels: y = 290 + 40 + 4 = 334
    int labelY2 = 334;
    g.drawText("RATE",   310 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("DEPTH",  403 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("SPREAD", 496 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("MIX",    589 - 40, labelY2, 80, 14, juce::Justification::centred);
}

void GhostDelayEditor::resized()
{
    int knobSize = 80;
    int hk = knobSize / 2;

    // Top row: TIME, FDBK, DECAY, TONE
    knobTime->setBounds(310 - hk, 178 - hk, knobSize, knobSize);
    knobFdbk->setBounds(403 - hk, 178 - hk, knobSize, knobSize);
    knobDecay->setBounds(496 - hk, 178 - hk, knobSize, knobSize);
    knobTone->setBounds(589 - hk, 178 - hk, knobSize, knobSize);

    // Bottom row: RATE, DEPTH, SPREAD, MIX
    knobRate->setBounds(310 - hk, 290 - hk, knobSize, knobSize);
    knobDepth->setBounds(403 - hk, 290 - hk, knobSize, knobSize);
    knobSpread->setBounds(496 - hk, 290 - hk, knobSize, knobSize);
    knobMix->setBounds(589 - hk, 290 - hk, knobSize, knobSize);

    // Ghost renderer — ScreenBacking area
    ghostRenderer.setBounds(282, 388, 335, 103);
    ghostRenderer.setSpriteOffset(282, 388);

    // Sweep display — top screen area
    spectrumDisplay.setBounds(323, 77, 254, 35);
}
