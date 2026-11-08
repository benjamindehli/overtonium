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

project(Overtonium VERSION 1.16.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------------------
# JUCE
#
# By default JUCE is downloaded at configure time. Point -DJUCE_PATH=/path/to/JUCE at a
# local checkout to skip the download (useful offline or for CI caching).
#
# JUCE 9.0.5, fetched as the archive of the commit its tag points to and checked against
# that archive's hash. A tag can be moved and a commit cannot, and the hash fails the
# configure if what arrives is not what was checked.
# ------------------------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

if(JUCE_PATH)
    message(STATUS "Overtonium: using local JUCE at ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    # Extracted files get the time they were extracted, so a new archive rebuilds
    # what depends on it. Newer CMake warns unless this is chosen.
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    include(FetchContent)
    FetchContent_Declare(JUCE
        URL      https://github.com/juce-framework/JUCE/archive/d60b7a78030661574a615b586d9325ba7a7fa7dd.tar.gz
        URL_HASH SHA256=8237b2976871982eb9107fd67b576d95bf02355d99540a0127c4e7c001ecd9f5)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------------------
# dehli-musikk-plugin-core
#
# The output stage and its machines, shared with the other Dehli Musikk plugins. Pinned the
# same way as JUCE, by the archive of a commit and that archive's hash. To work on the
# library and Overtonium together, point CMake at a checkout of it:
#
#   -DFETCHCONTENT_SOURCE_DIR_DM_PLUGIN_CORE=../dehli-musikk-plugin-core
# ------------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(dm_plugin_core
    URL      https://github.com/benjamindehli/dehli-musikk-plugin-core/archive/e51c8eb536f84d749e434082fb5452105141aa69.tar.gz
    URL_HASH SHA256=a40881bdf7e10f8252d0334350427f653abc0637432e0f03acb87ab4c5a97a2f)
FetchContent_MakeAvailable(dm_plugin_core)

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
    DESCRIPTION                 "32-partial overtone synthesizer"
    # The program icon. JUCE builds the whole icon set from this one square, so
    # there is no smaller second file to keep in step, and it reaches every
    # format that is a bundle: the standalone in the Dock and the task bar, and
    # the VST3 and the component in Finder. LV2 is not a bundle and Linux has
    # no icon step at all, so neither takes it.
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED FALSE)

# The DSP core, which builds with no JUCE at all. The DSP tests compile this
# list alone, with the library.
set(OVERTONIUM_DSP_SOURCES
    Source/dsp/BucketEcho.cpp
    Source/dsp/DigitalEcho.cpp
    Source/dsp/PlateReverb.cpp
    Source/dsp/SpringReverb.cpp
    Source/dsp/BusDrive.cpp
    Source/dsp/Voice.cpp
    Source/dsp/SynthEngine.cpp
    Source/dsp/TapeEcho.cpp
    Source/dsp/Reverb.cpp
    Source/dsp/Wobble.cpp
    Source/dsp/KarplusString.cpp
    Source/dsp/SympatheticStrings.cpp)

set(OVERTONIUM_SOURCES
    ${OVERTONIUM_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginParameters.cpp
    Source/Presets.cpp
    Source/MidiLearn.cpp
    Source/UI/MacroPanel.cpp
    Source/UpdateCheck.cpp
    Source/UI/ChannelStrip.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/MuteSoloButton.cpp
    Source/UI/Notice.cpp
    Source/UI/NoiseStrip.cpp
    Source/UI/OptionBars.cpp
    Source/UI/SettingsMenus.cpp
    Source/UI/ShapeButton.cpp
    Source/UI/StripColumn.cpp
    Source/UI/Theme.cpp
    Source/UI/TopBar.cpp)

# The logo travels inside the binary rather than as a file beside it, since a
# plugin is loaded from wherever the host feels like putting it.
juce_add_binary_data(OvertoniumResources
    SOURCES
        Resources/logo.png)

target_sources(Overtonium PRIVATE ${OVERTONIUM_SOURCES})

target_include_directories(Overtonium PRIVATE Source)

target_compile_definitions(Overtonium PUBLIC
    JUCE_WEB_BROWSER=0
    # Off because nothing here loads one. JUCE 9.0.3 vendored libwebp and put
    # the decoder in the static list of image formats, where the linker cannot
    # see past it, so it ships whether or not anything ever calls it. Measured
    # at 717 KB of an arm64 binary, a tenth of the whole, and about 1.4 MB of
    # the universal macOS one. The three embedded resources are SVG and PNG,
    # and the site's WebP files are made outside the build entirely. See the
    # comment at the head of Tools/render_docs_images.cpp.
    JUCE_USE_WEBP=0
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
        dm::plugin_core
        dm::dm_gui
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------------------
# Warnings as errors, on by default and turned off for a release build.
#
# juce_recommended_warning_flags already asks for a great deal, including
# -Wconversion and -Wsign-conversion, but nothing makes any of it fatal, so a
# new warning can sit in the log until the log is long enough that nobody reads
# it. This makes it fatal, so a pull request that adds one fails.
#
# On by default, so that a plain build here is the build the workflows do. It
# was off, on the grounds that a warning is no reason for someone building from
# source on an untried compiler to end up with no plugin at all. That reason
# still holds and is why the switch exists, but it had the cost the other way
# round: the strict build was the unusual one, so warnings scrolled past every
# local build and failed twenty minutes later on a runner. A `-Wfloat-equal` in
# a test reached the Linux job that way. Anyone stuck on a warning this project
# has not met can configure with -DOVERTONIUM_WARNINGS_AS_ERRORS=OFF.
#
# Applied per target rather than globally, which spares juceaide and whatever
# else CMake builds for the framework's own purposes. It does not spare JUCE
# itself: a module's sources are compiled into the target that links it, so
# they are built with these options too. That was read the other way round here
# for a while. When a JUCE release warns, nothing in this repository can fix
# the warning, so it is silenced for the one JUCE file and the one check, as
# below the function for JUCE 9.0.5, and our own code keeps it.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(overtonium_set_warnings_as_errors target)
    if(NOT OVERTONIUM_WARNINGS_AS_ERRORS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    # A function defined with no declaration before it, which is almost always
    # one that belongs in an anonymous namespace. JUCE's recommended flags ask
    # Clang for this as -Wmissing-prototypes, and GCC has it under this name,
    # so the Linux build catches it before the macOS one does.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-declarations>)
    endif()
endfunction()

overtonium_set_warnings_as_errors(Overtonium)

# JUCE 9.0.5's alert window code takes a reference to the first button in a Span
# that getButtons() returns by value. The reference points into the options
# object, not the Span, so it is sound, but GCC 13's dangling-reference check
# warns about any reference that comes through a temporary. Silenced for that
# one JUCE file and on GCC only, since the warning is GCC's, and our own code
# keeps the check. Source properties belong to the directory, so this covers
# every target here that compiles the file.
if(OVERTONIUM_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    set_source_files_properties(
        "${JUCE_MODULES_DIR}/juce_gui_basics/juce_gui_basics_2.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-dangling-reference")
endif()

# The Ogg Vorbis library JUCE carries inside juce_audio_formats is C compiled as
# C++, and declares nothing before it defines it. Our own code keeps
# -Wmissing-declarations. See overtonium_set_warnings_as_errors.
if(OVERTONIUM_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        "${JUCE_MODULES_DIR}/juce_audio_formats/juce_audio_formats.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-missing-declarations")
endif()

# ------------------------------------------------------------------------------------------
# Sanitizers
#
# Address and undefined behaviour, for the test targets. The gates that already run say
# whether the plugin does the right thing. These say whether it does it without reading past
# an array or relying on behaviour the standard does not define. pluginval exercises the
# plugin as a host and cannot see either, since a read one byte off the end of a buffer
# usually returns a plausible number.
#
# Off by default, and CI turns it on in a job of its own rather than in the build that
# produces the binaries: sanitized code is several times slower and is not what anybody
# should be shipping or listening to.
#
# Recover is left on. A run reports everything it finds rather than stopping at the first
# thing, which is what makes one CI log worth reading. The job checks the log rather than
# only the exit status, and a leak is a finding as much as an overflow is.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_SANITIZE "Build the tests with address and UB sanitizers" OFF)

function(overtonium_set_sanitizers target)
    if(NOT OVERTONIUM_SANITIZE)
        return()
    endif()

    if(MSVC)
        message(WARNING "OVERTONIUM_SANITIZE is a no-op on MSVC")
        return()
    endif()

    # Frame pointers so a report names the functions it came through, and no tail-call
    # elimination for the same reason: without them a stack trace can skip the frame that
    # actually did it.
    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -g)

    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()

# ------------------------------------------------------------------------------------------
# The console programs: the runtime test and the tools. Each compiles the whole plugin
# with one file of its own and runs it with no host and no display.
#
# No curl and no WebP. They compile UpdateCheck but never fetch anything, and none of
# them loads an image, so the machine building them needs neither library. The
# authoring entry is off whatever the plugin was configured with: these compile
# TopBar.cpp too, and a macro left undefined is a warning under -Wundef rather than a
# silent zero. The rest is as the plugin has it.
# ------------------------------------------------------------------------------------------
function(overtonium_add_console target source)
    juce_add_console_app(${target} PRODUCT_NAME "${target}")

    target_sources(${target} PRIVATE ${source} ${OVERTONIUM_SOURCES})
    target_include_directories(${target} PRIVATE Source)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_WEBP=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_STANDALONE_APPLICATION=1
        OVERTONIUM_PRESET_AUTHORING=0
        OVERTONIUM_VERSION="${PROJECT_VERSION}")

    target_link_libraries(${target}
        PRIVATE
            OvertoniumResources
            dm::plugin_core
            dm::dm_gui
            juce::juce_audio_utils
            ${ARGN}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    overtonium_set_warnings_as_errors(${target})
endfunction()

# ------------------------------------------------------------------------------------------
# DSP tests
#
# The DSP core has no JUCE dependency at all, so the tests build and run anywhere with a
# C++17 compiler -- including CI images with no GUI libraries installed.
# ------------------------------------------------------------------------------------------
option(OVERTONIUM_BUILD_TESTS "Build the DSP and runtime tests" ON)

if(OVERTONIUM_BUILD_TESTS)
    enable_testing()

    add_executable(overtonium_dsp_test
        Tests/dsp_test.cpp
        ${OVERTONIUM_DSP_SOURCES})

    target_include_directories(overtonium_dsp_test PRIVATE Source)
    target_compile_features(overtonium_dsp_test PRIVATE cxx_std_17)
    target_link_libraries(overtonium_dsp_test PRIVATE dm::plugin_core)

    # The library too, since both test programs run its code.
    overtonium_set_sanitizers(dm_plugin_core)
    overtonium_set_sanitizers(overtonium_dsp_test)

    add_test(NAME dsp COMMAND overtonium_dsp_test)

    # Headless integration test: builds the plugin sources as a console app and exercises
    # parameter wiring, MIDI, presets, bus layouts and state round-tripping. The editors it
    # opens are never put on the desktop, so it runs fine on a machine with no display.
    overtonium_add_console(overtonium_runtime_test Tests/plugin_runtime_test.cpp)

    # Where docs/parameters.json is, which the test holds to the layout and
    # --write-parameters regenerates.
    target_compile_definitions(overtonium_runtime_test PUBLIC
        OVERTONIUM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # overtonium_dsp_test is left out of the warnings on purpose. It carries no
    # warning flags here, and the DSP core is already compiled with -Wall
    # -Wextra -Werror by the job that checks it builds without a build system.
    overtonium_set_sanitizers(overtonium_runtime_test)

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
option(OVERTONIUM_BUILD_TOOLS "Build the docs image and audio renderers" ON)

if(OVERTONIUM_BUILD_TOOLS)
    overtonium_add_console(overtonium_render_docs Tools/render_docs_images.cpp)

    # The audio clips. It needs juce_audio_formats for the Ogg and FLAC writers,
    # which the plugin itself has no use for.
    overtonium_add_console(overtonium_render_docs_audio Tools/render_docs_audio.cpp
        juce::juce_audio_formats)

    # The preset generator. It loads a saved preset into a processor and prints
    # the block that would add it to Presets.cpp, which is the one step between
    # setting up a patch and shipping it.
    overtonium_add_console(overtonium_preset_to_code Tools/preset_to_code.cpp)
endif()
