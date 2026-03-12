#include "GhostRenderer.h"

GhostRenderer::GhostRenderer()
{
    startTimerHz(30);
}

GhostRenderer::~GhostRenderer()
{
    stopTimer();
}

void GhostRenderer::loadSpritesheet(const void* data, size_t size,
                                     int cols, int rows, int frames)
{
    spritesheet = juce::ImageCache::getFromMemory(data, static_cast<int>(size));
    spriteCols = cols;
    spriteRows = rows;
    totalFrames = frames;

    if (!spritesheet.isNull())
    {
        frameWidth = spritesheet.getWidth() / spriteCols;
        frameHeight = spritesheet.getHeight() / spriteRows;
    }
}

void GhostRenderer::timerCallback()
{
    float level = audioLevel.load();

    // Animation speed scales with audio level
    int frameStep = 1 + static_cast<int>(level * 3.0f);
    currentFrame = (currentFrame + frameStep) % totalFrames;

    repaint();
}

void GhostRenderer::paint(juce::Graphics& g)
{
    if (spritesheet.isNull() || frameWidth == 0 || frameHeight == 0)
        return;

    // Get the current frame from spritesheet
    int col = currentFrame % spriteCols;
    int row = currentFrame / spriteCols;
    int srcX = col * frameWidth;
    int srcY = row * frameHeight;

    // Draw at 1:1 — position is baked into the sprite (ghost walk path from Blender)
    // The component bounds match the sprite region, so just draw at (0,0)
    g.drawImage(spritesheet,
                0, 0, getWidth(), getHeight(),
                srcX, srcY, frameWidth, frameHeight);
}
