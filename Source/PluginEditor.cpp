#include "PluginEditor.h"
#include "BinaryData.h"
#include "SelfCapture.h"

// ────────────────────────────────────────────────────────────────
// v1.3 layout restored: 500x657 portrait, 70px knobs, v1.3 positions
// This is the version Austin approved.
// ────────────────────────────────────────────────────────────────

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Load background (Blender-rendered pedal body, no ghost/LED)
    background = juce::ImageCache::getFromMemory(
        BinaryData::background_png, BinaryData::background_pngSize);

    // Load LED states
    ledOn = juce::ImageCache::getFromMemory(
        BinaryData::led_on_png, BinaryData::led_on_pngSize);
    ledOff = juce::ImageCache::getFromMemory(
        BinaryData::led_off_png, BinaryData::led_off_pngSize);

    // Create filmstrip knobs (128 frames each, Blender-rendered 3D knobs)
    auto makeKnob = [this](const char* name, const void* data, size_t size)
    {
        auto k = std::make_unique<FilmstripKnob>(name, data, size, 128);
        addAndMakeVisible(k.get());
        return k;
    };

    knobSpread = makeKnob("SPREAD", BinaryData::knob_SPREAD_png, BinaryData::knob_SPREAD_pngSize);
    knobTime   = makeKnob("TIME",   BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobDir    = makeKnob("DIR",    BinaryData::knob_DIR_png,    BinaryData::knob_DIR_pngSize);
    knobFdbk   = makeKnob("FDBK",   BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobEnv    = makeKnob("ENV",    BinaryData::knob_ENV_png,    BinaryData::knob_ENV_pngSize);
    knobFreeze = makeKnob("FREEZE", BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobTilt   = makeKnob("TILT",   BinaryData::knob_TILT_png,   BinaryData::knob_TILT_pngSize);
    knobMix    = makeKnob("MIX",    BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);

    // Attach to APVTS
    auto& apvts = processor.getAPVTS();
    attTime   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "time",      *knobTime);
    attFdbk   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "feedback",  *knobFdbk);
    attMix    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix",       *knobMix);
    attFreeze = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "freeze",    *knobFreeze);
    attTilt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "tilt",      *knobTilt);
    attSpread = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "spread",    *knobSpread);
    attDir    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "direction", *knobDir);
    attEnv    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "envelope",  *knobEnv);

    // Ghost renderer
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    addAndMakeVisible(ghostRenderer);

    // Spectrum display
    addAndMakeVisible(spectrumDisplay);

    // Bypass button (invisible, overlays LED position)
    bypassButton.setButtonText("");
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    bypassButton.onClick = [this]()
    {
        processor.setBypassed(!processor.isBypassed());
        repaint();
    };
    addAndMakeVisible(bypassButton);

    startTimerHz(30);
    setSize(500, 657);

    // Self-capture at 2x for Retina quality
    juce::Timer::callAfterDelay(500, [this]() {
        SelfCapture::capture(this);
    });
}

GhostDelayEditor::~GhostDelayEditor()
{
    stopTimer();
}

void GhostDelayEditor::timerCallback()
{
    ghostRenderer.setAudioLevel(processor.getCurrentRMSLevel());

    float spectrum[48];
    processor.getSpectrum(spectrum, 48);
    spectrumDisplay.updateSpectrum(spectrum, 48);

    auto keyInfo = processor.getDetectedKey();
    spectrumDisplay.setDetectedKey(keyInfo.rootNote, keyInfo.isMinor, keyInfo.confidence);

    // File-triggered re-capture
    juce::File trigger("/tmp/ghost_capture_trigger");
    if (trigger.existsAsFile())
    {
        trigger.deleteFile();
        SelfCapture::capture(this);
    }
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Draw Blender-rendered background
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // ── Paint over baked-in ghost and spectrum from background ────
    // Ghost screen area: (74, 421, 351x107)
    g.setColour(juce::Colour(0x1a, 0x4a, 0x3a));
    g.fillRect(76, 423, 347, 103);

    // Spectrum display area: (115, 94, 269x39)
    g.setColour(juce::Colour(0x0a, 0x2a, 0x2a));
    g.fillRect(116, 95, 267, 37);

    // Title and text are baked into the Blender background — don't double-draw

    // ── LED ─────────────────────────────────────────────────────
    auto& led = processor.isBypassed() ? ledOff : ledOn;
    if (!led.isNull())
        g.drawImage(led, 435, 581, 34, 34, 0, 0, led.getWidth(), led.getHeight());
}

void GhostDelayEditor::resized()
{
    int knobSize = 70;
    int hk = knobSize / 2;

    // v1.3 exact positions (tight crop at 500x657)
    // Top row: SPREAD(103,201) DIR(201,201) TIME(298,201) FDBK(396,201)
    knobSpread->setBounds(103 - hk, 201 - hk, knobSize, knobSize);
    knobDir->setBounds(   201 - hk, 201 - hk, knobSize, knobSize);
    knobTime->setBounds(  298 - hk, 201 - hk, knobSize, knobSize);
    knobFdbk->setBounds(  396 - hk, 201 - hk, knobSize, knobSize);

    // Bottom row: ENV(103,318) FREEZE(201,318) TILT(298,318) MIX(396,318)
    knobEnv->setBounds(   103 - hk, 318 - hk, knobSize, knobSize);
    knobFreeze->setBounds(201 - hk, 318 - hk, knobSize, knobSize);
    knobTilt->setBounds(  298 - hk, 318 - hk, knobSize, knobSize);
    knobMix->setBounds(   396 - hk, 318 - hk, knobSize, knobSize);

    // Ghost renderer — ScreenBacking: (74, 421, 351x107)
    ghostRenderer.setBounds(74, 421, 351, 107);
    ghostRenderer.setSpriteOffset(74, 421);

    // Spectrum display — Display_Screen: (115, 94, 269x39)
    spectrumDisplay.setBounds(115, 94, 269, 39);

    // Bypass button over LED: (452, 598) with padding
    bypassButton.setBounds(432, 578, 54, 54);
}
