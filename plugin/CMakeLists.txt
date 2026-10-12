cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED GLOW_VERSION)
    set(GLOW_VERSION 0.1.0)   # CI passes 0.1.<build number>
endif()
project(Glow VERSION ${GLOW_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universal binary
endif()

# JUCE: use a local checkout if you pass -DJUCE_DIR=/path/to/JUCE, otherwise download 8.0.15.
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(GLOW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GLOW_FORMATS AU)
endif()

juce_add_plugin(Glow
    COMPANY_NAME "Haren Audio"
    BUNDLE_ID "com.harenaudio.glow"
    PLUGIN_MANUFACTURER_CODE Hrna
    PLUGIN_CODE Glw1
    FORMATS ${GLOW_FORMATS}
    PRODUCT_NAME "Glow"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE          # installs into ~/Library/Audio/Plug-Ins on macOS
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion)

target_sources(Glow PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Glow PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Glow
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offline renderer: runs the exact plugin DSP over a WAV. Used for the A/B tests.
option(GLOW_BUILD_TOOLS "Build the offline render tool" OFF)
if(GLOW_BUILD_TOOLS)
    juce_add_console_app(GlowRender PRODUCT_NAME "GlowRender")
    target_sources(GlowRender PRIVATE tools/RenderTool.cpp)
    target_include_directories(GlowRender PRIVATE Source)
    target_compile_definitions(GlowRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(GlowRender PRIVATE juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Host-style test of the real processor (timing, bypass latency, stereo)
    juce_add_console_app(GlowProcTest PRODUCT_NAME "GlowProcTest")
    target_sources(GlowProcTest PRIVATE tools/ProcTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(GlowProcTest PRIVATE Source)
    target_compile_definitions(GlowProcTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Glow" JucePlugin_VersionString="test" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(GlowProcTest PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
