cmake_minimum_required(VERSION 3.22)
project(LDSJvst VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
)
FetchContent_MakeAvailable(juce)

add_subdirectory(grid_plugin)

juce_add_plugin(
        LDSJvst
        PRODUCT_NAME "LDSJvst"
        VERSION 0.1.0
        COMPANY_NAME "YourCompany"
        PLUGIN_MANUFACTURER_CODE Ldsj
        PLUGIN_CODE Ldsj
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(LDSJvst)

target_compile_definitions(LDSJvst
  PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(LDSJvst PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h
)

target_link_libraries(LDSJvst PRIVATE grid::grid_plugin juce::juce_audio_utils)
