cmake_minimum_required(VERSION 3.22)
project(SeedChop VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE -----------------------------------------------------------------
# Option A: you already have a JUCE checkout (like your OAO project does) —
# just point this at it instead of using FetchContent:
#   add_subdirectory(../JUCE JUCE)
#
# Option B: fetch JUCE from GitHub (used below).
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SeedChop
    COMPANY_NAME            echoe
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    PLUGIN_MANUFACTURER_CODE Ecoe
    PLUGIN_CODE              Schp
    FORMATS                  VST3 AU Standalone
    PRODUCT_NAME             "SeedChop"
)

target_sources(SeedChop
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(SeedChop
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SeedChop
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
