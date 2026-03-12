#include "SpectrumDisplay.h"
#include <cmath>

SpectrumDisplay::SpectrumDisplay()
{
    barValues.fill(0.0f);
    barPeaks.fill(0.0f);
    barTargets.fill(0.0f);
    startTimerHz(30);
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
}

void SpectrumDisplay::updateSpectrum(const float* data, int numBins)
{
    // Map input bins to our bar count
    int step = std::max(1, numBins / NUM_BARS);
    for (int i = 0; i < NUM_BARS; ++i)
    {
        float sum = 0.0f;
        int start = i * step;
        int end = std::min(start + step, numBins);
        for (int j = start; j < end; ++j)
            sum += data[j];
        float val = sum / std::max(1, end - start);

        // Log scale for better visual response
        val = std::log10(1.0f + val * 9.0f);
        barTargets[i] = std::min(1.0f, val);
    }
}

void SpectrumDisplay::timerCallback()
{
    // Smooth bar movement
    for (int i = 0; i < NUM_BARS; ++i)
    {
        // Rise fast, fall slow
        if (barTargets[i] > barValues[i])
            barValues[i] += (barTargets[i] - barValues[i]) * 0.6f;
        else
            barValues[i] += (barTargets[i] - barValues[i]) * 0.15f;

        // Peak hold with slow decay
        if (barValues[i] > barPeaks[i])
            barPeaks[i] = barValues[i];
        else
            barPeaks[i] *= 0.97f;
    }

    repaint();
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float barWidth = bounds.getWidth() / NUM_BARS;
    float gap = 1.0f; // 1px gap between bars

    for (int i = 0; i < NUM_BARS; ++i)
    {
        float x = bounds.getX() + i * barWidth;
        float barH = barValues[i] * bounds.getHeight();

        // Bar color: teal gradient, brighter at top
        float intensity = 0.4f + barValues[i] * 0.6f;
        auto barColor = juce::Colour::fromFloatRGBA(
            0.0f,
            intensity * 0.7f,
            intensity * 0.75f,
            0.85f);

        // Draw bar from bottom
        g.setColour(barColor);
        g.fillRect(x + gap * 0.5f,
                   bounds.getBottom() - barH,
                   barWidth - gap,
                   barH);

        // Peak indicator (thin bright line)
        if (barPeaks[i] > 0.02f)
        {
            float peakY = bounds.getBottom() - barPeaks[i] * bounds.getHeight();
            g.setColour(juce::Colour::fromFloatRGBA(0.2f, 0.9f, 0.95f, 0.9f));
            g.fillRect(x + gap * 0.5f, peakY, barWidth - gap, 1.0f);
        }
    }
}
