#include "PluginEditor.h"
#include "BinaryData.h"
#include "SelfCapture.h"

// ────────────────────────────────────────────────────────────────
// Pixel-accurate positions from background.png analysis
// (NOT from Blender projection — those were wrong)
//
// Background: 500 x 657
// Knob circles: 50px diameter, white/cream
// LED screen: dark interior within metallic bezel
// Ghost screen: teal/dark interior at bottom
// ────────────────────────────────────────────────────────────────

static constexpr int kW = 500;
static constexpr int kH = 657;
static constexpr int kKnobSize = 50;

// REAL knob centers (pixel-measured from background.png)
struct KnobPos { int x, y; const char* label; };

// Ground truth from pixel analysis (threshold>200, confirmed across y=148-155 and y=310-317)
// These are EXACT centers of the white circles in the Blender background
static const KnobPos topRow[] = {
    { 46,  151, "SPREAD" },
    { 182, 151, "DIR" },
    { 317, 151, "TIME" },
    { 452, 151, "FDBK" }
};

static const KnobPos botRow[] = {
    { 46,  313, "ENV" },
    { 182, 313, "FREEZE" },
    { 317, 313, "TILT" },
    { 452, 313, "MIX" }
};

// LED screen interior (pixel-measured)
// Bezel edge starts ~y=58, screen surface y=62-116, x=58-456
static constexpr int kLedX = 58, kLedY = 62, kLedW = 398, kLedH = 54;

// Ghost screen interior (pixel-measured)
// Dark interior y=440-555, x=25-475
static constexpr int kGhostX = 25, kGhostY = 440, kGhostW = 450, kGhostH = 115;

// Label position: knob white circles end at y = center + 25
// Metal surface starts at y ~ center + 28
// Labels sit at knobY + 28
static constexpr int kLabelOffY = 28;

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Load background
    backgroundImage = juce::ImageCache::getFromMemory(
        BinaryData::background_png, BinaryData::background_pngSize);

    // Create filmstrip knobs
    auto makeKnob = [this](const void* data, size_t size) -> std::unique_ptr<FilmstripKnob>
    {
        auto k = std::make_unique<FilmstripKnob>("knob", data, size, 128);
        k->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible(*k);
        return k;
    };

    knobSpread = makeKnob(BinaryData::knob_SPREAD_png, BinaryData::knob_SPREAD_pngSize);
    knobDir    = makeKnob(BinaryData::knob_DIR_png,    BinaryData::knob_DIR_pngSize);
    knobTime   = makeKnob(BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobFdbk   = makeKnob(BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobEnv    = makeKnob(BinaryData::knob_ENV_png,     BinaryData::knob_ENV_pngSize);
    knobFreeze = makeKnob(BinaryData::knob_FREEZE_png,  BinaryData::knob_FREEZE_pngSize);
    knobTilt   = makeKnob(BinaryData::knob_TILT_png,    BinaryData::knob_TILT_pngSize);
    knobMix    = makeKnob(BinaryData::knob_MIX_png,     BinaryData::knob_MIX_pngSize);

    // Attach to APVTS
    auto& apvts = processor.getAPVTS();
    attSpread = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "spread",    *knobSpread);
    attDir    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "direction", *knobDir);
    attTime   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "time",      *knobTime);
    attFdbk   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "feedback",  *knobFdbk);
    attEnv    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "envelope",  *knobEnv);
    attFreeze = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "freeze",    *knobFreeze);
    attTilt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "tilt",      *knobTilt);
    attMix    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix",       *knobMix);

    // Ghost renderer
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    addAndMakeVisible(ghostRenderer);

    // Spectrum display (LED screen)
    addAndMakeVisible(spectrumTop);

    startTimerHz(30);
    setSize(kW, kH);

    // Self-capture after paint
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
    spectrumTop.updateSpectrum(spectrum, 48);

    auto keyInfo = processor.getDetectedKey();
    spectrumTop.setDetectedKey(keyInfo.rootNote, keyInfo.isMinor, keyInfo.confidence);

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
    // ── Blender background (the entire visual foundation) ───────
    if (backgroundImage.isValid())
        g.drawImageAt(backgroundImage, 0, 0);
    else
        g.fillAll(juce::Colour(0xff2a2a3a));

    // ── Paint over baked ghost sprites so GhostRenderer draws clean ─
    // Match the dark interior color of the ghost screen
    g.setColour(juce::Colour(0xff0f1717));
    g.fillRect(kGhostX + 4, kGhostY, kGhostW - 8, kGhostH);

    // ── Knob labels (embossed look on metal surface) ────────────
    // Dark shadow underneath, then light text on top = engraved appearance
    for (auto& k : topRow)
    {
        int lx = k.x - 30;
        int ly = k.y + kLabelOffY;
        // Shadow (1px down, darker)
        g.setColour(juce::Colour(0xff1a1e26));
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(k.label, lx, ly + 1, 60, 13, juce::Justification::centred);
        // Light text
        g.setColour(juce::Colour(0xffc0c8d4));
        g.drawText(k.label, lx, ly, 60, 13, juce::Justification::centred);
    }

    for (auto& k : botRow)
    {
        int lx = k.x - 30;
        int ly = k.y + kLabelOffY;
        g.setColour(juce::Colour(0xff1a1e26));
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(k.label, lx, ly + 1, 60, 13, juce::Justification::centred);
        g.setColour(juce::Colour(0xffc0c8d4));
        g.drawText(k.label, lx, ly, 60, 13, juce::Justification::centred);
    }
}

void GhostDelayEditor::resized()
{
    // ── Filmstrip knobs at pixel-accurate positions ─────────────
    auto place = [](std::unique_ptr<FilmstripKnob>& knob, int cx, int cy)
    {
        if (knob)
            knob->setBounds(cx - kKnobSize / 2, cy - kKnobSize / 2, kKnobSize, kKnobSize);
    };

    place(knobSpread, topRow[0].x, topRow[0].y);
    place(knobDir,    topRow[1].x, topRow[1].y);
    place(knobTime,   topRow[2].x, topRow[2].y);
    place(knobFdbk,   topRow[3].x, topRow[3].y);
    place(knobEnv,    botRow[0].x, botRow[0].y);
    place(knobFreeze, botRow[1].x, botRow[1].y);
    place(knobTilt,   botRow[2].x, botRow[2].y);
    place(knobMix,    botRow[3].x, botRow[3].y);

    // ── LED screen (spectrum display) — fits inside Blender bezel ─
    spectrumTop.setBounds(kLedX, kLedY, kLedW, kLedH);

    // ── Ghost renderer — fits inside ghost screen interior ──────
    ghostRenderer.setBounds(kGhostX + 4, kGhostY, kGhostW - 8, kGhostH);
}
