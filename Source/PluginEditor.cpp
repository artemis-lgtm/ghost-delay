#include "PluginEditor.h"
#include "BinaryData.h"

GhostDelayEditor::GhostDelayEditor(GhostDelayProcessor& p)
    : AudioProcessorEditor(&p), processor(p), waveDisplay(p)
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

    // Haunted Love face: MIX, RATE, FILTER, CRUSH (plate order)
    knobMix    = makeKnob("MIX",    BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);
    knobRate   = makeKnob("RATE",   BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobFilter = makeKnob("FILTER", BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobCrush  = makeKnob("CRUSH",  BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);

    auto& apvts = processor.getAPVTS();
    attMix    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix",    *knobMix);
    attRate   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "rate",   *knobRate);
    attFilter = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "filter", *knobFilter);
    attCrush  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "crush",  *knobCrush);

    // Preset selector over the baked plate box
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem(processor.getProgramName(i), i + 1);
    presetBox.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff05090d));
    presetBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xffa8c8c8));
    presetBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    presetBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffa8c8c8));
    presetBox.onChange = [this] {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != processor.getCurrentProgram())
            processor.setCurrentProgram(idx);
    };
    addAndMakeVisible(presetBox);

    // Ghost renderer — the ghost IS the XY performance pad (reference behavior):
    // drag it across the screen, X = AMOUNT (warp depth), Y = RATE (top = fast).
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    // Hand it the clean screen field, cropped from the plate render
    // (inner glass is (108,88)-(1572,686) in the 1680x1080 design)
    if (!background.isNull())
    {
        const float sx = background.getWidth() / 1680.0f;
        const float sy = background.getHeight() / 1080.0f;
        ghostRenderer.setFieldImage(background.getClippedImage(
            { (int) (108 * sx), (int) (88 * sy),
              (int) (1464 * sx), (int) (598 * sy) })
            .convertedToFormat(juce::Image::ARGB));
    }
    paramAmount = apvts.getParameter("amount");
    paramRate   = apvts.getParameter("rate");
    ghostRenderer.onDragStart = [this] {
        paramAmount->beginChangeGesture();
        paramRate->beginChangeGesture();
    };
    ghostRenderer.onDragEnd = [this] {
        paramAmount->endChangeGesture();
        paramRate->endChangeGesture();
    };
    ghostRenderer.onGhostMoved = [this](float nx, float ny) {
        paramAmount->setValueNotifyingHost(nx);
        paramRate->setValueNotifyingHost(1.0f - ny);
    };
    addAndMakeVisible(ghostRenderer);

    // waveDisplay unused on the Haunted Love face (screen is the baked warp field)

    startTimerHz(30);
    setSize(840, 540);   // 1680x1080 design @ 0.5
}

GhostDelayEditor::~GhostDelayEditor()
{
    stopTimer();
}

void GhostDelayEditor::timerCallback()
{
    ghostRenderer.setAudioLevel(processor.getCurrentRMSLevel());

    // Host automation / preset changes move the ghost (no-op mid-drag)
    ghostRenderer.setGhostPosition(paramAmount->getValue(),
                                   1.0f - paramRate->getValue());
    ghostRenderer.setWarpParams(paramAmount->getValue(), paramRate->getValue());
    waveDisplay.repaint();

    // Self-capture trigger
    juce::File trigger("/tmp/ghost_capture_trigger");
    if (trigger.existsAsFile())
    {
        trigger.deleteFile();
        SelfCapture::capture(this);
    }
}

void GhostDelayEditor::mouseDown(const juce::MouseEvent&)
{
    // No clickable UI elements outside knobs
}

void GhostDelayEditor::paint(juce::Graphics& g)
{
    // Background
    if (!background.isNull())
        g.drawImage(background, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0x1a, 0x1a, 0x1a));

    // Knob labels + screen bezel are baked into the new background.

    // Version label (small, bottom-right corner)
    g.setColour(juce::Colour(0x44, 0x55, 0x55));
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("HL v1.0", getWidth() - 32, getHeight() - 14, 28, 12, juce::Justification::centredRight);
}

void GhostDelayEditor::resized()
{
    // Haunted Love plate: 1680x1080 design rendered knobless, editor at 0.5x
    // (840x540). Knob centers from knob-positions-gui.json: x 470/760/1050/1340,
    // y 865, disc r 86 design px -> 86px disc on screen at 0.5 x r*2.
    // Filmstrip frame math (v2 CS shadow recipe): disc center in the 320px
    // frame is (160.04, 150.22), disc diameter 250 -> frame = disc*320/250.
    const int fs = (int) std::round(86.0 * 320.0 / 250.0);       // 110
    const int ox = (int) std::round(fs * 160.04 / 320.0);        // 55
    const int oy = (int) std::round(fs * 150.22 / 320.0);        // 52
    auto place = [&](FilmstripKnob& k, int cx, int cy)
    {
        k.setBounds(cx - ox, cy - oy, fs, fs);
    };
    place(*knobMix,    235, 432);
    place(*knobRate,   380, 432);
    place(*knobFilter, 525, 432);
    place(*knobCrush,  670, 432);

    // Ghost roams the warp screen: inner glass (108,88)-(1572,686) design -> 0.5x
    ghostRenderer.setBounds(54, 44, 732, 299);
    ghostRenderer.setSpriteOffset(54, 44);

    // Preset selector over the baked inset field (76,896 266x52 design -> 0.5x)
    presetBox.setBounds(38, 448, 133, 26);
}
