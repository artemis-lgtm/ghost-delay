#include "PluginEditor.h"
#include "BinaryData.h"

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Load background (Blender-rendered pedal body — no text, no knob domes, no ghost)
    background = juce::ImageCache::getFromMemory(
        BinaryData::background_png, BinaryData::background_pngSize);

    // Load LED states
    ledOn = juce::ImageCache::getFromMemory(
        BinaryData::led_on_png, BinaryData::led_on_pngSize);
    ledOff = juce::ImageCache::getFromMemory(
        BinaryData::led_off_png, BinaryData::led_off_pngSize);

    // Create filmstrip knobs (128 frames each, transparent background)
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

    // Window matches pedal aspect ratio (tight crop, no background)
    setSize(500, 657);
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
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Draw Blender-rendered background (all text hidden — JUCE draws everything)
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // ── Fill screen areas (cover any baked-in artifacts) ────────
    // ScreenBacking: (64, 427, 372x113)
    g.setColour(juce::Colour(0x1a, 0x4a, 0x3a));
    g.fillRect(66, 429, 368, 109);

    // Display_Screen: (108, 81, 284x41)
    g.setColour(juce::Colour(0x0a, 0x2a, 0x2a));
    g.fillRect(109, 82, 282, 39);

    // ── Title text ──────────────────────────────────────────────
    g.setColour(juce::Colour(0xdd, 0xdd, 0xee));
    g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    g.drawText("T I N Y   G H O S T", 0, 28, 500, 22, juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.setColour(juce::Colour(0x99, 0x99, 0xbb));
    g.drawText("SPECTRAL DELAY", 0, 50, 500, 14, juce::Justification::centred);

    // ── Knob labels (below each knob, centered on skirt) ────────
    g.setColour(juce::Colour(0xcc, 0xcc, 0xdd));
    g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());

    // Positions from final Blender render (skirt centers):
    // Top row: SPREAD(95,194) DIR(198,194) TIME(302,194) FDBK(405,194)
    // Bottom row: ENV(95,318) FREEZE(198,318) TILT(302,318) MIX(405,318)
    // Knob component = 50px, so label y = center + 25 + 2 = center + 27
    int labelY1 = 194 + 27;
    g.drawText("SPREAD",  95 - 30, labelY1, 60, 12, juce::Justification::centred);
    g.drawText("DIR",    198 - 30, labelY1, 60, 12, juce::Justification::centred);
    g.drawText("TIME",   302 - 30, labelY1, 60, 12, juce::Justification::centred);
    g.drawText("FDBK",   405 - 30, labelY1, 60, 12, juce::Justification::centred);

    int labelY2 = 318 + 27;
    g.drawText("ENV",     95 - 30, labelY2, 60, 12, juce::Justification::centred);
    g.drawText("FREEZE", 198 - 30, labelY2, 60, 12, juce::Justification::centred);
    g.drawText("TILT",   302 - 30, labelY2, 60, 12, juce::Justification::centred);
    g.drawText("MIX",    405 - 30, labelY2, 60, 12, juce::Justification::centred);

    // ── LED ─────────────────────────────────────────────────────
    auto& led = processor.isBypassed() ? ledOff : ledOn;
    if (!led.isNull())
    {
        // LED center: (464, 615)
        g.drawImage(led, 464 - 17, 615 - 17, 34, 34, 0, 0, led.getWidth(), led.getHeight());
    }
}

void GhostDelayEditor::resized()
{
    // Knob size: 50px (matches transparent filmstrip frame size)
    int knobSize = 50;
    int hk = knobSize / 2;

    // Exact skirt center positions from final Blender render
    // Top row: SPREAD(95,194) DIR(198,194) TIME(302,194) FDBK(405,194)
    knobSpread->setBounds( 95 - hk, 194 - hk, knobSize, knobSize);
    knobDir->setBounds(   198 - hk, 194 - hk, knobSize, knobSize);
    knobTime->setBounds(  302 - hk, 194 - hk, knobSize, knobSize);
    knobFdbk->setBounds(  405 - hk, 194 - hk, knobSize, knobSize);

    // Bottom row: ENV(95,318) FREEZE(198,318) TILT(302,318) MIX(405,318)
    knobEnv->setBounds(    95 - hk, 318 - hk, knobSize, knobSize);
    knobFreeze->setBounds(198 - hk, 318 - hk, knobSize, knobSize);
    knobTilt->setBounds(  302 - hk, 318 - hk, knobSize, knobSize);
    knobMix->setBounds(   405 - hk, 318 - hk, knobSize, knobSize);

    // Ghost renderer — ScreenBacking: (64, 427, 372x113)
    ghostRenderer.setBounds(64, 427, 372, 113);
    ghostRenderer.setSpriteOffset(64, 427);

    // Spectrum display — Display_Screen: (108, 81, 284x41)
    spectrumDisplay.setBounds(108, 81, 284, 41);

    // Bypass button around LED: (464, 615) center
    bypassButton.setBounds(464 - 27, 615 - 27, 54, 54);
}
