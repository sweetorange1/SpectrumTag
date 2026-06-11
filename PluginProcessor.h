#pragma once

#include <JuceHeader.h>
#include <array>
#include "PitchShiftEngine.h"

// ===== 参数ID定义（供 Processor 和 Editor 共用）=====
namespace ParameterIDs
{
    static const juce::String redRayClockwiseAngle = "redRayClockwiseAngle";
    static const juce::String isVerticalRay = "isVerticalRay";
    static const juce::String sigma          = "sigma";
    static const juce::String filterCenterSt = "filterCenterSt";
    static const juce::String filterWidthSt  = "filterWidthSt";
    static const juce::String rbPitchQuality = "rbPitchQuality";
    static const juce::String rbFormantMode  = "rbFormantMode";
    static const juce::String dot0          = "dot0";
    static const juce::String dot1          = "dot1";
    static const juce::String dot2          = "dot2";
    static const juce::String dot3          = "dot3";
    static const juce::String dot4          = "dot4";
    static const juce::String dot0Semitone  = "dot0Semitone";
    static const juce::String dot1Semitone  = "dot1Semitone";
    static const juce::String dot2Semitone  = "dot2Semitone";
    static const juce::String dot3Semitone  = "dot3Semitone";
    static const juce::String dot4Semitone  = "dot4Semitone";
}

class PuponvstAudioProcessor : public juce::AudioProcessor,
                                  public juce::AudioProcessorValueTreeState::Listener
{
public:
    PuponvstAudioProcessor();
    ~PuponvstAudioProcessor() override;

    // AudioProcessor overrides
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // 参数系统：暴露给宿主用于自动化控制 =====
    // 使用 AudioProcessorValueTreeState 来管理参数
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // 获取 APVTS 引用（供 Editor 使用）
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // AudioProcessorValueTreeState::Listener 接口 - 参数改变回调
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // ===== 持久化的 UI 控制状态（由 Editor 拥有的"模型层"数值在此存档） =====
    // 设计：Editor 是临时组件（用户随时关闭/重开），状态必须存放在 Processor。
    //   - 每次用户交互后 Editor 主动调用 setEditorState(...) 推送一份镜像
    //   - setStateInformation 反序列化时填充这些字段
    //   - 新 Editor 构造时调用 getEditorState(...) 回填到自己的成员
    struct EditorState
    {
        // 蓝线角度（度，数学坐标）：0° = 水平向右，90° = 垂直向上，180° = 水平向左
        // 红线角度 = 180° - blueAngleDeg（左右对称）
        float blueAngleDeg = 90.0f;  // 默认垂直向上

        // 红线顺时针角度（以水平向左为 0°）：
        // 0° = 水平向左，90° = 垂直向上，180° = 水平向右
        // 与 blueAngleDeg 数值等价（redClockwise = blueAngleDeg）
        float redRayClockwiseDeg = 90.0f;
        bool  isVerticalRay = false;
        // 正态曲线方差（标准差）
        float sigma        = 2.70f;
        // 5 个圆点在轨道上的归一化高度，1.0 = 顶端（默认最大），0.0 = 底部
        std::array<float, 5> dotOffsetT { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        // 5 个圆点的半音偏移（单位 st，始终为整数）
        std::array<int, 5> dotSemitoneOffsets { -24, -12, 0, +12, +24 };
        // 是否包含已保存的有效内容（Processor 刚被构造、尚未恢复时为 false）
        bool  hasValidValues = false;
    };

    void setEditorState(const EditorState& s);
    EditorState getEditorState() const;

    // 提供给界面线程使用：获取示波器数据快照（按时间从旧到新排列）
    void getOscilloscopeSnapshot(juce::Array<float>& dest);

    // 提供给界面线程：获取当前活动的band数量（gain > 0 的band数量，用于调试显示）
    int getActiveBandsCount() const { return pitchEngine.getActiveBandsCount(); }

    // 提供给界面线程：把 5 个圆点的当前 gain / pan 同步给音频线程（lock-free）
    // index ∈ [0, 4]，gain ∈ [0, 1]，pan ∈ [-1, +1]
    void setDotGain(int index, float gain) { pitchEngine.setDotGain(index, gain); }
    void setDotPan (int index, float pan ) { pitchEngine.setDotPan (index, pan ); }
    void setDotSemitoneOffset(int index, int semitone) { pitchEngine.setDotSemitoneOffset(index, semitone); }
    void setFilterCenterOffsetSemitones(int centerOffsetSt) { pitchEngine.setFilterCenterOffsetSemitones(centerOffsetSt); }
    void setFilterWidthSemitones(int widthSt) { pitchEngine.setFilterWidthSemitones(widthSt); }
    void setPitchQualityMode(int mode) { pitchEngine.setPitchQualityMode(mode); }
    void setFormantMode(int mode) { pitchEngine.setFormantMode(mode); }

    // 视觉同步：UI 通过此 getter 获取相关状态

private:
    void syncEngineFromParameters();
    void updateDotGainsAndPansFromParameters() noexcept;
    static float computePanFromBlueAngleAndSemitone(float blueAngleDeg, int semitone) noexcept;
    static float computeGainFromDotState(float dotOffsetT, int semitone, float sigma) noexcept;
    void armStateSwitchFadeIn() noexcept;

    // 参数系统
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int oscilloscopeBufferSize = 2048;

    void pushSamplesToOscilloscope(const float* samples, int numSamples);

    juce::SpinLock oscilloscopeLock;
    std::array<float, oscilloscopeBufferSize> oscilloscopeBuffer {};
    int oscilloscopeWritePos = 0;

    // 音频处理引擎（重采样 pitch shift + 5 路混音 + 声相 + 硬削波）
    PitchShiftEngine pitchEngine;

    // 缓存上次读到的"一小节秒数"，用于相关处理（默认 120 BPM 4/4 = 2s）
    double lastBarSeconds = 2.0;

    // 记录 prepareToPlay 环境，供运行中重建 shifter 选项
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
    int lastReportedLatencySamples = -1;
    std::atomic<bool> pitchEngineOptionsDirty { false };
    int stateSwitchFadeOutSamplesTotal = 0;
    int stateSwitchFadeInSamplesTotal = 0;
    std::atomic<int> stateSwitchFadeOutSamplesRemaining { 0 };
    std::atomic<int> stateSwitchFadeInSamplesRemaining { 0 };
    std::atomic<bool> isRestoringState { false };
    std::array<float, 2> lastOutputSample { 0.0f, 0.0f };

    // ===== 持久化的 UI 状态镜像（GUI 关闭时此处保留最新值，宿主存档读取于此）=====
    // 由 Editor 在每次交互后通过 setEditorState() 推送；Editor 重新打开时通过 getEditorState() 拉取。
    // 用 SpinLock 保护：写入只发生在消息线程（Editor 交互、setStateInformation），读取也只发生在消息线程
    // （新 Editor 构造、getStateInformation 由 host 在主线程调用），SpinLock 足够。
    mutable juce::SpinLock editorStateLock;
    EditorState            editorState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PuponvstAudioProcessor)
};
