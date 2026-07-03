// Renders the Ghost Machine editor offscreen to a PNG (no window server capture needed).
// Mirrors the SaladSnapshot tool in stutter3.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GhostDelayProcessor processor;

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(471, 615);

    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);

    juce::File out(argc > 1 ? juce::String(argv[1])
                            : juce::String("/tmp/ghost_machine_ui.png"));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (!stream.openedOk())
        return 1;

    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream(image, stream);
    editor.reset();
    return ok ? 0 : 1;
}
