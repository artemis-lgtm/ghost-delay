#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>
#include <cmath>

// ============================================================================
// CRT-style analog signal-warp engine (v1, 2026-06-29) — council-verified +
// Leto code-review fixes applied. Param plane renamed 2026-07-02
// (audit vs Cymatics CRT reference): setters now carry their real names —
//   AMOUNT RATE FILTER MIX CRUSH NOISE WIDTH DRIVE. No remap layer.
// Whole nonlinear chain runs 4x oversampled (anti-alias). Chain per OS-sample:
// HF-first asymmetric tape sat -> DC block -> wow/flutter varispeed (cents,
// cubic) -> pink signal-noise + hum -> bandwidth EQ (HPF/LPF/head bump) ->
// envelope TPT-SVF LPF (keyed off dry, HF-first dropouts) -> band-limited
// dithered crush + auto-gain -> level wander -> DC block. Then equal-power MIX
// (dry delay-compensated) -> M/S width (post-mix, capped).
// (XY movement layer + POWER bypass are UI-coupled -> design pass.)
// ============================================================================
class GhostEngine
{
public:
    GhostEngine() = default;
    ~GhostEngine() = default;

    void prepare(double sr, int blockSize)
    {
        sampleRate = sr;
        os = std::make_unique<juce::dsp::Oversampling<float>>(
                 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        os->initProcessing((size_t) juce::jmax(1, blockSize));
        osSr = sr * (double) os->getOversamplingFactor();

        const double rampOS = 0.015;  // 15 ms param ramp at OS rate
        sgAmount.reset(osSr, rampOS); sgRate.reset(osSr, rampOS);
        sgFilter.reset(osSr, rampOS); sgMix.reset(osSr, rampOS);
        sgCrush.reset(osSr, rampOS);  sgNoise.reset(osSr, rampOS);
        sgWidth.reset(osSr, rampOS);  sgDrive.reset(osSr, rampOS);
        reset();
    }

    void reset()
    {
        if (os) os->reset();
        for (int i = 0; i < 2; ++i) chans[i].reset(osSr, i);
        rmsLevel.store(0.0f);
    }

    void setAmount(float v)   { sgAmount.setTargetValue(v); }
    void setRate(float v)  { sgRate.setTargetValue(v); }
    void setFilter(float v)   { sgFilter.setTargetValue(v); }
    void setMix(float v)    { sgMix.setTargetValue(v); }
    void setCrush(float v){ sgCrush.setTargetValue(v); }
    void setNoise(float v)   { sgNoise.setTargetValue(v); }
    void setWidth(float v)  { sgWidth.setTargetValue(v); }
    void setDrive(float v)   { sgDrive.setTargetValue(v); }

    float getCurrentRMSLevel() const { return rmsLevel.load(); }

    void process(juce::AudioBuffer<float>& buffer, juce::AudioPlayHead*)
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin(buffer.getNumChannels(), 2);
        if (numCh == 0 || os == nullptr) return;

        juce::dsp::AudioBlock<float> block(buffer);
        auto up = os->processSamplesUp(block);           // 4x
        const int n = (int) up.getNumSamples();
        const int upCh = (int) up.getNumChannels();
        double acc = 0.0;

        for (int i = 0; i < n; ++i)
        {
            amount = sgAmount.getNextValue(); rate  = sgRate.getNextValue();
            filter = sgFilter.getNextValue(); mix   = sgMix.getNextValue();
            crush  = sgCrush.getNextValue();  noise = sgNoise.getNextValue();
            width  = sgWidth.getNextValue();  drive = sgDrive.getNextValue();

            float l = up.getSample(0, i);
            float r = upCh > 1 ? up.getSample(1, i) : l;

            float wl = chans[0].process(l, *this, 0);
            float wr = upCh > 1 ? chans[1].process(r, *this, 1) : wl;

            float dl = chans[0].dryAligned(l);
            float dr = upCh > 1 ? chans[1].dryAligned(r) : dl;
            const float wetG = std::sin(mix * juce::MathConstants<float>::halfPi);
            const float dryG = std::cos(mix * juce::MathConstants<float>::halfPi);
            float ol = dl * dryG + wl * wetG;
            float orr = dr * dryG + wr * wetG;

            // M/S width AFTER the mix, side capped at unity (fix [16])
            float mid = 0.5f * (ol + orr), side = 0.5f * (ol - orr) * juce::jmin(1.0f, width * 1.5f);
            ol = mid + side; orr = mid - side;

            up.setSample(0, i, ol);
            if (upCh > 1) up.setSample(1, i, orr);
            acc += (double) ol * ol;
        }

        os->processSamplesDown(block);                   // back to base rate
        rmsLevel.store((float) std::sqrt(acc / juce::jmax(1, n)));
    }

private:
    double sampleRate = 44100.0, osSr = 176400.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::SmoothedValue<float> sgAmount { 0.4f }, sgRate { 0.3f }, sgFilter { 0.6f }, sgMix { 0.35f };
    juce::SmoothedValue<float> sgCrush { 0.0f }, sgNoise { 0.2f }, sgWidth { 0.667f }, sgDrive { 0.3f };
    float amount = 0.4f, rate = 0.3f, filter = 0.6f, mix = 0.35f;
    float crush = 0.0f, noise = 0.2f, width = 0.667f, drive = 0.3f;
    std::atomic<float> rmsLevel { 0.0f };

    struct Channel
    {
        static constexpr int SZ = 1 << 16;          // 65536, headroom for OS-rate delays
        static constexpr int MASK = SZ - 1;
        std::vector<float> buf, dry;
        int wp = 0, dwp = 0, baseDelay = 2000;
        float wowPh = 0, flutPh = 0, scrapePh = 0, humPh = 0, wanderPh = 0;
        float rnd = 0, rndTarget = 0; int rndCnt = 0;
        float peState = 0, satMem = 0;              // pre-emphasis + sat memory
        float dcs1X = 0, dcs1Y = 0, dcs2X = 0, dcs2Y = 0;  // two DC blockers
        float hp = 0, head = 0, lp = 0;            // bandwidth EQ states
        float pinkA = 0, pinkB = 0;                // pink noise filter
        float env = 0;                             // envelope follower (dry)
        float ic1 = 0, ic2 = 0;                    // TPT SVF integrators
        float clpre = 0, clpost = 0;               // crush band-limit one-poles
        float holdV = 0; int holdPos = 0;
        float dropEnv = 1.0f, dropTarget = 1.0f; int dropCnt = 0;
        double sr = 176400.0;
        juce::Random rng;

        void reset(double sampRate, int ch)
        {
            sr = sampRate;
            buf.assign(SZ, 0.0f); dry.assign(SZ, 0.0f);
            wp = dwp = 0;
            wowPh = 0.13f * (float) ch; flutPh = 0.41f * (float) ch; scrapePh = 0; humPh = 0; wanderPh = 0.5f * (float) ch;
            rnd = rndTarget = 0; rndCnt = 0;
            peState = satMem = 0; dcs1X = dcs1Y = dcs2X = dcs2Y = 0;
            hp = head = lp = 0; pinkA = pinkB = 0; env = 0; ic1 = ic2 = 0;
            clpre = clpost = 0; holdV = 0; holdPos = 0; dropEnv = dropTarget = 1.0f; dropCnt = 0;
            baseDelay = (int) (0.012 * sr);                       // ~12ms, SR-relative
            rng.setSeed((juce::int64) (ch * 2654435761LL + 1));   // independent per channel
        }

        inline float cubic(float delay) const
        {
            float rpf = (float) wp - delay;
            while (rpf < 0) rpf += SZ;
            int i1 = (int) rpf; float f = rpf - (float) i1;
            int i0 = (i1 - 1) & MASK, i2 = (i1 + 1) & MASK, i3 = (i1 + 2) & MASK; i1 &= MASK;
            float a = buf[(size_t) i0], b = buf[(size_t) i1], c = buf[(size_t) i2], d = buf[(size_t) i3];
            return b + 0.5f * f * (c - a + f * (2*a - 5*b + 4*c - d + f * (3*(b - c) + d - a)));
        }

        float dryAligned(float in)
        {
            float out = dry[(size_t) ((dwp - baseDelay + SZ) & MASK)];
            dry[(size_t) dwp] = in; dwp = (dwp + 1) & MASK;
            return out;
        }

        static inline float onepoleCoeff(float hz, float sr) { return 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / sr); }

        float process(float in, GhostEngine& e, int ch)
        {
            const float fsr = (float) sr;
            const float tp = juce::MathConstants<float>::twoPi;

            // 1) tape saturation: HF pre-emphasis -> asymmetric shaper -> de-emphasis (fix [8])
            float dr = 1.0f + e.drive * 6.0f;
            float peC = onepoleCoeff(1500.0f, fsr);
            peState += peC * (in - peState);              // LF content
            float hfBoosted = in + 0.8f * (in - peState); // emphasize highs pre-sat
            float pre = hfBoosted * dr;
            float shaped = std::tanh(pre + 0.15f * pre * pre * e.drive - 0.25f * satMem); // asym (even harmonics) + memory
            satMem = 0.5f * satMem + 0.5f * shaped;
            float sat = shaped / (0.5f + 0.5f * dr);
            sat -= 0.6f * (sat - peState);                // gentle de-emphasis
            // DC block right after the asymmetric sat (fix [14])
            float dcCoeff = std::exp(-tp * 8.0f / fsr);
            float s1 = sat - dcs1X + dcCoeff * dcs1Y; dcs1X = sat; dcs1Y = s1; sat = s1;

            // 2) wow/flutter varispeed (cents-based, cubic, decorrelated)
            float det = 1.0f + 0.004f * (float) ch;
            float wowHz  = (0.3f + e.rate * 5.7f)  * det;
            float flutHz = (6.0f + e.rate * 14.0f) * det;
            const float scrapeHz = 35.0f;
            wowPh   += wowHz   / fsr; if (wowPh   >= 1) wowPh   -= 1;
            flutPh  += flutHz  / fsr; if (flutPh  >= 1) flutPh  -= 1;
            scrapePh += scrapeHz / fsr; if (scrapePh >= 1) scrapePh -= 1;
            if (--rndCnt <= 0) { rndTarget = rng.nextFloat() * 2 - 1; rndCnt = (int) (fsr * 0.02f); }
            rnd += 0.0008f * (rndTarget - rnd);
            float wow = std::sin(tp * wowPh), flut = std::sin(tp * flutPh), scr = std::sin(tp * scrapePh);
            auto c2r = [](float c) { return std::exp2(c / 1200.0f) - 1.0f; };
            float Awow   = c2r(e.amount * 25.0f) * fsr / (tp * wowHz);
            float Aflut  = c2r(e.amount * 7.0f)  * fsr / (tp * flutHz);
            float Adrift = c2r(e.amount * 6.0f)  * fsr / (tp * juce::jmax(0.1f, wowHz));
            float Ascr   = c2r(e.amount * 0.8f)  * fsr / (tp * scrapeHz);
            float dep = Awow * wow + Aflut * flut + Adrift * rnd + Ascr * scr;
            buf[(size_t) wp] = sat;
            float delay = juce::jlimit(2.0f, (float) (SZ - 4), (float) baseDelay + dep);
            float warped = cubic(delay);
            wp = (wp + 1) & MASK;

            // 3) pink, signal-modulated hiss + mains hum, injected INTO the medium (pre-band) (fix [12])
            float white = rng.nextFloat() * 2 - 1;
            pinkA = 0.99765f * pinkA + white * 0.0990460f;   // Paul Kellet pink (2-pole subset)
            pinkB = 0.96300f * pinkB + white * 0.2965164f;
            float pink = (pinkA + pinkB + white * 0.1848f) * 0.2f;
            humPh += 60.0f / fsr; if (humPh >= 1) humPh -= 1;
            float hum = 0.0007f * (std::sin(tp * humPh) + 0.4f * std::sin(tp * 2 * humPh) + 0.2f * std::sin(tp * 3 * humPh));
            float envN = juce::jlimit(0.0f, 1.0f, env * 3.0f);
            warped += e.noise * 0.05f * pink * (0.4f + 0.6f * envN) + hum;   // hum independent of NOISE knob

            // 4) bandwidth EQ: HPF ~40, head bump ~90, LPF ~9k
            float hpC = onepoleCoeff(40.0f, fsr);  hp = hp + hpC * (warped - hp);
            float hpd = warped - hp;
            float hdC = onepoleCoeff(90.0f, fsr);  head = head + hdC * (hpd - head);
            float eqd = hpd + 0.45f * head;
            float lpC = onepoleCoeff(9000.0f, fsr); lp = lp + lpC * (eqd - lp);
            float band = lp;

            // 5) envelope follower keyed off DRY (fast attack, slow release)
            float rect = std::fabs(in);
            float aC = rect > env ? (1 - std::exp(-1.0f / (0.002f * fsr)))
                                  : (1 - std::exp(-1.0f / (0.08f  * fsr)));
            env += aC * (rect - env);

            // HF-first dropouts: randomize duration 5-80ms, dip the cutoff not the gain (fix [13])
            if (--dropCnt <= 0)
            {
                bool drop = rng.nextFloat() < 0.04f;
                dropTarget = drop ? 0.45f : 1.0f;
                float ms = 5.0f + rng.nextFloat() * 75.0f;
                dropCnt = (int) (fsr * ms / 1000.0f);
            }
            dropEnv += 0.01f * (dropTarget - dropEnv);

            // 6) TPT state-variable LPF (FILTER base cut, env chokes it, dropouts pull HF) (fix [10])
            float breathe = 0.4f + 0.4f * (1.0f - e.filter);    // FILTER scales breathing depth (fix [10])
            float cut = (400.0f + e.filter * 7000.0f) * (1.0f - breathe * envN) * dropEnv;
            cut = juce::jlimit(40.0f, juce::jmin(0.45f * fsr, 16000.0f), cut);
            float g = std::tan(juce::MathConstants<float>::pi * cut / fsr);
            float k = 0.8f;
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1, a3 = g * a2;
            float v3 = band - ic2;
            float v1 = a1 * ic1 + a2 * v3;
            float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            float filt = v2;

            // 7) crush: band-limit pre + dithered hold + post band-limit + auto-gain (fix [11])
            if (e.crush > 0.001f)
            {
                float blC = onepoleCoeff(juce::jlimit(1000.0f, 16000.0f, 16000.0f - e.crush * 13000.0f), fsr);
                clpre += blC * (filt - clpre);
                int hold = 1 + (int) (e.crush * 32.0f * (fsr / 44100.0f));
                if (holdPos <= 0) { holdV = clpre; holdPos = hold; }
                holdPos--;
                float bits = 16.0f - e.crush * 12.0f;
                float lv = std::pow(2.0f, bits);
                float dith = (rng.nextFloat() - 0.5f) / lv;
                float q = std::round((holdV + dith) * lv) / lv;
                clpost += blC * (q - clpost);
                filt = clpost * (1.0f + 0.15f * e.crush);     // auto-gain makeup
            }

            // 8) slow level wander (the other half of "filter") (fix [13])
            wanderPh += 0.2f / fsr; if (wanderPh >= 1) wanderPh -= 1;
            filt *= 1.0f + 0.04f * std::sin(tp * wanderPh);

            // 9) final DC block
            float out = filt - dcs2X + dcCoeff * dcs2Y; dcs2X = filt; dcs2Y = out;
            return out;
        }
    };
    Channel chans[2];
};
