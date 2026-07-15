cmake_minimum_required(VERSION 3.22)
project(SpectrumTag VERSION 1.2.1 LANGUAGES C CXX)

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

juce_add_plugin(
        SpectrumTag
        PRODUCT_NAME "SpectrumTag"
        VERSION 1.2.9
        COMPANY_NAME "iisaacbeas.cn"
        BUNDLE_ID "cn.iisaacbeas.SpectrumTag"
        PLUGIN_MANUFACTURER_CODE Pupo
        PLUGIN_CODE Pupn
        # 注意：AU 仅能在 macOS 上构建，其它平台会被 JUCE 自动忽略
        # Standalone 版由下方独立 target SpectrumTagStandalone 提供（工作模式完全不同），
        # 所以这里不再让 juce_add_plugin 生成插件式 Standalone wrapper，避免出现同名 exe
        FORMATS VST3 AU
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST FALSE
        # AU 必需的插件分类（效果器）
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD TRUE
)

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
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h
)

target_link_libraries(SpectrumTag PRIVATE juce::juce_audio_utils juce::juce_dsp SpectrumTagBinaryData)

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
        VERSION 1.2.9
        COMPANY_NAME "iisaacbeas.cn"
        BUNDLE_ID "cn.iisaacbeas.SpectrumTagStandalone"
        ICON_BIG "UI.png"
)

juce_generate_juce_header(SpectrumTagStandalone)

target_sources(SpectrumTagStandalone PRIVATE
        Standalone/StandaloneMain.cpp
        Standalone/StandaloneWindow.cpp
        Standalone/StandaloneWindow.h
        Standalone/OfflineRenderer.cpp
        Standalone/OfflineRenderer.h
        Standalone/SharedUI.cpp
        Standalone/SharedUI.h
)

target_compile_definitions(SpectrumTagStandalone
  PUBLIC
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(SpectrumTagStandalone PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        SpectrumTagBinaryData)