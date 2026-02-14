cmake_minimum_required(VERSION 3.22)
project(Intersect VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE JUCE)

juce_add_plugin(Intersect
    COMPANY_NAME "Intersect"
    PLUGIN_MANUFACTURER_CODE Intx
    PLUGIN_CODE Ixsr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "INTERSECT"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(Intersect PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/audio/SampleData.cpp
    src/audio/SliceManager.cpp
    src/audio/VoicePool.cpp
    src/audio/WsolaEngine.cpp
    src/audio/LazyChopEngine.cpp
    src/params/ParamLayout.cpp
    src/ui/TuckersLookAndFeel.cpp
    src/ui/HeaderBar.cpp
    src/ui/SliceControlBar.cpp
    src/ui/WaveformView.cpp
    src/ui/WaveformCache.cpp
    src/ui/ScrollZoomBar.cpp
    src/ui/SliceLane.cpp
    src/ui/ActionPanel.cpp
)

target_include_directories(Intersect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/../signalsmith-stretch/signalsmith-linear
)

target_compile_definitions(Intersect PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Intersect
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
