cmake_minimum_required(VERSION 3.22)
project(StringForge VERSION 0.1.0 LANGUAGES CXX)

# Place a JUCE checkout (or submodule) in ./JUCE before configuring.
add_subdirectory(JUCE)

juce_add_plugin(StringForge
    COMPANY_NAME "StringForge Audio"
    BUNDLE_ID com.stringforge.audio.stringforge
    PLUGIN_MANUFACTURER_CODE SfAu
    PLUGIN_CODE StFg
    PRODUCT_NAME "StringForge"
    DESCRIPTION "Multi-effects processor for electric and acoustic-electric strings"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 AU Standalone
    VST3_CATEGORIES Fx Distortion Delay Modulation Reverb EQ Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(StringForge)

target_sources(StringForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ParameterIDs.h
    Source/DSP/PitchHarmonyEngine.h
    Source/DSP/PitchHarmonyEngine.cpp
    Source/DSP/StereoLooper.h
    Source/DSP/StereoLooper.cpp
)

target_compile_features(StringForge PRIVATE cxx_std_17)
target_compile_definitions(StringForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(StringForge PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)


option(STRINGFORGE_BUILD_DSP_TESTS "Build StringForge JUCE DSP unit tests" ON)
if (STRINGFORGE_BUILD_DSP_TESTS)
    enable_testing()
    juce_add_console_app(StringForgeDspTests PRODUCT_NAME "StringForge DSP Tests")
    juce_generate_juce_header(StringForgeDspTests)
    target_sources(StringForgeDspTests PRIVATE
        Tests/DspModuleTests.cpp
        Source/DSP/PitchHarmonyEngine.h
        Source/DSP/PitchHarmonyEngine.cpp
        Source/DSP/StereoLooper.h
        Source/DSP/StereoLooper.cpp
    )
    target_compile_features(StringForgeDspTests PRIVATE cxx_std_17)
    target_compile_definitions(StringForgeDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_UNIT_TESTS=1)
    target_link_libraries(StringForgeDspTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    add_test(NAME StringForgeDSP COMMAND $<TARGET_FILE:StringForgeDspTests>)
endif()
