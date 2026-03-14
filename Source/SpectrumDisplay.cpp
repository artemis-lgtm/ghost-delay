#include "SpectrumDisplay.h"
#include <cmath>

SpectrumDisplay::SpectrumDisplay()
{
    startTimerHz(30);
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
}

void SpectrumDisplay::setSweepPosition(float pos)
{
    targetPos = std::clamp(pos, 0.0f, 1.0f);
}

void SpectrumDisplay::setSweepFrequency(float freqHz)
{
    displayFreq = freqHz;
}

void SpectrumDisplay::setAudioLevel(float level)
{
    displayLevel = level;
}

void SpectrumDisplay::timerCallback()
{
    // Smooth position for display
    displayPos += (targetPos - displayPos) * 0.4f;

    // Trail follows more slowly
    trailPos += (targetPos - trailPos) * 0.1f;

    repaint();
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight();

    // ── Background glow trail ───────────────────────────────
    // Draw a soft trail showing where the sweep has been
    {
        float trailX = bounds.getX() + trailPos * w;
        float trailW = std::max(w * 0.15f, 20.0f);

        juce::ColourGradient trail(
            juce::Colour::fromFloatRGBA(0.0f, 0.5f, 0.55f, 0.15f),
            trailX, bounds.getY(),
            juce::Colour::fromFloatRGBA(0.0f, 0.3f, 0.35f, 0.0f),
            trailX + trailW * 0.5f, bounds.getY(),
            true);  // radial = true? No, linear is fine for a sweep

        // Soft rectangular glow
        g.setColour(juce::Colour::fromFloatRGBA(0.0f, 0.4f, 0.45f, 0.12f));
        float halfTrail = trailW * 0.5f;
        g.fillRect(trailX - halfTrail, bounds.getY(), trailW, h);
    }

    // ── Main sweep indicator ────────────────────────────────
    {
        float sweepX = bounds.getX() + displayPos * w;
        float intensity = 0.5f + std::min(displayLevel * 3.0f, 0.5f);

        // Outer glow (wider, dimmer)
        float glowW = std::max(w * 0.08f, 12.0f);
        g.setColour(juce::Colour::fromFloatRGBA(0.0f, 0.7f, 0.8f, intensity * 0.3f));
        g.fillRect(sweepX - glowW * 0.5f, bounds.getY(), glowW, h);

        // Inner bright bar
        float barW = std::max(w * 0.025f, 4.0f);
        g.setColour(juce::Colour::fromFloatRGBA(0.1f, 0.95f, 1.0f, intensity * 0.85f));
        g.fillRect(sweepX - barW * 0.5f, bounds.getY(), barW, h);

        // Bright center line
        g.setColour(juce::Colour::fromFloatRGBA(0.5f, 1.0f, 1.0f, intensity));
        g.fillRect(sweepX - 0.5f, bounds.getY(), 1.0f, h);
    }

    // ── Frequency label ─────────────────────────────────────
    {
        int freqInt = (int)(displayFreq + 0.5f);
        juce::String freqText = juce::String(freqInt) + " Hz";

        // Position text near the sweep bar but clamped to screen
        float textX = bounds.getX() + displayPos * w;
        float textW = 50.0f;

        // Clamp so text doesn't go off-screen
        if (textX + textW * 0.5f > bounds.getRight() - 5.0f)
            textX = bounds.getRight() - textW - 5.0f;
        if (textX - textW * 0.5f < bounds.getX() + 5.0f)
            textX = bounds.getX() + textW * 0.5f + 5.0f;

        g.setColour(juce::Colour::fromFloatRGBA(0.0f, 0.85f, 0.95f, 0.7f));
        g.setFont(juce::Font(9.0f));
        g.drawText(freqText,
                   (int)(textX - textW * 0.5f), (int)bounds.getY() + 1,
                   (int)textW, 12,
                   juce::Justification::centred, false);
    }

    // ── Scale markers (60 Hz and 900 Hz at edges) ───────────
    g.setColour(juce::Colour::fromFloatRGBA(0.3f, 0.5f, 0.55f, 0.3f));
    g.setFont(juce::Font(8.0f));
    g.drawText("60", (int)bounds.getX() + 2, (int)(bounds.getBottom() - 11), 20, 10,
               juce::Justification::centredLeft, false);
    g.drawText("900", (int)(bounds.getRight() - 25), (int)(bounds.getBottom() - 11), 23, 10,
               juce::Justification::centredRight, false);
}
