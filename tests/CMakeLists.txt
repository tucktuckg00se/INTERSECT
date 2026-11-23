juce_add_console_app(IntersectAudioTests
    PRODUCT_NAME "INTERSECT audio tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectAudioTests PRIVATE
    LazyChopEngineTests.cpp
    SampleDataTests.cpp
    ../src/audio/LazyChopEngine.cpp
    ../src/audio/VoicePool.cpp
    ../src/audio/SliceManager.cpp
    ../src/audio/SampleData.cpp
    ../src/audio/Rex2Import.cpp
)
target_include_directories(IntersectAudioTests PRIVATE
    ..
    ../src
    ../signalsmith-stretch
    ../signalsmith-linear
)
target_compile_definitions(IntersectAudioTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectAudioTests PRIVATE
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    bungee_lib
    velociloops
)
add_test(NAME lazy_chop COMMAND IntersectAudioTests)

juce_add_console_app(IntersectStemTests
    PRODUCT_NAME "INTERSECT stem tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectStemTests PRIVATE
    StemSeparationTests.cpp
    ../src/audio/StemSeparation.cpp
    ../src/audio/StemSpectral.cpp
)
target_include_directories(IntersectStemTests PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectStemTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectStemTests PRIVATE
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME stem_separation COMMAND IntersectStemTests)

juce_add_console_app(IntersectSettingsTests
    PRODUCT_NAME "INTERSECT settings tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectSettingsTests PRIVATE
    SettingsFileTests.cpp
    ../src/SettingsFile.cpp
)
target_include_directories(IntersectSettingsTests PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectSettingsTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectSettingsTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME settings_file COMMAND IntersectSettingsTests)

juce_add_console_app(IntersectUpdateTests
    PRODUCT_NAME "INTERSECT update tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectUpdateTests PRIVATE
    UpdateCheckTests.cpp
    ../src/UpdateCheck.cpp
    ../src/HttpFetch.cpp
)
target_include_directories(IntersectUpdateTests PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectUpdateTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectUpdateTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME update_check COMMAND IntersectUpdateTests)

juce_add_console_app(IntersectBrowserTests
    PRODUCT_NAME "INTERSECT browser tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectBrowserTests PRIVATE
    BrowserSearchTests.cpp
    ../src/ui/browser/SearchIndex.cpp
    ../src/ui/browser/DirectoryListing.cpp
)
target_include_directories(IntersectBrowserTests PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectBrowserTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectBrowserTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME browser_search COMMAND IntersectBrowserTests)

juce_add_console_app(IntersectUrlImportTests
    PRODUCT_NAME "INTERSECT URL import tests"
    VERSION "${INTERSECT_VERSION}"
)

target_sources(IntersectUrlImportTests PRIVATE
    UrlImportTests.cpp
    ../src/urlimport/UrlImportLogic.cpp
    ../src/urlimport/ToolProcess.cpp
)
target_include_directories(IntersectUrlImportTests PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectUrlImportTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(IntersectUrlImportTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME url_import COMMAND IntersectUrlImportTests)

# Opt-in end-to-end run against a real site (needs the network, so it isn't an add_test):
#   INTERSECT_MEDIA_TOOLS_MANIFEST=file:///.../intersect_media_tools_manifest.json \
#     IntersectUrlImportE2E <url> <fresh tools folder> <output folder>
juce_add_console_app(IntersectUrlImportE2E
    PRODUCT_NAME "INTERSECT URL import end-to-end"
    VERSION "${INTERSECT_VERSION}"
    NEEDS_CURL TRUE
)

target_sources(IntersectUrlImportE2E PRIVATE
    UrlImportEndToEnd.cpp
    ../src/urlimport/UrlImportLogic.cpp
    ../src/urlimport/ToolProcess.cpp
    ../src/urlimport/ToolInstall.cpp
    ../src/urlimport/UrlImportJob.cpp
    ../src/Download.cpp
    ../src/HttpFetch.cpp
    ../src/audio/StemSeparation.cpp
    ../src/audio/StemSpectral.cpp
)
target_include_directories(IntersectUrlImportE2E PRIVATE
    ..
    ../src
)
target_compile_definitions(IntersectUrlImportE2E PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
)
target_link_libraries(IntersectUrlImportE2E PRIVATE
    juce::juce_audio_basics
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Opt-in stem separation timing with the plugin's own job (downloads the ORT bundle and the model,
# so it isn't an add_test). Point HOME at a scratch folder:
#   HOME=/tmp/bench IntersectStemBench <model id> <cpu|gpu> <input.wav | gen:SECONDS> <output folder>
if(INTERSECT_HAS_ONNX_RUNTIME)
    juce_add_console_app(IntersectStemBench
        PRODUCT_NAME "INTERSECT stem benchmark"
        VERSION "${INTERSECT_VERSION}"
        NEEDS_CURL TRUE
    )
    target_sources(IntersectStemBench PRIVATE
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/audio/StemCoreML.mm>
        StemBenchmark.cpp
        ../src/audio/StemSeparation.cpp
        ../src/audio/StemSpectral.cpp
        ../src/audio/StemSeparationJob.cpp
        ../src/audio/StemGpuInfo.cpp
        ../src/audio/StemModelDownloadJob.cpp
        ../src/audio/OrtBundleDownloadJob.cpp
        ../src/Download.cpp
        ../src/HttpFetch.cpp
    )
    target_include_directories(IntersectStemBench PRIVATE
        ..
        ../src
        "${INTERSECT_ORT_HEADERS_ROOT}/include"
    )
    target_compile_definitions(IntersectStemBench PRIVATE
        JucePlugin_VersionString="${INTERSECT_VERSION}"
        JUCE_WEB_BROWSER=0
        INTERSECT_HAS_ONNX_RUNTIME=1
        INTERSECT_ORT_VERSION="${INTERSECT_ORT_VERSION}"
    )
    # On Linux, NEEDS_CURL above defines JUCE_USE_CURL=1 and links libcurl.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(IntersectStemBench PRIVATE JUCE_USE_CURL=0)
    endif()
    if(WIN32)
        target_link_libraries(IntersectStemBench PRIVATE dxgi)
    endif()
    if(APPLE)
        target_link_libraries(IntersectStemBench PRIVATE "-framework CoreML")
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(IntersectStemBench PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_link_libraries(IntersectStemBench PRIVATE
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endif()

# The Core ML runner (macOS) is Objective-C++ with ARC; source properties don't carry over from the parent directory.
if(APPLE AND INTERSECT_HAS_ONNX_RUNTIME)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/audio/StemCoreML.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# The real processor (every plugin source but the standalone shell), driven like a host would.
# Keep the include dirs, definitions and libraries in step with the Intersect target.
juce_add_console_app(IntersectProcessorTests
    PRODUCT_NAME "INTERSECT processor tests"
    VERSION "${INTERSECT_VERSION}"
    NEEDS_CURL TRUE
)

target_sources(IntersectProcessorTests PRIVATE
    ProcessorTests.cpp
    ${INTERSECT_CORE_SOURCES}
)
target_include_directories(IntersectProcessorTests PRIVATE
    ..
    ../src
    ../signalsmith-stretch
    ../signalsmith-linear
    ${BUNGEE_ROOT}
)
if(INTERSECT_HAS_ONNX_RUNTIME)
    target_include_directories(IntersectProcessorTests PRIVATE "${INTERSECT_ORT_HEADERS_ROOT}/include")
endif()
target_compile_definitions(IntersectProcessorTests PRIVATE
    JucePlugin_VersionString="${INTERSECT_VERSION}"
    JucePlugin_Build_Standalone=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    INTERSECT_HAS_ONNX_RUNTIME=$<BOOL:${INTERSECT_HAS_ONNX_RUNTIME}>
    INTERSECT_ORT_VERSION="${INTERSECT_ORT_VERSION}"
    INTERSECT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
# On Linux, NEEDS_CURL above defines JUCE_USE_CURL=1 and links libcurl, like the plugin's JUCE_USE_CURL=1.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(IntersectProcessorTests PRIVATE JUCE_USE_MP3AUDIOFORMAT=1)
else()
    target_compile_definitions(IntersectProcessorTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
endif()
if(WIN32)
    target_link_libraries(IntersectProcessorTests PRIVATE dxgi)
endif()
if(APPLE AND INTERSECT_HAS_ONNX_RUNTIME)
    target_link_libraries(IntersectProcessorTests PRIVATE "-framework CoreML")
endif()
if(INTERSECT_HAS_ONNX_RUNTIME AND UNIX AND NOT APPLE)
    target_link_libraries(IntersectProcessorTests PRIVATE ${CMAKE_DL_LIBS})
endif()
target_link_libraries(IntersectProcessorTests PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    bungee_lib
    velociloops
    IntersectFonts
)
# HOME points at a scratch folder so the tests never read or write the real settings.yaml.
add_test(NAME processor_state COMMAND IntersectProcessorTests state)
# Preset/project restores while another thread renders audio. Plain builds only catch crashes;
# a TSan build (-DINTERSECT_SANITIZE=thread, TSAN_OPTIONS=suppressions=tests/tsan.supp) shows races.
add_test(NAME processor_restore_during_playback COMMAND IntersectProcessorTests stress)
set_tests_properties(processor_state processor_restore_during_playback
                     PROPERTIES ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home")

# The saved-state format on its own (state/SessionState), no processor.
juce_add_console_app(IntersectStateTests
    PRODUCT_NAME "INTERSECT state format tests"
    VERSION "${INTERSECT_VERSION}"
)
target_sources(IntersectStateTests PRIVATE
    StateFormatTests.cpp
    ../src/state/SessionState.cpp
    ../src/params/SampleParams.cpp
    ../src/audio/SampleGroups.cpp
)
target_include_directories(IntersectStateTests PRIVATE .. ../src)
target_compile_definitions(IntersectStateTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    INTERSECT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(IntersectStateTests PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME state_format COMMAND IntersectStateTests)

# Every test that ctest runs, so CI can build just these: cmake --build <dir> --target IntersectTests
add_custom_target(IntersectTests)
add_dependencies(IntersectTests
    IntersectAudioTests
    IntersectStemTests
    IntersectSettingsTests
    IntersectUpdateTests
    IntersectBrowserTests
    IntersectUrlImportTests
    IntersectProcessorTests
    IntersectStateTests
)
