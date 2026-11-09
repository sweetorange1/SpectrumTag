cmake_minimum_required(VERSION 3.22)

# ============================================================================
#  macOS：默认产出 Apple Silicon + Intel 双架构（universal / fat）二进制
# ----------------------------------------------------------------------------
#  CMAKE_OSX_ARCHITECTURES 为空时（默认）自动填 "arm64;x86_64"，
#  Standalone / VST3 / AU 三个产物都会是双架构，Intel 与 M 系列机器都能加载。
#  只想出单架构时显式覆盖即可：
#     -DCMAKE_OSX_ARCHITECTURES=arm64     （仅 Apple Silicon）
#     -DCMAKE_OSX_ARCHITECTURES=x86_64    （仅 Intel）
#  注意：该变量必须在 project() 之前设置，否则不生效。
# ============================================================================
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "macOS architectures (default: universal = Apple Silicon + Intel)" FORCE)
    endif()
    # arm64 的 deployment target 必须 >= 11.0（Big Sur）
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Minimum macOS version (arm64 requires 11.0+)" FORCE)
    endif()
    message(STATUS "[SpectrumTag] macOS architectures : ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "[SpectrumTag] macOS deployment    : ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

project(SpectrumTag VERSION 1.4.28 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(
        SpectrumTag
        PRODUCT_NAME "SpectrumTag"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.SpectrumTag"
        # 四字符码必须全局唯一：VST3 / AU 的 class ID 由
        # (ManufacturerCode, PluginCode) 唯一推导（见 JUCE 的
        # VST3Interface::jucePluginId）。此前误用 Pupon 的 Pupo/Pupn，
        # 导致两个插件 UID 完全相同，宿主加载 SpectrumTag 时实际打开的是 Pupon。
        PLUGIN_MANUFACTURER_CODE Iisa
        PLUGIN_CODE SpTg
        # 注意：AU 仅能在 macOS 上构建，其它平台会被 JUCE 自动忽略
        # Standalone 版由下方独立 target SpectrumTagStandalone 提供（工作模式完全不同），
        # 所以这里不再让 juce_add_plugin 生成插件式 Standalone wrapper，避免出现同名 exe
        FORMATS VST3 AU
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST TRUE
        # AU 必需的插件分类（效果器）
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE
        # https 请求（遥测 / 更新检查）需要网络后端支持
        NEEDS_CURL FALSE
)

# JUCE 8.0.15's resource command depends on the icon but omits Info.txt.
# Without this edge, incremental Windows builds retain an older PE file version.
if(WIN32)
    get_target_property(spectrumtag_generated SpectrumTag JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(spectrumtag_info SpectrumTag JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${spectrumtag_generated}/SpectrumTag_resources.rc"
        APPEND DEPENDS "${spectrumtag_info}")
endif()

juce_generate_juce_header(SpectrumTag)

# ===== 字体资源：仅打包标题 "SpectrumTag" 所需的子集字体 =====
juce_add_binary_data(SpectrumTagBinaryData
  SOURCES
        otf/BasementGrotesque-Black_v1.202.otf
)

target_compile_definitions(SpectrumTag
  PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(SpectrumTag PRIVATE
  PluginProcessor.cpp
  shared/Recording.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h
  shared/IisaacTelemetry.h
  source/network/Version.h
  source/network/Version.cpp
  source/network/UpdateChecker.h
  source/network/UpdateChecker.cpp
  source/ui/UpdateDialog.h
  source/ui/UpdateDialog.cpp
)

target_link_libraries(SpectrumTag PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra juce::juce_cryptography SpectrumTagBinaryData)

# ============================================================================
#  SpectrumTagStandalone —— 独立可执行程序（非 VST3 插件）
# ----------------------------------------------------------------------------
#  与 VST3 插件共用同一份 UI 视觉和核心 STFT/OLA 算法，但工作模式完全不同：
#   - 用户拖入本地音频文件（WAV/MP3/FLAC 等 JUCE 支持的格式）；
#   - 左侧显示整段音频的静态时频图；Speed 参数横向拉伸；可用鼠标横向拖动；
#   - 拖入图片后点 Print → 弹出目录选择 → 后台线程离线合成 → 输出 WAV。
# ============================================================================
juce_add_gui_app(
        SpectrumTagStandalone
        PRODUCT_NAME "SpectrumTag"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.SpectrumTagStandalone"
        ICON_BIG "UI.png"
)

juce_generate_juce_header(SpectrumTagStandalone)

# Windows portable EXE: compressed codec and notices are PE resources, not a
# companion folder. Set this path when building the standalone target.
set(SPECTRUMTAG_STANDALONE_RUNTIME_DIR "" CACHE PATH "Directory with ffmpeg.exe, COPYING.GPLv3 and FFmpeg-NOTICE.txt for the portable EXE")
if(WIN32 AND SPECTRUMTAG_STANDALONE_RUNTIME_DIR)
    set(codec_generated "${CMAKE_CURRENT_BINARY_DIR}/embedded-codec")
    file(MAKE_DIRECTORY "${codec_generated}")
    set(codec_files ffmpeg.exe COPYING.GPLv3 FFmpeg-NOTICE.txt)
    set(codec_dependencies)
    foreach(codec_file IN LISTS codec_files)
        if(NOT EXISTS "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}/${codec_file}")
            message(FATAL_ERROR "Portable standalone codec file missing: ${codec_file}")
        endif()
        list(APPEND codec_dependencies "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}/${codec_file}")
    endforeach()
    file(SHA256 "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}/ffmpeg.exe" CODEC_EXE_HASH)
    file(SHA256 "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}/COPYING.GPLv3" CODEC_LICENSE_HASH)
    file(SHA256 "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}/FFmpeg-NOTICE.txt" CODEC_NOTICE_HASH)
    set(CODEC_ARCHIVE "${codec_generated}/runtime.zip")
    add_custom_command(OUTPUT "${CODEC_ARCHIVE}"
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${CODEC_ARCHIVE}" --format=zip -- ${codec_files}
        WORKING_DIRECTORY "${SPECTRUMTAG_STANDALONE_RUNTIME_DIR}"
        DEPENDS ${codec_dependencies} VERBATIM)
    add_custom_target(SpectrumTagEmbeddedCodec DEPENDS "${CODEC_ARCHIVE}")
    add_dependencies(SpectrumTagStandalone SpectrumTagEmbeddedCodec)
    configure_file(Standalone/EmbeddedCodec.rc.in "${codec_generated}/EmbeddedCodec.rc" @ONLY)
    configure_file(Standalone/EmbeddedCodecConfig.h.in "${codec_generated}/EmbeddedCodecConfig.h" @ONLY)
    set_source_files_properties("${codec_generated}/EmbeddedCodec.rc" PROPERTIES OBJECT_DEPENDS "${CODEC_ARCHIVE}")
    target_sources(SpectrumTagStandalone PRIVATE "${codec_generated}/EmbeddedCodec.rc")
    target_include_directories(SpectrumTagStandalone PRIVATE "${codec_generated}")
    target_compile_definitions(SpectrumTagStandalone PRIVATE SPECTRUMTAG_EMBEDDED_CODEC=1)
endif()
target_sources(SpectrumTagStandalone PRIVATE Standalone/EmbeddedCodec.cpp)
target_link_libraries(SpectrumTagStandalone PRIVATE juce::juce_cryptography)


target_sources(SpectrumTagStandalone PRIVATE Standalone/ModernStandalone.cpp Standalone/AudioFileSession.h Standalone/SpectrumVideo.cpp)
target_include_directories(SpectrumTagStandalone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_PROPERTY:SpectrumTag,INCLUDE_DIRECTORIES>)
target_compile_definitions(SpectrumTagStandalone PRIVATE $<TARGET_PROPERTY:SpectrumTag,COMPILE_DEFINITIONS> JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpectrumTagStandalone PRIVATE SpectrumTag juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_gui_extra SpectrumTagBinaryData)
if(MSVC)
    target_compile_options(SpectrumTagStandalone PRIVATE /utf-8)
endif()
if(WIN32)
    get_target_property(standalone_generated SpectrumTagStandalone JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(standalone_info SpectrumTagStandalone JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${standalone_generated}/SpectrumTagStandalone_resources.rc" APPEND DEPENDS "${standalone_info}")
endif()

# ============================================================================
#  SpectrumTag_All 聚合入口
# ----------------------------------------------------------------------------
#  juce_add_plugin 自动生成的 SpectrumTag_All 只聚合插件格式（VST3 / AU）；
#  Standalone 是独立 gui_app target，默认不在其中，所以只构建 SpectrumTag_All
#  时不会产出 SpectrumTag.exe。这里把 Standalone 挂成依赖，
#  构建 SpectrumTag_All 即同时产出插件与独立程序。
# ============================================================================
add_dependencies(SpectrumTag_All SpectrumTagStandalone)
option(SPECTRUMTAG_ENABLE_NETWORK "Enable upstream telemetry and update checks" OFF)
target_compile_definitions(SpectrumTag PUBLIC SPECTRUMTAG_ENABLE_NETWORK=$<BOOL:${SPECTRUMTAG_ENABLE_NETWORK}>)
option(SPECTRUMTAG_BUILD_TESTS "Build offline processor verification" OFF)
if(SPECTRUMTAG_BUILD_TESTS)
    juce_add_console_app(SpectrumTagVerify PRODUCT_NAME "SpectrumTagVerify")
    juce_generate_juce_header(SpectrumTagVerify)
    target_sources(SpectrumTagVerify PRIVATE tests/Verify.cpp)
    target_include_directories(SpectrumTagVerify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(SpectrumTagVerify PRIVATE SpectrumTag)
    target_include_directories(SpectrumTagVerify PRIVATE $<TARGET_PROPERTY:SpectrumTag,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SpectrumTagVerify PRIVATE $<TARGET_PROPERTY:SpectrumTag,COMPILE_DEFINITIONS>)
endif()

if(MSVC)
    target_compile_options(SpectrumTag PRIVATE /utf-8)
    if(SPECTRUMTAG_BUILD_TESTS)
        target_compile_options(SpectrumTagVerify PRIVATE /utf-8)
    endif()
endif()

# Dedicated ARA companion. Realtime SpectrumTag keeps its existing identity/version.
option(SPECTRUMTAG_BUILD_ARA "Build the dedicated ARA VST3" OFF)
set(SPECTRUMTAG_ARA_SDK_DIR "" CACHE PATH "Celemony ARA SDK 2.3 directory")
if(SPECTRUMTAG_BUILD_ARA)
    if(NOT SPECTRUMTAG_ARA_SDK_DIR)
        FetchContent_Declare(spectrumtag_ara_sdk
            GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
            GIT_TAG 0005c532bb03eb210e7bb490329fc6d7ba1792c7
            GIT_SUBMODULES ARA_API ARA_Library)
        FetchContent_MakeAvailable(spectrumtag_ara_sdk)
        set(SPECTRUMTAG_ARA_SDK_DIR "${spectrumtag_ara_sdk_SOURCE_DIR}")
    endif()
    juce_set_ara_sdk_path("${SPECTRUMTAG_ARA_SDK_DIR}")
    set(spectrumtag_ara_formats VST3)
    if(APPLE)
        list(APPEND spectrumtag_ara_formats AU)
    endif()
    juce_add_plugin(SpectrumTagARA
        PRODUCT_NAME "SpectrumTag ARA"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.SpectrumTagARA"
        PLUGIN_MANUFACTURER_CODE Iisa PLUGIN_CODE SpAr
        FORMATS ${spectrumtag_ara_formats} IS_ARA_EFFECT TRUE
        AU_MAIN_TYPE kAudioUnitType_Effect
        AU_EXPORT_PREFIX SpectrumTagARA
        ARA_FACTORY_ID "\"cn.iisaacbeats.SpectrumTagARA.factory.1\""
        ARA_DOCUMENT_ARCHIVE_ID "\"cn.iisaacbeats.SpectrumTagARA.archive.1\""
        ARA_TRANSFORMATION_FLAGS kARAPlaybackTransformationNoChanges
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx OnlyARA
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_AUTO_MANIFEST TRUE COPY_PLUGIN_AFTER_BUILD FALSE NEEDS_CURL FALSE)
    # JUCE injects Fx when no main category is supplied. This target is an
    # extension-only container, so do not advertise it as an ordinary insert.
    foreach(category_property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(ara_definitions SpectrumTagARA ${category_property})
        list(FILTER ara_definitions EXCLUDE REGEX "^JucePlugin_Vst3Category=")
        set_property(TARGET SpectrumTagARA PROPERTY ${category_property} "${ara_definitions}")
    endforeach()
    target_compile_definitions(SpectrumTagARA PUBLIC JucePlugin_Vst3Category="OnlyARA")
    juce_generate_juce_header(SpectrumTagARA)
    target_sources(SpectrumTagARA PRIVATE PluginProcessor.cpp PluginEditor.cpp shared/Recording.cpp
        ARA/ARAIntegration.cpp source/network/Version.cpp source/network/UpdateChecker.cpp source/ui/UpdateDialog.cpp)
    target_compile_definitions(SpectrumTagARA PUBLIC SPECTRUMTAG_ARA=1 SPECTRUMTAG_ENABLE_NETWORK=0
        JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(SpectrumTagARA PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra juce::juce_cryptography SpectrumTagBinaryData)
    if(WIN32)
        get_target_property(ara_generated SpectrumTagARA JUCE_GENERATED_SOURCES_DIRECTORY)
        get_target_property(ara_info SpectrumTagARA JUCE_INFO_FILE)
        add_custom_command(OUTPUT "${ara_generated}/SpectrumTagARA_resources.rc" APPEND DEPENDS "${ara_info}")
    endif()
    if(MSVC)
        target_compile_options(SpectrumTagARA PRIVATE /utf-8)
    endif()
    if(SPECTRUMTAG_BUILD_TESTS)
        add_subdirectory("${SPECTRUMTAG_ARA_SDK_DIR}/ARA_Library" "${CMAKE_CURRENT_BINARY_DIR}/ARA_Library" EXCLUDE_FROM_ALL)
        juce_add_console_app(SpectrumTagARAVerify PRODUCT_NAME "SpectrumTagARAVerify")
        juce_generate_juce_header(SpectrumTagARAVerify)
        target_sources(SpectrumTagARAVerify PRIVATE tests/ARAVerify.cpp
            "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp")
        target_include_directories(SpectrumTagARAVerify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_PROPERTY:SpectrumTagARA,INCLUDE_DIRECTORIES>)
        target_compile_definitions(SpectrumTagARAVerify PRIVATE $<TARGET_PROPERTY:SpectrumTagARA,COMPILE_DEFINITIONS>)
        target_link_libraries(SpectrumTagARAVerify PRIVATE SpectrumTagARA ARA_Host_Library juce::juce_audio_plugin_client)
        if(APPLE)
            target_link_libraries(SpectrumTagARAVerify PRIVATE "-framework CoreFoundation")
        endif()
        if(MSVC)
            target_compile_options(SpectrumTagARAVerify PRIVATE /utf-8)
        endif()
    endif()
endif()
