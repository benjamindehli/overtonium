cmake_minimum_required(VERSION 3.22)

project(Overtonium VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    # Universal binary so the same build runs on Apple Silicon and Intel hosts.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

# ------------------------------------------------------------------------------------------
# JUCE
#
# By default JUCE is downloaded at configure time. Point -DJUCE_PATH=/path/to/JUCE at a
# local checkout to skip the download (useful offline or for CI caching).
# ------------------------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

if(JUCE_PATH)
    message(STATUS "Overtonium: using local JUCE at ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------------------
set(OVERTONIUM_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND OVERTONIUM_FORMATS AU)
endif()

if(UNIX AND NOT APPLE)
    list(APPEND OVERTONIUM_FORMATS LV2)
endif()

juce_add_plugin(Overtonium
    PRODUCT_NAME                "Overtonium"
    # COMPANY_NAME is the string hosts group plugins by in their browser.
    COMPANY_NAME                "DehliMusikk"
    BUNDLE_ID                   "com.dehlimusikk.overtonium"
    PLUGIN_MANUFACTURER_CODE    Dhmk
    PLUGIN_CODE                 Ovtn
    FORMATS                     ${OVERTONIUM_FORMATS}
    LV2URI                      "urn:dehlimusikk:overtonium"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    DESCRIPTION                 "32-partial overtone synthesiser"
    MICROPHONE_PERMISSION_ENABLED FALSE)

set(OVERTONIUM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginParameters.cpp
    Source/Presets.cpp
    Source/dsp/Voice.cpp
    Source/dsp/SynthEngine.cpp
    Source/dsp/TapeEcho.cpp
    Source/dsp/Reverb.cpp
    Source/UI/ChannelStrip.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/NoiseStrip.cpp
    Source/UI/Theme.cpp
    Source/UI/TopBar.cpp)

target_sources(Overtonium PRIVATE ${OVERTONIUM_SOURCES})

target_include_directories(Overtonium PRIVATE Source)

target_compile_definitions(Overtonium PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # JUCE_DISPLAY_SPLASH_SCREEN: left at its default (enabled) on purpose. Turning it off
    # requires either a paid JUCE licence or releasing this project under the AGPLv3.
    # See README.md before changing it.
    )

target_link_libraries(Overtonium
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------------------
# DSP tests
#
# The DSP core has no JUCE dependency at all, so the tests build and run anywhere with a
# C++17 compiler -- including CI images with no GUI libraries installed.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_BUILD_TESTS "Build the DSP unit tests" ON)

if(OVERTONIUM_BUILD_TESTS)
    enable_testing()

    add_executable(overtonium_dsp_test
        Tests/dsp_test.cpp
        Source/dsp/Voice.cpp
        Source/dsp/SynthEngine.cpp
        Source/dsp/TapeEcho.cpp
        Source/dsp/Reverb.cpp)

    target_include_directories(overtonium_dsp_test PRIVATE Source)
    target_compile_features(overtonium_dsp_test PRIVATE cxx_std_17)

    add_test(NAME dsp COMMAND overtonium_dsp_test)

    # Headless integration test: builds the plugin sources as a console app and exercises
    # parameter wiring, MIDI, presets, bus layouts and state round-tripping. It never
    # creates an editor, so it runs fine on a machine with no display.
    juce_add_console_app(overtonium_runtime_test PRODUCT_NAME "overtonium_runtime_test")

    target_sources(overtonium_runtime_test PRIVATE
        Tests/plugin_runtime_test.cpp
        ${OVERTONIUM_SOURCES})

    target_include_directories(overtonium_runtime_test PRIVATE Source)

    target_compile_definitions(overtonium_runtime_test PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(overtonium_runtime_test
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME plugin COMMAND overtonium_runtime_test)
endif()
