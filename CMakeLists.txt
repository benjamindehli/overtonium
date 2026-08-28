cmake_minimum_required(VERSION 3.22)

# Before project(), and it has to be. project() creates both of these as empty
# cache entries while it works out the platform, and a set(... CACHE ...) after
# that is ignored because the entry already exists. Set afterwards they read as
# if they work and do nothing: the architecture list comes out empty, which
# means the host's own, so an Apple Silicon build machine quietly produces
# something no Intel Mac will run.
#
# CMAKE_HOST_APPLE rather than APPLE, since APPLE is one of the things project()
# is what defines.
if(CMAKE_HOST_APPLE)
    # Universal binary so the same build runs on Apple Silicon and Intel hosts.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

project(Overtonium VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
# On a workstation the point of a build is to have the plugin installed and
# ready for the host to rescan. On a build machine there is no host, and copying
# into a user folder that does not exist is a way to fail for no reason.
option(OVERTONIUM_INSTALL_AFTER_BUILD
       "Copy the built plugins into the user plugin folders" ON)

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
    # The update check reaches an https address, and only on Linux does that
    # take a dependency. macOS and Windows go through the platform's own
    # networking whatever this says, while JUCE's Linux fallback is a plain
    # socket with no TLS in it: it knows port 80 and nothing else.
    #
    # JUCE's own switch rather than defining JUCE_USE_CURL by hand. It sets the
    # macro and links pkgconfig_JUCE_CURL_LINUX_DEPS together, which is the
    # half that is easy to forget: the define alone compiles and then fails at
    # the link with every curl symbol missing.
    NEEDS_CURL                  TRUE
    COPY_PLUGIN_AFTER_BUILD     ${OVERTONIUM_INSTALL_AFTER_BUILD}
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    DESCRIPTION                 "32-partial overtone synthesiser"
    # The program icon. JUCE builds the whole icon set from this one square, so
    # there is no smaller second file to keep in step, and it reaches every
    # format that is a bundle: the standalone in the Dock and the task bar, and
    # the VST3 and the component in Finder. LV2 is not a bundle and Linux has
    # no icon step at all, so neither takes it.
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED FALSE)

set(OVERTONIUM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginParameters.cpp
    Source/Presets.cpp
    Source/UpdateCheck.cpp
    Source/dsp/Voice.cpp
    Source/dsp/SynthEngine.cpp
    Source/dsp/TapeEcho.cpp
    Source/dsp/Reverb.cpp
    Source/dsp/Wobble.cpp
    Source/UI/ChannelStrip.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/NoiseStrip.cpp
    Source/UI/Theme.cpp
    Source/UI/TopBar.cpp)

# The logo travels inside the binary rather than as a file beside it, since a
# plugin is loaded from wherever the host feels like putting it.
juce_add_binary_data(OvertoniumResources
    SOURCES
        Resources/logo.png
        Resources/dehli-musikk.svg)

target_sources(Overtonium PRIVATE ${OVERTONIUM_SOURCES})

target_include_directories(Overtonium PRIVATE Source)

target_compile_definitions(Overtonium PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # Our own, rather than JucePlugin_VersionString. That one is generated by
    # juce_add_plugin and does not exist for the console app the runtime tests
    # build, so anything shared between the two cannot reach it. This comes
    # from project(), which the release workflow already checks against the tag.
    OVERTONIUM_VERSION="${PROJECT_VERSION}")

# The preset menu's "Copy as factory preset code", which puts the C++ for the
# current patch on the clipboard for pasting into Presets.cpp. That is the
# workflow for adding a factory preset and it is no use to anybody who is not
# going to rebuild the plugin, so a released build does not carry it.
#
# Off rather than tied to a debug build: authoring a preset means listening to
# it, and a debug build of thirty-three voices is not something to judge a
# sound by.
option(OVERTONIUM_PRESET_AUTHORING
    "Offer the factory preset code generator in the preset menu" OFF)

# Always defined, as 1 or 0, so the guard in TopBar.cpp is a comparison rather
# than a test for existence. An undefined macro there evaluates to 0 and works,
# but -Wundef is right to call that a typo waiting to happen.
if(OVERTONIUM_PRESET_AUTHORING)
    target_compile_definitions(Overtonium PUBLIC OVERTONIUM_PRESET_AUTHORING=1)
else()
    target_compile_definitions(Overtonium PUBLIC OVERTONIUM_PRESET_AUTHORING=0)
endif()

if(APPLE)
    # Backs the editor with a CAMetalLayer instead of the ordinary CoreGraphics one.
    #
    # This matters here because of what the mixer asks for. Thirty-three meters spread
    # across the window each invalidate a small band of themselves, and since about
    # macOS 10.13 CoreGraphics answers a list of scattered rectangles by redrawing the
    # single rectangle that encloses them, which for this layout is the whole editor.
    # JUCE's own note in juce_NSViewComponentPeer_mac.mm says the Metal layer is the
    # way out of that. It also puts the per-frame compositing on the GPU, which is
    # where the cost was showing up: little CPU, but a large energy impact whenever
    # the meters were moving.
    #
    # Nothing about how anything is drawn changes. The same CoreGraphics calls produce
    # the same pixels, they are just handed to the window a different way.
    target_compile_definitions(Overtonium PUBLIC
        JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

target_link_libraries(Overtonium
    PRIVATE
        OvertoniumResources
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------------------
# Warnings as errors, off by default and turned on in CI.
#
# juce_recommended_warning_flags already asks for a great deal, including
# -Wconversion and -Wsign-conversion, but nothing makes any of it fatal, so a
# new warning can sit in the log until the log is long enough that nobody reads
# it. CI turns this on and a pull request that adds one fails.
#
# Off by default because a warning is not a reason for someone building from
# source on a compiler nobody here has tried to end up with no plugin at all.
# Applied to our own targets rather than globally, for the same reason in the
# other direction: a warning from inside JUCE is not something a change here
# can fix, and it should not be able to fail the build.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(overtonium_set_warnings_as_errors target)
    if(NOT OVERTONIUM_WARNINGS_AS_ERRORS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

overtonium_set_warnings_as_errors(Overtonium)

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
        Source/dsp/Reverb.cpp
        Source/dsp/Wobble.cpp)

    target_include_directories(overtonium_dsp_test PRIVATE Source)
    target_compile_features(overtonium_dsp_test PRIVATE cxx_std_17)

    add_test(NAME dsp COMMAND overtonium_dsp_test)

    # Headless integration test: builds the plugin sources as a console app and exercises
    # parameter wiring, MIDI, presets, bus layouts and state round-tripping. The editors it
    # opens are never put on the desktop, so it runs fine on a machine with no display.
    juce_add_console_app(overtonium_runtime_test PRODUCT_NAME "overtonium_runtime_test")

    target_sources(overtonium_runtime_test PRIVATE
        Tests/plugin_runtime_test.cpp
        ${OVERTONIUM_SOURCES})

    target_include_directories(overtonium_runtime_test PRIVATE Source)

    # No curl here, unlike the plugin. The tests compile UpdateCheck but never
    # fetch anything, so the test build does not need libcurl on the machine.
    # The authoring entry is off here whatever the plugin was configured with:
    # these targets compile TopBar.cpp too, and a macro left undefined is a
    # warning under -Wundef rather than a silent zero.
    target_compile_definitions(overtonium_runtime_test PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        OVERTONIUM_PRESET_AUTHORING=0
        OVERTONIUM_VERSION="${PROJECT_VERSION}")

    target_link_libraries(overtonium_runtime_test
        PRIVATE
            OvertoniumResources
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # overtonium_dsp_test is left out on purpose. It carries no warning flags
    # here, and the DSP core is already compiled with -Wall -Wextra -Werror by
    # the job that checks it builds without a build system at all.
    overtonium_set_warnings_as_errors(overtonium_runtime_test)

    add_test(NAME plugin COMMAND overtonium_runtime_test)
endif()

# ------------------------------------------------------------------------------------------
# The renderer behind the pictures on the project page.
#
# On by default so that it is compiled whenever anything else is. A tool that only builds
# when someone remembers to ask for it is a tool that stops building, and this one already
# went missing once. Turn it off with -DOVERTONIUM_BUILD_TOOLS=OFF if the extra link is not
# worth it. CONTRIBUTING.md covers running it.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_BUILD_TOOLS "Build the docs image renderer" ON)

if(OVERTONIUM_BUILD_TOOLS)
    juce_add_console_app(overtonium_render_docs PRODUCT_NAME "overtonium_render_docs")

    target_sources(overtonium_render_docs PRIVATE
        Tools/render_docs_images.cpp
        ${OVERTONIUM_SOURCES})

    target_include_directories(overtonium_render_docs PRIVATE Source)

    # Same reasoning as the runtime test: UpdateCheck is compiled but never fetches, so the
    # machine building this does not need libcurl.
    target_compile_definitions(overtonium_render_docs PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        OVERTONIUM_PRESET_AUTHORING=0
        OVERTONIUM_VERSION="${PROJECT_VERSION}")

    target_link_libraries(overtonium_render_docs
        PRIVATE
            OvertoniumResources
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    overtonium_set_warnings_as_errors(overtonium_render_docs)
endif()
