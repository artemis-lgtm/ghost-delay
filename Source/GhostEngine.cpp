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

    // SVFs (bandpass + lowpass)
    svfL.reset();
    svfR.reset();
    lpfL.reset();
    lpfR.reset();

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
    lpfL.reset();
    lpfR.reset();
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

    // ── Step 2: Reverb (block-based, always stereo) ─────────
    reverb.processStereo(wetBuffer.getWritePointer(0),
                         wetBuffer.getWritePointer(1),
                         numSamples);

    // ── Step 3: Subtle tape saturation on reverb output ─────
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* wet = wetBuffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            wet[s] = std::tanh(wet[s] * 1.15f) / 1.15f;
    }

    // ── Step 3.5: Global low-pass filter (TONE) ─────────────
    // TONE controls wet signal LP cutoff: 200 Hz (fully dark) → 2000 Hz (less dark)
    // Never fully bright — everything stays submerged
    float lpCutoff = 200.0f + tone * 1800.0f;   // 200 Hz → 2000 Hz
    float lpQ = 0.707f;   // Butterworth-ish, no resonance — smooth rolloff

    for (int s = 0; s < numSamples; ++s)
    {
        float wetL = wetBuffer.getSample(0, s);
        float wetR = wetBuffer.getSample(1, s);
        wetBuffer.setSample(0, s, lpfL.processLowpass(wetL, lpCutoff, lpQ, sampleRate));
        wetBuffer.setSample(1, s, lpfR.processLowpass(wetR, lpCutoff, lpQ, sampleRate));
    }

    // ── Step 4: Animated bandpass sweep (per-sample) ────────
    // Underwater sweep range: 100 Hz to 400 Hz (deep, murky)
    constexpr float FREQ_LOW  = 100.0f;
    constexpr float FREQ_HIGH = 400.0f;

    // Q: gentle resonance — enough to hear the sweep, not piercing
    // Wider Q than before since the LP already darkens everything
    float q = 0.7f + tone * tone * 3.0f;   // 0.7 → 3.7

    // Sweep rate
    float sweepHz = getSweepRateHz();
    double lfoInc  = (double)sweepHz / sampleRate;

    // PPQ-locked phase (when available)
    bool usePPQ = hasPPQ && hostPlaying && hostBPM > 20.0;
    double sweepPeriodBeats = getSweepPeriodBeats();

    // Per-sample tracking for UI
    float lastFreqL = FREQ_LOW;

    for (int s = 0; s < numSamples; ++s)
    {
        // ── LFO phase ───────────────────────────────────
        double currentPhase;
        if (usePPQ)
        {
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
        float lfoNorm = (lfoVal + 1.0f) * 0.5f;

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
            bpL *= (1.0f + q * 0.5f);   // Compensate for BP energy loss
            wetBuffer.setSample(0, s, wetL * (1.0f - depth) + bpL * depth);

            // Right
            {
                float wetR = wetBuffer.getSample(1, s);
                float bpR  = svfR.processBandpass(wetR, centerR, q, sampleRate);
                bpR *= (1.0f + q * 0.5f);
                wetBuffer.setSample(1, s, wetR * (1.0f - depth) + bpR * depth);
            }
        }
    }

    // ── Step 5: Mix dry/wet + DC-blocking HP ────────────────
    const int outChannels = buffer.getNumChannels();
    const int mixChannels = std::min(outChannels, 2);

    float rms = 0.0f;
    for (int ch = 0; ch < mixChannels; ++ch)
    {
        const int dryCh = (ch < numChannels) ? ch : 0;
        const float* dry = buffer.getReadPointer(dryCh);
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
        int idx = juce::jlimit(0, NUM_SUBS - 1, (int)(time * (float)NUM_SUBS));
        float beats = delaySubs[idx];
        double secondsPerBeat = 60.0 / hostBPM;
        samples = (float)(beats * secondsPerBeat * sampleRate);
    }
    else
    {
        float ms = 20.0f * std::pow(100.0f, time);
        samples = ms * 0.001f * (float)sampleRate;
    }

    if (channel == 1)
        samples *= (1.0f + spread * 0.02f);

    return std::clamp(samples, 1.0f, (float)(MAX_DELAY_SAMPLES - 1));
}

// ═════════════════════════════════════════════════════════════════
// Sweep rate calculation — SLOWER for underwater feel
// ═════════════════════════════════════════════════════════════════
double GhostEngine::getSweepPeriodBeats() const
{
    // Slower range: 64 beats (16 bars, glacial) → 2 beats (1/2 note, moderate)
    // rate=0 → 64 beats, rate=0.5 → ~11 beats, rate=1 → 2 beats
    return 64.0 * std::pow(0.03125, (double)rate);
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
        // Free-running: 0.02 Hz → 4 Hz (slower range)
        return (float)(0.02 * std::pow(200.0, (double)rate));
    }
}

// ═════════════════════════════════════════════════════════════════
// Reverb parameter mapping — HEAVIER damping for underwater
// ═════════════════════════════════════════════════════════════════
void GhostEngine::updateReverbParams()
{
    juce::Reverb::Parameters params;
    params.roomSize   = 0.4f + decay * 0.59f;            // 0.4 → 0.99 (larger base room)
    params.damping    = 0.7f + (1.0f - tone) * 0.29f;    // 0.7 → 0.99 (always heavy damping)
    params.wetLevel   = 1.0f;
    params.dryLevel   = 0.0f;
    params.width      = 0.3f + spread * 0.7f;
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
