cmake_minimum_required(VERSION 3.22)
project(Intersect VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE JUCE)

# --- Bungee (built as static library) ---
set(BUNGEE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../bungee)

# pffft (C library used by Bungee)
add_library(pffft STATIC EXCLUDE_FROM_ALL
    ${BUNGEE_ROOT}/submodules/pffft/pffft.c
    ${BUNGEE_ROOT}/submodules/pffft/fftpack.c
)
if (MSVC)
    target_compile_options(pffft PRIVATE /fp:fast)
    target_compile_definitions(pffft PRIVATE _USE_MATH_DEFINES)
else()
    target_compile_options(pffft PRIVATE -ffast-math -fno-finite-math-only -fno-exceptions)
endif()

# Bungee library
file(GLOB BUNGEE_SOURCES "${BUNGEE_ROOT}/src/*.cpp")
add_library(bungee_lib STATIC EXCLUDE_FROM_ALL ${BUNGEE_SOURCES})
target_include_directories(bungee_lib PRIVATE
    ${BUNGEE_ROOT}/submodules/eigen
    ${BUNGEE_ROOT}/submodules
    ${BUNGEE_ROOT}
    $<$<CXX_COMPILER_ID:MSVC>:${CMAKE_CURRENT_SOURCE_DIR}/src/audio/msvc_compat>
)
target_include_directories(bungee_lib PUBLIC ${BUNGEE_ROOT})
target_compile_definitions(bungee_lib PRIVATE
    BUNGEE_SELF_TEST=0
    eigen_assert=BUNGEE_ASSERT1
    EIGEN_DONT_PARALLELIZE=1
    BUNGEE_VERSION="0.0.0"
)
if (MSVC)
    target_compile_options(bungee_lib PRIVATE /W0
        /FI${CMAKE_CURRENT_SOURCE_DIR}/src/audio/bungee_msvc_compat.h)
    target_compile_definitions(bungee_lib PRIVATE _USE_MATH_DEFINES)
else()
    target_compile_options(bungee_lib PRIVATE -fwrapv -w)
endif()
target_link_libraries(bungee_lib PRIVATE pffft)

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
    src/ui/IntersectLookAndFeel.cpp
    src/ui/HeaderBar.cpp
    src/ui/SliceControlBar.cpp
    src/ui/WaveformView.cpp
    src/ui/WaveformCache.cpp
    src/ui/ScrollZoomBar.cpp
    src/ui/SliceLane.cpp
    src/ui/ActionPanel.cpp
)

target_include_directories(Intersect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/../signalsmith-linear
    ${BUNGEE_ROOT}
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
        bungee_lib
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
