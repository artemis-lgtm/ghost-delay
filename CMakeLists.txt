cmake_minimum_required(VERSION 3.22)
project(GhostDelay VERSION 7.0.0)

# Find JUCE (expects JUCE installed or fetched)
find_package(JUCE CONFIG QUIET)
if(NOT JUCE_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(GhostDelay
    COMPANY_NAME "GhostAudio"
    PLUGIN_MANUFACTURER_CODE Ghst
    PLUGIN_CODE GcrT
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Ghost CRT"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(GhostDelay
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/GhostEngine.cpp
        Source/GhostEngine.h
        Source/GhostRenderer.cpp
        Source/GhostRenderer.h
        Source/FilmstripKnob.cpp
        Source/FilmstripKnob.h
        Source/WaveDisplay.h
)

# Binary resources from Blender renders
juce_add_binary_data(GhostDelayData
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        Resources/background.png
        Resources/ghost_spritesheet.png
        Resources/spec_sheet.png
        Resources/knob_DIR.png
        Resources/knob_ENV.png
        Resources/knob_FDBK.png
        Resources/knob_FREEZE.png
        Resources/knob_MIX.png
        Resources/knob_SPREAD.png
        Resources/knob_TILT.png
        Resources/knob_TIME.png
        Resources/led_on.png
        Resources/led_off.png
)

target_link_libraries(GhostDelay
    PRIVATE
        GhostDelayData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

target_compile_definitions(GhostDelay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Offscreen UI snapshot tool (dev only — renders the editor to a PNG)
juce_add_console_app(GhostSnapshot PRODUCT_NAME "GhostSnapshot")
target_sources(GhostSnapshot PRIVATE
    Tools/SnapshotMain.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GhostEngine.cpp
    Source/GhostRenderer.cpp
    Source/FilmstripKnob.cpp
)
target_link_libraries(GhostSnapshot PRIVATE
    GhostDelayData
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
)
target_compile_definitions(GhostSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# Offline engine stress test (clicks / blowout regression)
juce_add_console_app(GhostEngineTest PRODUCT_NAME "GhostEngineTest")
target_sources(GhostEngineTest PRIVATE
    Tests/EngineStressTest.cpp
    Source/GhostEngine.cpp
)
target_link_libraries(GhostEngineTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
)
target_compile_definitions(GhostEngineTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# Offline demo renderer (dry loop + ghost-drag automated wet pass)
juce_add_console_app(GhostDemoRender PRODUCT_NAME "GhostDemoRender")
target_sources(GhostDemoRender PRIVATE
    Tools/DemoRender.cpp
    Source/GhostEngine.cpp
)
target_link_libraries(GhostDemoRender PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
)
target_compile_definitions(GhostDemoRender PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
