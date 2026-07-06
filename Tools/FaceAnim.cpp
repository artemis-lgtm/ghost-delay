// FaceAnim: renders N frames of the live editor with params moving and real
// audio flowing, for the family lineup film. Generic: uses createPluginFilter.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cmath>

// ---- per-pedal config (edited per repo)
#define OUT_DIR "/tmp/faceanim_hl"
#define AUDIO_MODE 0
static const struct { const char* id; float base, amp, hz, phase; } kMoves[] = {
    {"amount", 0.55f, 0.40f, 0.055f, 0.00f},
    {"rate",   0.50f, 0.42f, 0.085f, 0.25f},
    {"filter", 0.60f, 0.20f, 0.030f, 0.50f},
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int fps = 30, frames = 480;
    const int block = (int) (sr / fps);

    auto* proc = createPluginFilter();
    proc->prepareToPlay (sr, block);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setVisible (true);

    juce::File out (OUT_DIR);
    out.deleteRecursively();
    out.createDirectory();

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double phA = 0, phB = 0, phC = 0;
    int step = 0;

    for (int f = 0; f < frames; ++f)
    {
        const float t = f / (float) fps;
        // set params by id via the host parameter list
        for (const auto& m : kMoves)
        {
            for (auto* rp : proc->getParameters())
                if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (rp))
                    if (rap->getParameterID() == m.id)
                        rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                            m.base + m.amp * (float) std::sin (6.28318 * (m.hz * t + m.phase))));
        }

        // audio feed
        buf.clear();
        for (int i = 0; i < block; ++i)
        {
            const double gt = (f * (double) block + i) / sr;
            float s = 0.0f;
#if AUDIO_MODE == 0     // plucks: gentle arpeggio
            {
                const double noteLen = 0.5;
                const int idx = (int) (gt / noteLen);
                const double lt = gt - idx * noteLen;
                static const float hz[] = {220.0f, 261.63f, 329.63f, 392.0f};
                const float h = hz[idx % 4];
                s = (float) (std::exp (-4.0 * lt) * 0.5
                    * (std::sin (6.28318 * h * lt) + 0.3 * std::sin (12.566 * h * lt)));
            }
#elif AUDIO_MODE == 1   // pad: slow chord
            {
                phA += 6.28318 * 196.0 / sr; phB += 6.28318 * 246.9 / sr; phC += 6.28318 * 293.7 / sr;
                s = 0.22f * (float) (std::sin (phA) + std::sin (phB) + std::sin (phC));
            }
#else                   // drums: kick + hat 120bpm
            {
                const double beat = 0.5;
                const int bi = (int) (gt / beat);
                const double bt = gt - bi * beat;
                s = (float) (std::exp (-18.0 * bt) * std::sin (6.28318 * (60.0 + 80.0 * std::exp (-30.0 * bt)) * bt));
                if (bt < 0.03 && (bi % 2 == 1))
                    s += 0.25f * (float) ((rand() / (double) RAND_MAX) * 2 - 1) * (float) std::exp (-60.0 * bt);
                (void) step;
            }
#endif
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        proc->processBlock (buf, midi);
        midi.clear();

        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto snap = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File ff (out.getChildFile (juce::String::formatted ("frame_%04d.png", f)));
        juce::FileOutputStream os (ff);
        juce::PNGImageFormat png;
        png.writeImageToStream (snap, os);
        if (f % 120 == 0) std::printf ("f %d/%d\n", f, frames);
    }
    std::printf ("FACEANIM-DONE %s\n", OUT_DIR);
    ed.reset();
    delete proc;
    return 0;
}
