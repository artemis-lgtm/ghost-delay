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
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Draw Blender-rendered background
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // Draw LED (on/off based on bypass state)
    auto& led = processor.isBypassed() ? ledOff : ledOn;
    if (!led.isNull())
    {
        // Exact position from Blender: bbox(642, 454, 34x39)
        g.drawImage(led, 642, 454, 34, 39, 0, 0, led.getWidth(), led.getHeight());
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
    spectrumDisplay.setBounds(327, 86, 245, 36);

    // Bypass button over LED: (643, 558) with padding
    bypassButton.setBounds(623, 538, 54, 54);
}
