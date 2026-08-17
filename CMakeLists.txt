cmake_minimum_required(VERSION 3.25)

project(SAF_BEETLE VERSION 0.1.0 LANGUAGES C CXX)

option(SAF_BEETLE_BUILD_PLUGIN "Build the JUCE VST3 and standalone applications" ON)
option(SAF_BEETLE_BUILD_TESTS "Build deterministic DSP tests" ON)
option(SAF_BEETLE_BUILD_DEMO "Build the WAV-rendering demo" ON)

add_library(saf_beetle_dsp STATIC
    src/dsp/BtleProcessor.cpp
    src/dsp/BtleProcessor.h
)

target_include_directories(saf_beetle_dsp PUBLIC src)
target_compile_features(saf_beetle_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(saf_beetle_dsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(saf_beetle_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SAF_BEETLE_BUILD_TESTS)
    include(CTest)
    add_executable(saf_beetle_tests tests/BtleProcessorTests.cpp)
    target_link_libraries(saf_beetle_tests PRIVATE saf_beetle_dsp)
    add_test(NAME saf_beetle_dsp_tests COMMAND saf_beetle_tests)
endif()

if(SAF_BEETLE_BUILD_DEMO)
    add_executable(saf_beetle_render tools/RenderDemo.cpp)
    target_link_libraries(saf_beetle_render PRIVATE saf_beetle_dsp)
endif()

if(SAF_BEETLE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.0.tar.gz
        URL_HASH SHA256=368AFEB9C29217E82219EB4F9786FF56E0ACE0753E8D21C451B6A32BB15A6DE8
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(SAF_BEETLE
        COMPANY_NAME "Simple AF"
        BUNDLE_ID "com.simpleaf.beetle"
        PLUGIN_MANUFACTURER_CODE Safx
        PLUGIN_CODE Btle
        FORMATS VST3 Standalone
        PRODUCT_NAME "SAF Beetle"
        DESCRIPTION "Bluetooth packet-loss and jitter emulator"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Distortion Delay
    )

    juce_generate_juce_header(SAF_BEETLE)

    target_sources(SAF_BEETLE PRIVATE
        src/plugin/PluginEditor.cpp
        src/plugin/PluginEditor.h
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginProcessor.h
    )

    target_compile_features(SAF_BEETLE PRIVATE cxx_std_20)
    target_compile_definitions(SAF_BEETLE PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(SAF_BEETLE
        PRIVATE
            saf_beetle_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()
