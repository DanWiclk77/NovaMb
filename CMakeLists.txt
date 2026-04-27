cmake_minimum_required(VERSION 3.22)

project(NovaMB_VST3 VERSION 1.0.0)

# Set up JUCE (This expects JUCE to be available as a subdirectory or installed)
# In GitHub Actions, we typically fetch it or use a pre-installed one.
add_subdirectory(JUCE) 

juce_add_plugin(NovaMB
    PRODUCT_NAME "NovaMB"
    COMPANY_NAME "NovaAudio"
    BUNDLE_ID com.novaaudio.novamb
    PLUGIN_MANUFACTURER_CODE Nova
    PLUGIN_CODE NvMB
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Dynamics Compressor
)

target_sources(NovaMB PRIVATE
    Source/MultibandEngine.h
    Source/MultibandEngine.cpp
    # Agrega aquí tus archivos PluginProcessor y PluginEditor generados por Projucer o creados manualmente
)

target_compile_definitions(NovaMB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(NovaMB PRIVATE
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
)
