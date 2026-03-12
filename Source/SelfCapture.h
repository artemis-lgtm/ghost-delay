#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * SelfCapture — lets the plugin save a screenshot of itself.
 * No macOS permissions required since we're rendering our own component.
 * Captures at 2x scale for Retina-quality output.
 */
class SelfCapture
{
public:
    static void capture(juce::Component* editor, const juce::String& path = "/tmp/ghost_screenshot.png")
    {
        if (editor == nullptr) return;
        
        // Capture at 2x for Retina-quality screenshots
        auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        
        juce::File file(path);
        file.deleteFile();
        
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream(snapshot, stream);
        }
    }
};
