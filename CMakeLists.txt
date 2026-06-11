cmake_minimum_required(VERSION 3.22)
project(Puponvst VERSION 1.0.4 LANGUAGES C CXX)

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

# ===== Rubber Band Library（GPL，适用于本开源项目）=====
# 仅拉取源码，不执行它的 Meson 构建；我们用官方提供的 single-file unity
# （single/RubberBandSingle.cpp）把整个库以源码形式编进本插件。
FetchContent_Declare(
        rubberband
        GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
        GIT_TAG        v4.0.0
)
# 用 Populate 而不是 MakeAvailable，避免触发其顶层构建脚本
FetchContent_GetProperties(rubberband)
if(NOT rubberband_POPULATED)
    FetchContent_Populate(rubberband)
endif()

# Apply local Rubber Band patch (v4.0.0) in a reproducible way.
set(RUBBERBAND_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/rubberband-v4.0.0-resampler-quality.patch")
if(EXISTS "${RUBBERBAND_PATCH_FILE}")
    find_package(Git REQUIRED)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${rubberband_SOURCE_DIR}" apply --check "${RUBBERBAND_PATCH_FILE}"
        RESULT_VARIABLE RB_PATCH_CAN_APPLY
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(RB_PATCH_CAN_APPLY EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${rubberband_SOURCE_DIR}" apply "${RUBBERBAND_PATCH_FILE}"
            RESULT_VARIABLE RB_PATCH_APPLY_RESULT
            OUTPUT_QUIET
            ERROR_VARIABLE RB_PATCH_APPLY_ERROR
        )
        if(NOT RB_PATCH_APPLY_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply Rubber Band patch: ${RB_PATCH_APPLY_ERROR}")
        endif()
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${rubberband_SOURCE_DIR}" apply --reverse --check "${RUBBERBAND_PATCH_FILE}"
            RESULT_VARIABLE RB_PATCH_ALREADY_APPLIED
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT RB_PATCH_ALREADY_APPLIED EQUAL 0)
            message(FATAL_ERROR "Rubber Band source does not match expected v4.0.0 baseline for patch ${RUBBERBAND_PATCH_FILE}")
        endif()
    endif()
else()
    message(FATAL_ERROR "Missing required Rubber Band patch file: ${RUBBERBAND_PATCH_FILE}")
endif()

juce_add_plugin(
        Puponvst
        PRODUCT_NAME "Pupon"
        VERSION 1.0.4
        COMPANY_NAME "iisaacbeas.cn"
        BUNDLE_ID "cn.iisaacbeas.Pupon"
        PLUGIN_MANUFACTURER_CODE Pupo
        PLUGIN_CODE Pupn
        # 注意：AU 仅能在 macOS 上构建，其它平台会被 JUCE 自动忽略
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST FALSE
        # AU 必需的插件分类（效果器）
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(Puponvst)

# ===== 字体资源：仅打包标题 "PUPON" 所需的子集字体 =====
juce_add_binary_data(PuponvstBinaryData
  SOURCES
    otf/AtomicMarker-PUPON-subset.otf
)

target_compile_definitions(Puponvst
  PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(Puponvst PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h
  PitchShiftEngine.cpp
  PitchShiftEngine.h
  # Rubber Band single-file unity build（见官方 single/RubberBandSingle.cpp 说明，
  # 里面已内置 USE_BUILTIN_FFT / USE_BQRESAMPLER / NO_THREADING 等宏）
  ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
)

# 针对 Rubber Band 源文件单独设置 NOMINMAX（防止 <windows.h> 的 min/max 宏
# 污染 std::min / std::max）以及 _USE_MATH_DEFINES（Rubber Band 用了 M_PI 等）。
# 只作用于该源文件，不影响 JUCE 及本插件其它代码。
# 
# 保留 Rubber Band 默认质量逻辑，运行时由插件参数控制质量
if(MSVC)
    set_source_files_properties(
        ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "NOMINMAX;_USE_MATH_DEFINES;_CRT_SECURE_NO_WARNINGS"
    )
else()
    set_source_files_properties(
        ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "_USE_MATH_DEFINES"
    )
endif()

# Rubber Band 头文件位置：<include>/rubberband/RubberBandLiveShifter.h
target_include_directories(Puponvst PRIVATE
  ${rubberband_SOURCE_DIR}
)

# Apple 平台下 Rubber Band 会用 vDSP（Accelerate framework）
if(APPLE)
    target_link_libraries(Puponvst PRIVATE "-framework Accelerate")
endif()

target_link_libraries(Puponvst PRIVATE juce::juce_audio_utils juce::juce_dsp PuponvstBinaryData)
