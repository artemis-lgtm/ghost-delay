#include "GhostRenderer.h"

GhostRenderer::GhostRenderer()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setOpaque(false);
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

    if (spritesheet.isNull())
        return;

    frameWidth = spritesheet.getWidth() / spriteCols;
    frameHeight = spritesheet.getHeight() / spriteRows;

    // One-time alpha scan: where the ghost sits inside each frame.
    ghostBounds.assign((size_t) totalFrames, {});
    juce::Image::BitmapData bd(spritesheet, juce::Image::BitmapData::readOnly);
    for (int f = 0; f < totalFrames; ++f)
    {
        const int fx = (f % spriteCols) * frameWidth;
        const int fy = (f / spriteCols) * frameHeight;
        int minX = frameWidth, minY = frameHeight, maxX = -1, maxY = -1;
        for (int y = 0; y < frameHeight; ++y)
            for (int x = 0; x < frameWidth; ++x)
                if (bd.getPixelColour(fx + x, fy + y).getAlpha() > 12)
                {
                    minX = juce::jmin(minX, x); maxX = juce::jmax(maxX, x);
                    minY = juce::jmin(minY, y); maxY = juce::jmax(maxY, y);
                }
        ghostBounds[(size_t) f] = (maxX >= 0)
            ? juce::Rectangle<int>(fx + minX, fy + minY, maxX - minX + 1, maxY - minY + 1)
            : juce::Rectangle<int>(fx, fy, frameWidth, frameHeight);
    }
}

void GhostRenderer::resized()
{
    fieldWarped = juce::Image();   // force re-alloc at the new size
}

void GhostRenderer::timerCallback()
{
    float level = audioLevel.load();

    // Animation speed scales with audio level
    int frameStep = 1 + static_cast<int>(level * 3.0f);
    currentFrame = (currentFrame + frameStep) % totalFrames;

    // Vortex spin: RATE drives it, audio adds a push
    swirlPhase += (0.01f + warpRate * 0.09f + level * 0.03f);
    if (swirlPhase > juce::MathConstants<float>::twoPi)
        swirlPhase -= juce::MathConstants<float>::twoPi;

    rebuildField();
    repaint();
}

void GhostRenderer::rebuildField()
{
    if (fieldSrc.isNull() || getWidth() <= 0 || getHeight() <= 0)
        return;

    // Half component resolution keeps this ~55k pixels per tick
    const int w = juce::jmax(1, getWidth() / 2);
    const int h = juce::jmax(1, getHeight() / 2);
    if (fieldWarped.getWidth() != w || fieldWarped.getHeight() != h)
        fieldWarped = juce::Image(juce::Image::ARGB, w, h, false);

    juce::Image::BitmapData src(fieldSrc, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst(fieldWarped, juce::Image::BitmapData::writeOnly);

    const float sw = (float) fieldSrc.getWidth();
    const float sh = (float) fieldSrc.getHeight();
    const float cx = ghostX * (float) w;
    const float cy = ghostY * (float) h;

    // Swirl reach and twist scale with AMOUNT (example: field wraps into a
    // full spiral at high amount, barely disturbed near zero)
    const float reach = (0.18f + warpAmount * 0.55f) * (float) w;
    const float twist = warpAmount * 7.5f;

    for (int y = 0; y < h; ++y)
    {
        auto* line = (juce::PixelARGB*) dst.getLinePointer(y);
        for (int x = 0; x < w; ++x)
        {
            const float dx = (float) x - cx;
            const float dy = (float) y - cy;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float fall = std::exp(-r / reach);
            float sxp = (float) x, syp = (float) y;
            if (fall > 0.003f)
            {
                const float a = std::atan2(dy, dx) + twist * fall + swirlPhase * fall;
                sxp = cx + r * std::cos(a);
                syp = cy + r * std::sin(a);
            }
            const int px = juce::jlimit(0, (int) sw - 1, (int) (sxp * sw / (float) w));
            const int py = juce::jlimit(0, (int) sh - 1, (int) (syp * sh / (float) h));
            line[x].set(*(const juce::PixelARGB*) src.getPixelPointer(px, py));
        }
    }
}

void GhostRenderer::setGhostPosition(float nx, float ny)
{
    if (dragging) return;   // live drag wins over host echo
    ghostX = juce::jlimit(0.0f, 1.0f, nx);
    ghostY = juce::jlimit(0.0f, 1.0f, ny);
}

void GhostRenderer::moveTo(juce::Point<float> p, bool notify)
{
    ghostX = juce::jlimit(0.0f, 1.0f, p.x / (float) juce::jmax(1, getWidth()));
    ghostY = juce::jlimit(0.0f, 1.0f, p.y / (float) juce::jmax(1, getHeight()));
    if (notify && onGhostMoved)
        onGhostMoved(ghostX, ghostY);
    repaint();
}

void GhostRenderer::mouseDown(const juce::MouseEvent& e)
{
    dragging = true;
    if (onDragStart) onDragStart();
    moveTo(e.position, true);
}

void GhostRenderer::mouseDrag(const juce::MouseEvent& e)
{
    moveTo(e.position, true);
}

void GhostRenderer::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    if (onDragEnd) onDragEnd();
}

void GhostRenderer::paint(juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float cx = ghostX * W;
    const float cy = ghostY * H;

    // 1. Swirled field (covers the baked plate field exactly)
    if (!fieldWarped.isNull())
        g.drawImage(fieldWarped, getLocalBounds().toFloat());

    // 2. Center glow: white-hot core like the example, stronger with AMOUNT
    {
        const float gr = H * (0.35f + warpAmount * 0.55f);
        juce::ColourGradient glow(
            juce::Colours::white.withAlpha(0.08f + warpAmount * 0.30f), cx, cy,
            juce::Colours::white.withAlpha(0.0f), cx + gr, cy, true);
        g.setGradientFill(glow);
        g.fillEllipse(cx - gr, cy - gr, gr * 2.0f, gr * 2.0f);
    }

    // 3. Crosshair reticle locked to the ghost (full-screen thin lines)
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawLine(0.0f, cy, W, cy, 1.0f);
    g.drawLine(cx, 0.0f, cx, H, 1.0f);

    // 4. Concentric circles: one bright ring, fainter ones outward
    const float r1 = H * 0.155f;
    g.drawEllipse(cx - r1, cy - r1, r1 * 2.0f, r1 * 2.0f, 1.6f);
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    for (float m : { 1.7f, 2.5f, 3.3f })
    {
        const float rr = r1 * m;
        g.drawEllipse(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.0f);
    }

    // 5. The ghost, small like the example (~12% of screen height).
    // Nearest-neighbor keeps the pixel-art crisp at this size.
    if (!spritesheet.isNull() && !ghostBounds.empty())
    {
        const auto src = ghostBounds[(size_t)(currentFrame % (int) ghostBounds.size())];
        const float scale = (H * 0.12f) / (float) juce::jmax(1, src.getHeight());
        const int dw = (int) (src.getWidth() * scale);
        const int dh = (int) (src.getHeight() * scale);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        // Sprite alpha is soft; stack it so the small ghost reads solid
        // like the automation example (1-(1-a)^4 coverage).
        for (int pass = 0; pass < 4; ++pass)
            g.drawImage(spritesheet,
                        (int) cx - dw / 2, (int) cy - dh / 2, dw, dh,
                        src.getX(), src.getY(), src.getWidth(), src.getHeight());
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    }
}
