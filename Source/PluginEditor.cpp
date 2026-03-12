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
    int knobSize = 40;
    int halfKnob = knobSize / 2;

    // Exact positions from Blender camera projection at 900x600
    // Top row: SPREAD(330,95), DIR(418,96), TIME(507,97), FDBK(596,99)
    knobSpread->setBounds(330 - halfKnob, 95 - halfKnob, knobSize, knobSize);
    knobDir->setBounds(418 - halfKnob, 96 - halfKnob, knobSize, knobSize);
    knobTime->setBounds(507 - halfKnob, 97 - halfKnob, knobSize, knobSize);
    knobFdbk->setBounds(596 - halfKnob, 99 - halfKnob, knobSize, knobSize);

    // Bottom row: ENV(320,211), FREEZE(412,213), TILT(504,215), MIX(597,216)
    knobEnv->setBounds(320 - halfKnob, 211 - halfKnob, knobSize, knobSize);
    knobFreeze->setBounds(412 - halfKnob, 213 - halfKnob, knobSize, knobSize);
    knobTilt->setBounds(504 - halfKnob, 215 - halfKnob, knobSize, knobSize);
    knobMix->setBounds(597 - halfKnob, 216 - halfKnob, knobSize, knobSize);

    // Ghost renderer — Blender-projected walk bounds: offset(296,307), size 313x84
    ghostRenderer.setBounds(296, 307, 313, 84);
    ghostRenderer.setSpriteOffset(296, 307);

    // Spectrum display — exact Blender Display_Screen bbox: (343, 36, 241x32)
    spectrumDisplay.setBounds(343, 36, 241, 32);

    // Bypass button over LED: bbox(642, 454, 34x39) with padding
    bypassButton.setBounds(632, 444, 54, 59);
}
