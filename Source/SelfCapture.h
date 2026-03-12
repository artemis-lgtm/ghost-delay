#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * SelfCapture — lets the plugin save a screenshot of itself.
 * No macOS permissions required since we're rendering our own component.
 * Call capture() with the editor component to save to /tmp/ghost_screenshot.png
 */
class SelfCapture
{
public:
    static void capture(juce::Component* editor, const juce::String& path = "/tmp/ghost_screenshot.png")
    {
        if (editor == nullptr) return;
        
        auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
        
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
