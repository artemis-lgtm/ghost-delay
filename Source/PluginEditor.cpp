#include "PluginEditor.h"
#include "BinaryData.h"

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

    // Create filmstrip knobs (128 frames each, Blender-rendered)
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

    // Timer to feed spectrum data from processor to display
    startTimerHz(30);

    // Set size LAST so resized() doesn't fire before components exist
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

    // Feed spectrum data to display
    float spectrum[48];
    processor.getSpectrum(spectrum, 48);
    spectrumDisplay.updateSpectrum(spectrum, 48);

    // Feed detected key to spectrum display
    auto keyInfo = processor.getDetectedKey();
    spectrumDisplay.setDetectedKey(keyInfo.rootNote, keyInfo.isMinor, keyInfo.confidence);

    // Self-capture: check for trigger file
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

    // ── Paint over the baked-in ghost from Blender background ────
    // The GhostRenderer component draws the animated ghost on top.
    // We need to cover the static ghost that's baked into the background render.
    // ScreenBacking area: (282, 388, 335x103)
    g.setColour(juce::Colour(0x1a, 0x4a, 0x3a)); // match the teal screen color
    g.fillRect(284, 390, 331, 99);

    // ── Paint over the baked-in spectrum from Blender background ─
    // Display_Screen area: (327, 86, 245x36)
    g.setColour(juce::Colour(0x0a, 0x2a, 0x2a)); // dark teal for spectrum area
    g.fillRect(328, 87, 243, 34);

    // ── Knob labels ─────────────────────────────────────────────
    g.setColour(juce::Colour(0xcc, 0xcc, 0xdd));
    g.setFont(juce::Font(11.0f).boldened());

    // Top row labels (below knobs): y = knob center 178 + half knob 40 + gap 4 = 222
    int labelY1 = 222;
    g.drawText("SPREAD", 310 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("DIR",    403 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("TIME",   496 - 40, labelY1, 80, 14, juce::Justification::centred);
    g.drawText("FDBK",   589 - 40, labelY1, 80, 14, juce::Justification::centred);

    // Bottom row labels (below knobs): y = 290 + 40 + 4 = 334
    int labelY2 = 334;
    g.drawText("ENV",    310 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("FREEZE", 403 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("TILT",   496 - 40, labelY2, 80, 14, juce::Justification::centred);
    g.drawText("MIX",    589 - 40, labelY2, 80, 14, juce::Justification::centred);

    // Draw LED (on/off based on bypass state)
    auto& led = processor.isBypassed() ? ledOff : ledOn;
    if (!led.isNull())
    {
        // LED position from flat orthographic: (643, 558) centered
        g.drawImage(led, 626, 541, 34, 34, 0, 0, led.getWidth(), led.getHeight());
    }
}

void GhostDelayEditor::resized()
{
    int knobSize = 80;
    int halfKnob = knobSize / 2;

    // Flat orthographic top-down positions from Blender at 900x600
    // Top row: SPREAD(310,178), DIR(403,178), TIME(496,178), FDBK(589,178)
    knobSpread->setBounds(310 - halfKnob, 178 - halfKnob, knobSize, knobSize);
    knobDir->setBounds(403 - halfKnob, 178 - halfKnob, knobSize, knobSize);
    knobTime->setBounds(496 - halfKnob, 178 - halfKnob, knobSize, knobSize);
    knobFdbk->setBounds(589 - halfKnob, 178 - halfKnob, knobSize, knobSize);

    // Bottom row: ENV(310,290), FREEZE(403,290), TILT(496,290), MIX(589,290)
    knobEnv->setBounds(310 - halfKnob, 290 - halfKnob, knobSize, knobSize);
    knobFreeze->setBounds(403 - halfKnob, 290 - halfKnob, knobSize, knobSize);
    knobTilt->setBounds(496 - halfKnob, 290 - halfKnob, knobSize, knobSize);
    knobMix->setBounds(589 - halfKnob, 290 - halfKnob, knobSize, knobSize);

    // Ghost renderer — ScreenBacking area: (282, 388, 335x103)
    ghostRenderer.setBounds(282, 388, 335, 103);
    ghostRenderer.setSpriteOffset(282, 388);

    // Spectrum display — Display_Screen area: (327, 86, 245x36)
    spectrumDisplay.setBounds(316, 72, 266, 45);

    // Bypass button over LED: (643, 558) with padding
    bypassButton.setBounds(623, 538, 54, 54);
}
