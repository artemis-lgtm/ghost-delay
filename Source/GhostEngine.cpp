#include "GhostEngine.h"
#include <algorithm>

// Static constexpr definitions
constexpr float GhostEngine::delaySubs[NUM_SUBS];

GhostEngine::GhostEngine() {}

void GhostEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;

    // Clear delay lines
    delayL.fill(0.0f);
    delayR.fill(0.0f);
    delayWriteL = 0;
    delayWriteR = 0;
    smoothDelayL = 0.0f;
    smoothDelayR = 0.0f;

    // Reverb
    reverb.setSampleRate(sr);
    reverb.reset();

    // SVF
    svfL.reset();
    svfR.reset();

    // DC-blocking HP at 30 Hz (1-pole)
    float hpFreq = 30.0f;
    hpCoeff = 1.0f - (juce::MathConstants<float>::twoPi * hpFreq / (float)sr);
    hpCoeff = std::clamp(hpCoeff, 0.9f, 0.9999f);
    hpStateL = 0.0f;
    hpStateR = 0.0f;

    // LFO
    lfoPhase = 0.0;

    // Wet buffer
    wetBuffer.setSize(2, blockSize);
    wetBuffer.clear();
}

void GhostEngine::reset()
{
    delayL.fill(0.0f);
    delayR.fill(0.0f);
    delayWriteL = 0;
    delayWriteR = 0;
    smoothDelayL = 0.0f;
    smoothDelayR = 0.0f;

    reverb.reset();
    svfL.reset();
    svfR.reset();
    hpStateL = 0.0f;
    hpStateR = 0.0f;
    lfoPhase = 0.0;
}

// ═════════════════════════════════════════════════════════════════
// Main processing
// ═════════════════════════════════════════════════════════════════
void GhostEngine::process(juce::AudioBuffer<float>& buffer,
                           juce::AudioPlayHead* playHead)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), 2);

    // ── Read host transport ─────────────────────────────────
    hasPPQ = false;
    if (playHead != nullptr)
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBPM = std::max(*bpm, 20.0);

            hostPlaying = pos->getIsPlaying();

            if (auto ppq = pos->getPpqPosition())
            {
                ppqPosition = *ppq;
                hasPPQ = true;
            }
        }
    }

    // ── Ensure wet buffer is large enough ───────────────────
    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.clear();

    // ── Update reverb parameters ────────────────────────────
    updateReverbParams();

    // ── Target delay times ──────────────────────────────────
    float targetDelayL = getDelayTimeSamples(0);
    float targetDelayR = getDelayTimeSamples(1);

    // Init smoothed delay on first call
    if (smoothDelayL < 1.0f) smoothDelayL = targetDelayL;
    if (smoothDelayR < 1.0f) smoothDelayR = targetDelayR;

    // ── Feedback gain (quadratic curve, capped at 0.95) ─────
    float fbk = feedback * feedback * 0.95f;

    // ── Step 1: Delay processing (per-sample) ───────────────
    for (int s = 0; s < numSamples; ++s)
    {
        // Smooth delay time (slew to avoid clicks)
        smoothDelayL += (targetDelayL - smoothDelayL) * 0.0005f;
        smoothDelayR += (targetDelayR - smoothDelayR) * 0.0005f;

        // Left channel
        float inL = (numChannels > 0) ? buffer.getSample(0, s) : 0.0f;
        float delayedL = readDelay(delayL.data(), delayWriteL, smoothDelayL);
        delayL[delayWriteL] = std::tanh(inL + delayedL * fbk);
        delayWriteL = (delayWriteL + 1) % MAX_DELAY_SAMPLES;
        wetBuffer.setSample(0, s, delayedL);

        // Right channel (use mono input if only 1 channel)
        {
            float inR = (numChannels > 1) ? buffer.getSample(1, s) : inL;
            float delayedR = readDelay(delayR.data(), delayWriteR, smoothDelayR);
            delayR[delayWriteR] = std::tanh(inR + delayedR * fbk);
            delayWriteR = (delayWriteR + 1) % MAX_DELAY_SAMPLES;
            wetBuffer.setSample(1, s, delayedR);
        }
    }

    // ── Step 2: Reverb (block-based, always stereo on wetBuffer) ─
    // wetBuffer is always 2 channels even with mono input
    reverb.processStereo(wetBuffer.getWritePointer(0),
                         wetBuffer.getWritePointer(1),
                         numSamples);

    // ── Step 3: Subtle tape saturation on reverb output ─────
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* wet = wetBuffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            wet[s] = std::tanh(wet[s] * 1.15f) / 1.15f;
    }

    // ── Step 4: Animated bandpass sweep (per-sample) ────────
    // Sweep frequency range: 200 Hz to 600 Hz
    constexpr float FREQ_LOW  = 200.0f;
    constexpr float FREQ_HIGH = 600.0f;

    // Q from TONE (quadratic: gentle at low, resonant at high)
    float q = 0.5f + tone * tone * 5.0f;   // 0.5 → 5.5

    // Sweep rate
    float sweepHz = getSweepRateHz();
    double lfoInc  = (double)sweepHz / sampleRate;

    // PPQ-locked phase (when available)
    bool usePPQ = hasPPQ && hostPlaying && hostBPM > 20.0;
    double sweepPeriodBeats = getSweepPeriodBeats();

    // Per-sample tracking for UI (updated at end)
    float lastFreqL = FREQ_LOW;

    for (int s = 0; s < numSamples; ++s)
    {
        // ── LFO phase ───────────────────────────────────
        double currentPhase;
        if (usePPQ)
        {
            // Derive phase directly from playhead position
            double sampleBeatOffset = (double)s * (hostBPM / (60.0 * sampleRate));
            double currentBeat = ppqPosition + sampleBeatOffset;
            currentPhase = std::fmod(currentBeat / sweepPeriodBeats, 1.0);
            if (currentPhase < 0.0) currentPhase += 1.0;
        }
        else
        {
            currentPhase = lfoPhase;
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        }

        // Sine LFO → 0–1 range
        float lfoVal  = (float)(std::sin(currentPhase * juce::MathConstants<double>::twoPi));
        float lfoNorm = (lfoVal + 1.0f) * 0.5f;    // 0–1

        // ── Center frequencies (L and R with spread offset) ─
        float centerL = FREQ_LOW + (FREQ_HIGH - FREQ_LOW) * lfoNorm;

        // R channel: phase offset from SPREAD (up to 180°)
        double phaseR = currentPhase + (double)spread * 0.5;
        if (phaseR >= 1.0) phaseR -= 1.0;
        float lfoNormR = ((float)std::sin(phaseR * juce::MathConstants<double>::twoPi) + 1.0f) * 0.5f;
        float centerR = FREQ_LOW + (FREQ_HIGH - FREQ_LOW) * lfoNormR;

        lastFreqL = centerL;

        // ── Apply bandpass and blend with depth ─────────
        if (depth > 0.001f)
        {
            // Left
            float wetL = wetBuffer.getSample(0, s);
            float bpL  = svfL.processBandpass(wetL, centerL, q, sampleRate);
            // Boost bandpass output to compensate for narrow Q energy loss
            bpL *= (1.0f + q * 0.4f);
            wetBuffer.setSample(0, s, wetL * (1.0f - depth) + bpL * depth);

            // Right
            if (numChannels > 1)
            {
                float wetR = wetBuffer.getSample(1, s);
                float bpR  = svfR.processBandpass(wetR, centerR, q, sampleRate);
                bpR *= (1.0f + q * 0.4f);
                wetBuffer.setSample(1, s, wetR * (1.0f - depth) + bpR * depth);
            }
        }
    }

    // ── Step 5: Mix dry/wet + DC-blocking HP ────────────────
    // Handle mono-to-stereo: if input is mono but we have a stereo bus,
    // process both channels using the mono input as dry for both.
    const int outChannels = buffer.getNumChannels();
    const int mixChannels = std::min(outChannels, 2);

    float rms = 0.0f;
    for (int ch = 0; ch < mixChannels; ++ch)
    {
        // For mono input, use channel 0 as dry source for both L and R
        const int dryCh = (ch < numChannels) ? ch : 0;
        const float* dry = buffer.getReadPointer(dryCh);
        // Wet buffer always has stereo from reverb processing
        const int wetCh = std::min(ch, wetBuffer.getNumChannels() - 1);
        const float* wet = wetBuffer.getReadPointer(wetCh);
        float* out = buffer.getWritePointer(ch);
        float& hpState = (ch == 0) ? hpStateL : hpStateR;

        for (int s = 0; s < numSamples; ++s)
        {
            float mixed = dry[s] * (1.0f - mix) + wet[s] * mix;

            // DC-blocking high-pass (1-pole)
            float hpOut = mixed - hpState;
            hpState = mixed - hpCoeff * hpOut;
            out[s] = hpOut;

            rms += out[s] * out[s];
        }
    }

    // ── Update UI atomics ───────────────────────────────────
    rms = std::sqrt(rms / (float)(numSamples * std::max(numChannels, 1)));
    rmsLevel.store(rms);
    sweepFreq.store(lastFreqL);
    sweepPos.store((lastFreqL - FREQ_LOW) / (FREQ_HIGH - FREQ_LOW));
}

// ═════════════════════════════════════════════════════════════════
// Delay time calculation
// ═════════════════════════════════════════════════════════════════
float GhostEngine::getDelayTimeSamples(int channel) const
{
    float samples;

    if (hostBPM > 20.0)
    {
        // Tempo-synced: quantize to nearest subdivision
        int idx = juce::jlimit(0, NUM_SUBS - 1, (int)(time * (float)NUM_SUBS));
        float beats = delaySubs[idx];
        double secondsPerBeat = 60.0 / hostBPM;
        samples = (float)(beats * secondsPerBeat * sampleRate);
    }
    else
    {
        // Free-running: 20ms → 2000ms (exponential)
        float ms = 20.0f * std::pow(100.0f, time);
        samples = ms * 0.001f * (float)sampleRate;
    }

    // Stereo spread: R channel slightly longer (Haas-style widening)
    if (channel == 1)
        samples *= (1.0f + spread * 0.02f);   // Up to 2% offset

    return std::clamp(samples, 1.0f, (float)(MAX_DELAY_SAMPLES - 1));
}

// ═════════════════════════════════════════════════════════════════
// Sweep rate calculation
// ═════════════════════════════════════════════════════════════════
double GhostEngine::getSweepPeriodBeats() const
{
    // Exponential: 32 beats (8 bars, slow) → 0.5 beats (1/8 note, fast)
    // rate=0 → 32, rate=0.5 → 4, rate=1 → 0.5
    return 32.0 * std::pow(0.015625, (double)rate);
}

float GhostEngine::getSweepRateHz() const
{
    double periodBeats = getSweepPeriodBeats();

    if (hostBPM > 20.0)
    {
        double secondsPerBeat = 60.0 / hostBPM;
        return (float)(1.0 / (periodBeats * secondsPerBeat));
    }
    else
    {
        // Free-running: 0.05 Hz → 8 Hz
        return (float)(0.05 * std::pow(160.0, (double)rate));
    }
}

// ═════════════════════════════════════════════════════════════════
// Reverb parameter mapping
// ═════════════════════════════════════════════════════════════════
void GhostEngine::updateReverbParams()
{
    juce::Reverb::Parameters params;
    params.roomSize   = 0.3f + decay * 0.69f;           // 0.3 → 0.99
    params.damping    = 1.0f - (tone * 0.7f);            // 1.0 (dark) → 0.3 (bright)
    params.wetLevel   = 1.0f;                             // Full wet (external mix)
    params.dryLevel   = 0.0f;
    params.width      = 0.3f + spread * 0.7f;            // 0.3 → 1.0
    params.freezeMode = 0.0f;
    reverb.setParameters(params);
}

// ═════════════════════════════════════════════════════════════════
// Delay line read with linear interpolation
// ═════════════════════════════════════════════════════════════════
float GhostEngine::readDelay(const float* buf, int writePos, float delaySamples)
{
    float readPos = (float)writePos - delaySamples;
    if (readPos < 0.0f) readPos += (float)MAX_DELAY_SAMPLES;

    int idx0 = (int)readPos;
    int idx1 = (idx0 + 1) % MAX_DELAY_SAMPLES;
    float frac = readPos - (float)idx0;

    return buf[idx0 % MAX_DELAY_SAMPLES] * (1.0f - frac)
         + buf[idx1] * frac;
}
