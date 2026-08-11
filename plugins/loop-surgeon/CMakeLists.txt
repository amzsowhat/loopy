juce_add_plugin(LoopSurgeon
    VERSION 0.10.0
    COMPANY_NAME "Sound VST Project"
    BUNDLE_ID com.amzsowhat.loopsurgeon
    PLUGIN_MANUFACTURER_CODE SVst
    PLUGIN_CODE LpSg
    FORMATS VST3 Standalone
    PRODUCT_NAME "Loop Surgeon"
    DESCRIPTION "Repair loops or construct long-form SFX texture loops from selected audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
)

target_sources(LoopSurgeon
    PRIVATE
        Source/LoopEngine.cpp
        Source/LoopEngine.h
        Source/LoopAnalyzer.cpp
        Source/LoopAnalyzer.h
        Source/SignalDiagnostics.cpp
        Source/SignalDiagnostics.h
        Source/TextureSynthesizer.cpp
        Source/TextureSynthesizer.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
)

target_compile_definitions(LoopSurgeon
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LoopSurgeon
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(LoopSurgeon PRIVATE /utf-8)
endif()

if(SOUND_VST_BUILD_TESTS)
    juce_add_console_app(LoopSurgeonTests PRODUCT_NAME "Loop Surgeon Tests")

    target_sources(LoopSurgeonTests
        PRIVATE
            Source/LoopEngine.cpp
            Source/LoopEngine.h
            Source/LoopAnalyzer.cpp
            Source/LoopAnalyzer.h
            Source/SignalDiagnostics.cpp
            Source/SignalDiagnostics.h
            Source/TextureSynthesizer.cpp
            Source/TextureSynthesizer.h
            Tests/LoopEngineTests.cpp
    )

    target_include_directories(LoopSurgeonTests PRIVATE Source)

    target_link_libraries(LoopSurgeonTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    if(MSVC)
        target_compile_options(LoopSurgeonTests PRIVATE /utf-8)
    endif()

    add_test(NAME LoopSurgeon.Engine COMMAND LoopSurgeonTests)

endif()

