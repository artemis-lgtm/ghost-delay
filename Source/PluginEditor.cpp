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

    // Top row: SIZE, DECAY, TONE, MIX
    knobSize  = makeKnob("SIZE",  BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobDecay = makeKnob("DECAY", BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobTone  = makeKnob("TONE",  BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobMix   = makeKnob("MIX",   BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);

    // Bottom row: SHIMMER, DUCK, WIDTH, GRIT (reverb-native)
    knobShimmer = makeKnob("SHIMMER", BinaryData::knob_TIME_png,   BinaryData::knob_TIME_pngSize);
    knobDuck    = makeKnob("DUCK",    BinaryData::knob_FDBK_png,   BinaryData::knob_FDBK_pngSize);
    knobWidth   = makeKnob("WIDTH",   BinaryData::knob_FREEZE_png, BinaryData::knob_FREEZE_pngSize);
    knobGrit    = makeKnob("GRIT",    BinaryData::knob_MIX_png,    BinaryData::knob_MIX_pngSize);

    // Attach to APVTS
    auto& apvts = processor.getAPVTS();
    attSize  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "time",     *knobSize);
    attDecay = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "feedback", *knobDecay);
    attTone  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "decay",    *knobTone);
    attMix   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "tone",     *knobMix);

    attShimmer = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "rate",   *knobShimmer);
    attDuck    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "depth",  *knobDuck);
    attWidth   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "spread", *knobWidth);
    attGrit    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix",    *knobGrit);

    // Ghost renderer
    ghostRenderer.loadSpritesheet(
        BinaryData::ghost_spritesheet_png,
        BinaryData::ghost_spritesheet_pngSize,
        16, 12, 192);
    addAndMakeVisible(ghostRenderer);

    // Live waveform on the top LED strip (real audio tap from the processor)
    addAndMakeVisible(waveDisplay);

    startTimerHz(30);
    setSize(471, 615);   // 1240x1620 design @ 0.37984
}

GhostDelayEditor::~GhostDelayEditor()
{
    stopTimer();
}

void GhostDelayEditor::timerCallback()
{
    ghostRenderer.setAudioLevel(processor.getCurrentRMSLevel());
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
    g.drawText("v7.3", getWidth() - 32, getHeight() - 14, 28, 12, juce::Justification::centredRight);
}

void GhostDelayEditor::resized()
{
    // Pedal design space: knob centers x (230,490,750,1010), rows y 640/1020,
    // scaled by 471/1240 = 0.37984. Labels are baked above the knobs.
    //
    // v2 shadowed frames (320px, CS shadow recipe): the knob disc is NOT
    // centered in the frame — measured disc center (160.04, 150.22), disc
    // diameter 250px. To show an 84px disc, draw the frame at 84*320/250
    // ≈ 108px and offset so the DISC center lands on the knob center.
    const int fs = 108;                              // drawn frame size
    const int ox = (int) std::round(fs * 160.04 / 320.0);  // 54: disc cx in frame
    const int oy = (int) std::round(fs * 150.22 / 320.0);  // 51: disc cy in frame
    auto place = [&](FilmstripKnob& k, int cx, int cy)
    {
        k.setBounds(cx - ox, cy - oy, fs, fs);
    };

    // Top row: SIZE, DECAY, TONE, MIX
    place(*knobSize,   87, 243);
    place(*knobDecay, 186, 243);
    place(*knobTone,  285, 243);
    place(*knobMix,   384, 243);

    // Bottom row: SHIMMER, DUCK, WIDTH, GRIT
    place(*knobShimmer,  87, 387);
    place(*knobDuck,    186, 387);
    place(*knobWidth,   285, 387);
    place(*knobGrit,    384, 387);

    // Ghost renderer — fills the OLED well baked into the new background
    ghostRenderer.setBounds(78, 454, 315, 107);
    ghostRenderer.setSpriteOffset(78, 454);

    // Live waveform — fills the analyzer panel window, covering the static
    // wave baked into the plate (measured: design x 184-1056, y 301-408)
    waveDisplay.setBounds(70, 114, 332, 42);
}
