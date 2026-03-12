#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Ghost sprite renderer — plays back pre-animated Blender frames.
 * Position is baked into the spritesheet (ghost already walks across screen area).
 * Audio level controls animation speed only.
 */
class GhostRenderer : public juce::Component, private juce::Timer
{
public:
    GhostRenderer();
    ~GhostRenderer() override;

    void loadSpritesheet(const void* data, size_t size,
                         int cols, int rows, int totalFrames);

    void paint(juce::Graphics& g) override;

    // Feed audio level (0-1) to control animation speed
    void setAudioLevel(float level) { audioLevel.store(level); }

    // Offset from plugin origin where the spritesheet region starts
    void setSpriteOffset(int x, int y) { offsetX = x; offsetY = y; }

private:
    void timerCallback() override;

    juce::Image spritesheet;
    int spriteCols = 16;
    int spriteRows = 12;
    int totalFrames = 192;
    int frameWidth = 0;
    int frameHeight = 0;
    int currentFrame = 0;

    int offsetX = 0; // where this region sits in the full plugin
    int offsetY = 0;

    std::atomic<float> audioLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostRenderer)
};
