cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

set(INTERSECT_VERSION "" CACHE STRING "Plugin version (injected by CI, or auto-detected from git tag)")
if(INTERSECT_VERSION STREQUAL "")
    find_package(Git QUIET)
    if(Git_FOUND OR GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v*" --abbrev=0
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_tag
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX REPLACE "^v" "" _git_version "${_git_tag}")
        if(_git_version STREQUAL "")
            set(_git_version "0.0.0")
        endif()
    else()
        set(_git_version "0.0.0")
    endif()
    set(INTERSECT_VERSION "${_git_version}")
endif()
project(Intersect)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(INTERSECT_ORT_VERSION "1.23.2" CACHE STRING "ONNX Runtime version to download")
set(INTERSECT_ORT_ROOT "" CACHE PATH "Path to a prebuilt ONNX Runtime package (skips auto-download)")

# --- Auto-download ONNX Runtime if no custom root is set ---
if(INTERSECT_ORT_ROOT STREQUAL "")
    include(FetchContent)
    if(WIN32)
        set(_ort_archive "onnxruntime-win-x64-${INTERSECT_ORT_VERSION}.zip")
        set(_ort_dir "onnxruntime-win-x64-${INTERSECT_ORT_VERSION}")
    elseif(APPLE)
        set(_ort_archive "onnxruntime-osx-universal2-${INTERSECT_ORT_VERSION}.tgz")
        set(_ort_dir "onnxruntime-osx-universal2-${INTERSECT_ORT_VERSION}")
    else()
        set(_ort_archive "onnxruntime-linux-x64-${INTERSECT_ORT_VERSION}.tgz")
        set(_ort_dir "onnxruntime-linux-x64-${INTERSECT_ORT_VERSION}")
    endif()
    set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${INTERSECT_ORT_VERSION}/${_ort_archive}")

    FetchContent_Declare(onnxruntime
        URL "${_ort_url}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(INTERSECT_ORT_ROOT "${onnxruntime_SOURCE_DIR}")
endif()

set(INTERSECT_HAS_ONNX_RUNTIME OFF)
if(EXISTS "${INTERSECT_ORT_ROOT}/include/onnxruntime_cxx_api.h")
    if(WIN32 AND EXISTS "${INTERSECT_ORT_ROOT}/lib/onnxruntime.lib")
        set(INTERSECT_HAS_ONNX_RUNTIME ON)
    elseif(APPLE AND EXISTS "${INTERSECT_ORT_ROOT}/lib/libonnxruntime.dylib")
        set(INTERSECT_HAS_ONNX_RUNTIME ON)
    elseif(UNIX AND EXISTS "${INTERSECT_ORT_ROOT}/lib/libonnxruntime.so")
        set(INTERSECT_HAS_ONNX_RUNTIME ON)
    endif()
endif()

set(INTERSECT_ORT_RUNTIME_LIBS "")
if(INTERSECT_HAS_ONNX_RUNTIME)
    if(WIN32)
        file(GLOB INTERSECT_ORT_RUNTIME_LIBS
            "${INTERSECT_ORT_ROOT}/*.dll"
            "${INTERSECT_ORT_ROOT}/bin/*.dll"
            "${INTERSECT_ORT_ROOT}/lib/*.dll"
        )
    elseif(APPLE)
        file(GLOB INTERSECT_ORT_RUNTIME_LIBS
            "${INTERSECT_ORT_ROOT}/lib/libonnxruntime*.dylib"
        )
    else()
        file(GLOB INTERSECT_ORT_RUNTIME_LIBS
            "${INTERSECT_ORT_ROOT}/lib/libonnxruntime*.so*"
        )
    endif()

    list(REMOVE_DUPLICATES INTERSECT_ORT_RUNTIME_LIBS)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/JUCE JUCE)

# --- Bungee (built as static library) ---
set(BUNGEE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/bungee)

# pffft (C library used by Bungee)
add_library(pffft STATIC EXCLUDE_FROM_ALL
    ${BUNGEE_ROOT}/submodules/pffft/pffft.c
    ${BUNGEE_ROOT}/submodules/pffft/fftpack.c
)
set_target_properties(pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(pffft PRIVATE /fp:fast)
    target_compile_definitions(pffft PRIVATE _USE_MATH_DEFINES)
else()
    target_compile_options(pffft PRIVATE -ffast-math -fno-finite-math-only -fno-exceptions)
endif()

# Bungee library
file(GLOB BUNGEE_SOURCES "${BUNGEE_ROOT}/src/*.cpp")
add_library(bungee_lib STATIC EXCLUDE_FROM_ALL ${BUNGEE_SOURCES})
set_target_properties(bungee_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
    COMPANY_NAME "tucktuckg00se"
    PLUGIN_MANUFACTURER_CODE Intx
    PLUGIN_CODE Ixsr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "INTERSECT"
    VERSION "${INTERSECT_VERSION}"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_add_binary_data(IntersectFonts SOURCES
    src/ui/fonts/Inter_24pt-Regular.ttf
    src/ui/fonts/Inter_24pt-Bold.ttf
)

target_sources(Intersect PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/StandaloneApp.cpp
    src/audio/SampleData.cpp
    src/audio/SliceManager.cpp
    src/audio/VoicePool.cpp
    src/audio/GrainEngine.cpp
    src/audio/LazyChopEngine.cpp
    src/audio/StemSeparation.cpp
    src/audio/StemModelDownloadJob.cpp
    src/params/ParamLayout.cpp
    src/ui/IntersectLookAndFeel.cpp
    src/ui/HeaderBar.cpp
    src/ui/SampleLane.cpp
    src/ui/SignalChainBar.cpp
    src/ui/WaveformView.cpp
    src/ui/WaveformCache.cpp
    src/ui/ScrollZoomBar.cpp
    src/ui/SliceLane.cpp
    src/ui/ActionPanel.cpp
    src/ui/AutoChopPanel.cpp
    src/ui/StemExportPanel.cpp
    src/audio/StemSeparationJob.cpp
)

target_include_directories(Intersect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-linear
    ${BUNGEE_ROOT}
)

if(INTERSECT_HAS_ONNX_RUNTIME)
    target_include_directories(Intersect PRIVATE "${INTERSECT_ORT_ROOT}/include")
endif()

target_compile_definitions(Intersect PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    INTERSECT_HAS_ONNX_RUNTIME=$<BOOL:${INTERSECT_HAS_ONNX_RUNTIME}>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(CURL REQUIRED)
    target_compile_definitions(Intersect PUBLIC
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_CURL=1
    )
else()
    target_compile_definitions(Intersect PUBLIC JUCE_USE_CURL=0)
endif()

target_link_libraries(Intersect
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        bungee_lib
        IntersectFonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Intersect PRIVATE CURL::libcurl)
endif()

if(INTERSECT_HAS_ONNX_RUNTIME)
    if(WIN32)
        target_link_libraries(Intersect PRIVATE "${INTERSECT_ORT_ROOT}/lib/onnxruntime.lib")
    elseif(APPLE)
        target_link_libraries(Intersect PRIVATE "${INTERSECT_ORT_ROOT}/lib/libonnxruntime.dylib")
    else()
        target_link_libraries(Intersect PRIVATE "${INTERSECT_ORT_ROOT}/lib/libonnxruntime.so")
    endif()
endif()

function(intersect_bundle_onnx_runtime target_name)
    if(NOT TARGET "${target_name}")
        return()
    endif()

    if(WIN32)
        set(_runtime_dest "$<TARGET_FILE_DIR:${target_name}>")
    elseif(APPLE)
        set(_runtime_dest "$<TARGET_FILE_DIR:${target_name}>/../Frameworks")
        set_target_properties("${target_name}" PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "@loader_path/../Frameworks"
        )
    else()
        set(_runtime_dest "$<TARGET_FILE_DIR:${target_name}>")
        set_target_properties("${target_name}" PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            BUILD_RPATH "$ORIGIN"
            INSTALL_RPATH "$ORIGIN"
        )
    endif()

    foreach(runtime_lib IN LISTS INTERSECT_ORT_RUNTIME_LIBS)
        if(EXISTS "${runtime_lib}")
            add_custom_command(TARGET "${target_name}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_runtime_dest}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_lib}" "${_runtime_dest}"
                VERBATIM
            )
        endif()
    endforeach()
endfunction()

if(INTERSECT_HAS_ONNX_RUNTIME)
    intersect_bundle_onnx_runtime(Intersect_VST3)
    intersect_bundle_onnx_runtime(Intersect_Standalone)
    if(APPLE)
        intersect_bundle_onnx_runtime(Intersect_AU)
    endif()
endif()
