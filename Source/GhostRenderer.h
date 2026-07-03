#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Ghost sprite renderer + XY performance pad, matching the automation example
 * (ghost-haunted-love.mp4): a small pixel ghost with a full-screen crosshair
 * reticle and concentric circles locked to it, and the warp field swirling
 * into a vortex centered on the ghost.
 *   Drag the ghost: X -> AMOUNT (swirl strength), Y -> RATE (spin speed).
 * The field is a CPU swirl remap of the baked waveform plate, rebuilt at half
 * resolution each tick. Walk position is baked into the spritesheet frames,
 * so each frame's ghost bounding box is extracted at load and drawn at the
 * pad position. Audio level still drives animation speed.
 */
class GhostRenderer : public juce::Component, private juce::Timer
{
public:
    GhostRenderer();
    ~GhostRenderer() override;

    void loadSpritesheet(const void* data, size_t size,
                         int cols, int rows, int totalFrames);

    // The un-warped screen field (cropped from the plate render)
    void setFieldImage(juce::Image img) { fieldSrc = std::move(img); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Feed audio level (0-1) to control animation speed
    void setAudioLevel(float level) { audioLevel.store(level); }

    // Current param values driving the vortex (normalized 0-1)
    void setWarpParams(float amount, float rate)
    {
        warpAmount = amount;
        warpRate = rate;
    }

    // Offset from plugin origin where the spritesheet region starts
    void setSpriteOffset(int x, int y) { offsetX = x; offsetY = y; }

    // Normalized pad position (0-1, y=0 is top). Used to reflect host automation.
    void setGhostPosition(float nx, float ny);
    bool isDragging() const { return dragging; }

    // Fired with normalized (x, y) while the ghost is dragged
    std::function<void(float, float)> onGhostMoved;
    std::function<void()> onDragStart, onDragEnd;

private:
    void timerCallback() override;
    void moveTo(juce::Point<float> p, bool notify);
    void rebuildField();

    juce::Image spritesheet;
    int spriteCols = 16;
    int spriteRows = 12;
    int totalFrames = 192;
    int frameWidth = 0;
    int frameHeight = 0;
    int currentFrame = 0;

    // Per-frame ghost bounding box within its frame (walk path is baked in)
    std::vector<juce::Rectangle<int>> ghostBounds;

    juce::Image fieldSrc;      // clean field
    juce::Image fieldWarped;   // swirled, half component resolution
    float warpAmount = 0.4f, warpRate = 0.3f;
    float swirlPhase = 0.0f;

    int offsetX = 0; // where this region sits in the full plugin
    int offsetY = 0;

    float ghostX = 0.5f, ghostY = 0.5f;   // normalized pad position
    bool dragging = false;

    std::atomic<float> audioLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostRenderer)
};
