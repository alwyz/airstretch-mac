cmake_minimum_required(VERSION 3.22)

# Legacy Mac build: OS X 10.8 Mountain Lion, 64-bit Intel, for older hosts
# (e.g. Pro Tools 11 through a wrapper such as Blue Cat PatchWork). JUCE 7 and
# later need macOS 10.9+, so this build uses JUCE 6.1.6. See README.
option(AIRSTRETCH_LEGACY_MAC "Build for OS X 10.8 with JUCE 6.1.6 (x86_64, VST3 + AU)" OFF)

if(AIRSTRETCH_LEGACY_MAC)
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.8" CACHE STRING "" FORCE)
    set(AIRSTRETCH_JUCE_TAG 6.1.6)
else()
    # Universal binary on macOS (Apple Silicon + Intel); ignored elsewhere
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
    set(AIRSTRETCH_JUCE_TAG 8.0.4)
endif()

project(AirStretch VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout with -DJUCE_DIR=/path/to/JUCE, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${AIRSTRETCH_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(AIRSTRETCH_BUILD_TESTS "Build the offline DSP test" ON)

set(AIRSTRETCH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AIRSTRETCH_FORMATS AU)
endif()

if(AIRSTRETCH_LEGACY_MAC)
    set(AIRSTRETCH_FORMATS VST3 AU)
endif()

# VST3_AUTO_MANIFEST only exists in JUCE 7+; JUCE 6 would misread it as a format.
set(AIRSTRETCH_EXTRA_PLUGIN_ARGS)
if(NOT AIRSTRETCH_LEGACY_MAC)
    set(AIRSTRETCH_VST3_MANIFEST TRUE)
    # Unofficial MinGW cross-build (llvm-mingw from Linux); see README.
    # VST3 only, and the manifest step is skipped because it runs a .exe.
    if(MINGW)
        set(AIRSTRETCH_FORMATS VST3)
        set(AIRSTRETCH_VST3_MANIFEST FALSE)
    endif()
    list(APPEND AIRSTRETCH_EXTRA_PLUGIN_ARGS VST3_AUTO_MANIFEST ${AIRSTRETCH_VST3_MANIFEST})
endif()

juce_add_plugin(AirStretch
    COMPANY_NAME "MyCompany"                 # change to your company name
    BUNDLE_ID "com.mycompany.airstretch"
    PLUGIN_MANUFACTURER_CODE Mcmp            # 4 chars, at least one uppercase
    PLUGIN_CODE Arst                         # 4 chars, unique per plugin
    PRODUCT_NAME "AirStretch"
    FORMATS ${AIRSTRETCH_FORMATS}
    ${AIRSTRETCH_EXTRA_PLUGIN_ARGS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE            # set TRUE to auto-install after build
    VST3_CATEGORIES Fx Dynamics EQ
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(AirStretch PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(AirStretch PRIVATE Source)

target_compile_definitions(AirStretch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AirStretch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Linux builds: avoid optional X11 extensions / audio backends not always installed
if(UNIX AND NOT APPLE)
    target_compile_definitions(AirStretch PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
        JUCE_ALSA=0 JUCE_JACK=0)
endif()

if(MINGW)
    target_sources(AirStretch PRIVATE Source/MingwCompat.cpp)
    target_link_options(AirStretch_VST3 PRIVATE -static -static-libgcc -static-libstdc++)
    # MinGW ignores #pragma comment(lib, ...), so list the system libraries
    target_link_libraries(AirStretch PRIVATE
        winmm ws2_32 wininet version shlwapi ole32 oleaut32 imm32 comdlg32 comctl32
        dxgi d2d1 dwrite d3d11 dxguid shell32 uuid rpcrt4 gdi32 user32 advapi32
        crypt32 setupapi dwmapi uxtheme usp10 opengl32 windowscodecs dcomp dbghelp)
endif()

if(AIRSTRETCH_BUILD_TESTS)
    add_executable(EngineTest tests/EngineTest.cpp)
    target_include_directories(EngineTest PRIVATE Source)
endif()
