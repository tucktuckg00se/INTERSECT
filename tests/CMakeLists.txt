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
