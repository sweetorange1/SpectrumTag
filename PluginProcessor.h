#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>

// ============================================================================
//  SpectrumTag - 音频信号 × 图片轮廓 的频谱"印章"插件
// ----------------------------------------------------------------------------
//  设计要点（v3 STFT/OLA 方案）：
//   - 未按 Print 时：音频完全 bypass，不做任何 FFT/IFFT 运算，零延迟。
//   - 按下 Print 后：STFT（75% 重叠，hop = N/4），每个 bin 根据 mask 独立
//     调节幅度，保留原始相位，IFFT 后重叠相加（OLA）输出。
//     配合逐 bin 增益平滑 + 相位保留，彻底消除音量波动。
//   - 显示路径（瀑布图）保持独立实数 FFT，仅用于可视化。
// ============================================================================

namespace ParameterIDs
{
    static const juce::String fftSize        = "fftSize";        // Choice: 1024/2048/4096/8192
    static const juce::String fftScale       = "fftScale";       // Choice: linear / mel
    static const juce::String speed          = "speed";          // Float
    static const juce::String amplitudeRatio = "amplitudeRatio"; // Float: 0.0 - 1.5
    static const juce::String invert         = "invert";         // Bool
    static const juce::String printTrigger   = "printTrigger";   // Bool（自动化触发 Print，按上升沿）
}

class SpectrumTagAudioProcessor : public juce::AudioProcessor,
                                  public juce::AudioProcessorValueTreeState::Listener
{
public:
    SpectrumTagAudioProcessor();
    ~SpectrumTagAudioProcessor() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // ===== 频谱可视化（仅显示用 FFT）=====
    void getLatestMagnitudeSpectrum (std::vector<float>& dest);
    int  getDisplayUpdateCounter() const { return displayUpdateCounter.load (std::memory_order_acquire); }

    // ===== Editor → Processor 状态镜像 =====
    struct EditorState
    {
        juce::String imagePath;
        float imgRectXNorm = 0.30f;
        float imgRectYNorm = 0.20f;
        float imgRectWNorm = 0.40f;
        float imgRectHNorm = 0.50f;
        bool  hasValidValues = false;
    };

    void setEditorState (const EditorState& s);
    EditorState getEditorState() const;

    // ===== Print 流程 =====
    void startPrint (std::vector<float>&& maskData,
                     int maskRows, int maskCols,
                     float freqLowNorm, float freqHighNorm,
                     double durationSeconds);
    bool isPrintRunning() const { return printRunning.load(); }

    // ===== Print 数学日志 =====
    //
    // 编译期总开关（临时禁用日志生成）：
    //   - 0 = 禁用：跳过所有 BlockStats 采集 / pushBlockStats / CSV 写文件，
    //         消除日志开销引起的"打印"卡顿（实时性能测试用）。
    //   - 1 = 启用：恢复完整的处理前后块统计与 CSV 输出。
    // 所有相关逻辑代码、结构、函数定义均保留，便于一键恢复。
    #ifndef SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
        #define SPECTRUMTAG_PRINT_MATH_LOG_ENABLED 0
    #endif

    void markPrintClickAndBeginMathLog();
    void flushPrintMathLogToFile();

    // ===== Print Trigger 自动化（外部 host 控制） =====
    //
    // 触发模型：上升沿（false → true）。
    //  - DAW 自动化曲线 / MIDI Learn / 工程加载等任何来源的参数变动都会触发
    //    parameterChanged。我们只在 false→true 的瞬间记录一次"打印请求"，
    //    交由 Editor 的 message-thread timer 真正发起 onPrintClicked。
    //  - true→true / true→false 不触发；这样可以兼容自动化曲线的"按住"形态。
    //  - 为了避免 DAW 加载工程瞬间因参数回放产生伪边沿，setStateInformation
    //    会启动一段短暂的"加载抑制窗口"，期间忽略所有 trigger 边沿。
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    bool consumeAutomationPrintRequest();   // Editor timer 调用：取走一次自动化触发请求
    juce::AudioParameterBool* getPrintTriggerParam() const noexcept { return printTriggerParam; }

private:
    // =========================================================================
    //  STFT / OLA 状态（每通道独立）
    // =========================================================================
    struct StftChannelState
    {
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window;          // Hann 窗，长度 stftSize
        std::vector<float> fftWork;         // 2 * stftSize，复数值交错存储

        // 输入环形缓冲
        std::vector<float> inputRing;       // 长度 stftSize
        int                inputPos = 0;    // 下一个写入位置
        int                accumCount = 0;  // 自上一帧以来累积的采样数

        // 干声延迟线（用于与 STFT wet 路径按同一延迟对齐，避免 crossfade 错位）
        std::vector<float> dryDelayRing;    // 长度 stftSize（延迟读取长度为 stftSize - stftHop）
        int                dryDelayWritePos = 0;

        // 输出 OLA 累积环形缓冲
        std::vector<float> outputRing;      // 长度 stftSize（accumulator）
        std::vector<float> olaNormRing;     // 长度 stftSize（累积 w^2，用于逐样本归一化）

        // 输出 FIFO 环形缓冲（解耦 OLA 与输出读取）
        std::vector<float> outFifo;         // 长度 stftSize × 2（安全余量）
        int                outFifoWrite = 0;
        int                outFifoRead  = 0;
        int                outFifoCount = 0;

        // OLA 帧计数：只有 frameCount >= stftSize/stftHop 后才能放出采样到 FIFO
        int                frameCount = 0;

        // 逐 bin 增益平滑状态
        std::vector<float> smoothedGains;   // 长度 numBins = stftSize/2+1

        // 数学日志探针（最近一次 STFT 帧）
        float              lastTargetGainProbe = 1.0f;
        float              lastSmoothGainProbe = 1.0f;
        float              lastFftMagProbe = 0.0f;
        float              lastIfftProbe = 0.0f;
        float              lastOlaNormProbe = 0.0f;
        int                lastProcessedFrame = 0;
    };

    juce::AudioProcessorValueTreeState apvts;

    // ===== Editor 状态镜像 =====
    mutable juce::SpinLock editorStateLock;
    EditorState            editorState;

    // ===== 频谱可视化：FFT 显示路径（独立，不参与音频处理）=====
    mutable juce::SpinLock spectrumLock;
    std::vector<float>     latestMagnitudes;
    std::atomic<int>       displayUpdateCounter { 0 };

    int                             displayFftSize = 0;
    int                             displayHopSize = 0;
    std::unique_ptr<juce::dsp::FFT> displayFft;
    std::vector<float>              displayWindow;
    std::vector<float>              displayInputRing;
    int                             displayRingWritePos = 0;
    int                             displaySamplesAccum = 0;
    std::vector<float>              displayFftWork;

    // ===== Print 状态 =====
    std::atomic<bool> printRunning { false };
    int                wasPrintActive = 0;       // 检测 Print 启动瞬间，用于重置 OLA 管道
    juce::SpinLock     printLock;
    std::vector<float> printMask;
    int                printMaskRows = 0;
    int                printMaskCols = 0;
    float              printFreqLowNorm = 0.0f;
    float              printFreqHighNorm = 1.0f;
    double             printDurationSec = 0.0;
    double             printColCursorD = 0.0;     // 当前列游标（小数，精确推进）
    double             printColPerSample = 0.0;
    int                printWarmupFramesRemaining = 0; // Print 启动预热帧数（预热期不消费 mask 列）

    // ===== 干湿切换 crossfade 状态 =====
    bool               prevPrintActive = false;
    float              dryWetMix = 0.0f;            // 0=dry, 1=wet
    float              dryWetTarget = 0.0f;
    float              dryWetFadeStartMix = 0.0f;   // ramp 起点 mix（启动 fade 瞬间的 dryWetMix 快照）
    int                dryWetFadeSamplesRemaining = 0;
    int                dryWetFadeTotalSamples = 0;
    int                printFadeDelaySamples = 0;    // Print 状态翻转后延迟启动 crossfade 的采样计数

    // ===== Print Trigger 自动化状态 =====
    juce::AudioParameterBool* printTriggerParam = nullptr;     // 在 createParameterLayout 中保留指针
    std::atomic<bool> lastPrintTriggerState { false };         // 上一次观察到的参数布尔值（用于检测上升沿）
    std::atomic<bool> automationPrintRequest { false };        // 上升沿置位 → 由 Editor timer 消费
    juce::int64       triggerSuppressEndTimeMs = 0;            // 加载抑制窗口结束的绝对 ms 时刻

    // ===== STFT 参数 =====
    int  stftSize = 4096;
    int  stftHop  = 1024;                         // stftSize / 4 (75% overlap)
    int  currentScaleMode = 0;                    // 0=linear, 1=mel（影响显示，不影响 STFT）
    std::vector<StftChannelState> stftStates;     // 每通道一个

    // ===== 数学日志会话（每次点击 Print 产生一次） =====
    struct MathLogEvent
    {
        int64_t sampleIndex = 0;
        int channel = 0;
        int stftFrame = 0;
        bool printRequested = false;
        bool stftPathActive = false;
        bool frameReady = false;
        bool printStateFlipped = false;
        int maskCol = -1;
        int fadeDelaySamples = 0;
        int fadeSamplesRemaining = 0;
        int printWarmupRemaining = 0;
        int accumCount = 0;
        float printColCursor = 0.0f;
        float targetGainMin = 1.0f;
        float targetGainMax = 1.0f;
        float targetGainMean = 1.0f;
        float dryIn = 0.0f;
        float delayedDry = 0.0f;
        float wetOut = 0.0f;
        float outSample = 0.0f;
        float dryWetMix = 0.0f;
        float dryWetTarget = 0.0f;
        float targetGainProbe = 1.0f;
        float smoothGainProbe = 1.0f;
        float fftMagProbe = 0.0f;
        float ifftProbe = 0.0f;
        float olaNormProbe = 0.0f;
        int outFifoCount = 0;
    };

    // -------------------------------------------------------------------------
    //  Block 级统计（每个 processBlock 入口/出口分别计算一组）
    //  专为定位"音量跳变""竖直细线（冲激式电流声）"问题而设计：
    //   - peak/rms/energy/dc：定量描述音量与能量层级
    //   - maxAbsDiff/slewSpikeCount：捕捉相邻样本突变（竖直细线 = 冲激 = 高斜率）
    //   - zeroCrossings：频率特征参考
    //   - clipCount/nonFiniteCount：过载与数值异常
    // -------------------------------------------------------------------------
    struct BlockStats
    {
        int64_t blockIndex = 0;            // 自插件启动以来第几个 block
        int64_t startSample = 0;           // 该 block 在全局采样序列中的起始样本
        int     numSamples = 0;
        int     channel = 0;
        bool    isPost = false;            // false=处理前，true=处理后
        bool    printRequested = false;    // 该 block 期间 printRunning 状态（采样中点）
        bool    stftPathActive = false;    // STFT/wet 路径是否参与输出
        float   dryWetMixSnapshot = 0.0f;  // 该 block 起始处 mix 值
        float   dryWetTargetSnapshot = 0.0f;
        int     fadeSamplesRemaining = 0;
        int     fadeDelaySamples = 0;
        int     accumCountSnapshot = 0;
        int     outFifoCountSnapshot = 0;

        // 数学定量指标
        float   minVal = 0.0f;
        float   maxVal = 0.0f;
        float   peakAbs = 0.0f;
        int     peakAbsPos = -1;           // 峰值在 block 内的样本下标
        double  mean = 0.0;                // = DC 偏移
        double  rms = 0.0;
        double  energy = 0.0;              // = sum(x^2)
        float   maxAbsDiff = 0.0f;         // 相邻样本最大|x[n]-x[n-1]|（冲激/竖直线检测）
        int     maxAbsDiffPos = -1;
        int     slewSpikeCount = 0;        // |x[n]-x[n-1]| > slewThresh 的次数
        int     zeroCrossings = 0;
        int     clipCount = 0;             // |x[n]| >= 1.0 的样本数
        int     nonFiniteCount = 0;        // NaN / Inf 数量

        // 与上一个 block 末尾样本的衔接差（跨 block 的边界冲激很可能就是电流声来源）
        float   boundaryDiff = 0.0f;       // |x[0] - prevBlockLastSample|
        float   prevBlockLastSample = 0.0f;
    };

    struct MathLogSession
    {
        bool active = false;
        bool requestFlush = false;
        bool collectingPost = false;
        int64_t postSamplesRemaining = 0;
        int64_t globalSampleCounter = 0;
        int64_t printClickSample = -1;
        int64_t sampleRateRounded = 44100;
        juce::String clickTimeIso;
        std::deque<MathLogEvent> preEvents;
        std::vector<MathLogEvent> frozenPreEvents;
        std::vector<MathLogEvent> postEvents;

        // ---- block 级别滚动缓冲（pre = 处理前，post = 处理后） ----
        // 同样以"点击 Print 前 1 秒滚动 + 点击后 1 秒"方式收集
        std::deque<BlockStats>  preBlockStatsRolling;     // 点击前的滚动窗口
        std::vector<BlockStats> frozenPreBlockStats;      // 点击瞬间冻结的快照
        std::vector<BlockStats> postBlockStats;           // 点击后收集
    };

    std::mutex mathLogMutex;
    MathLogSession mathLog;

    // 跨 block 衔接：保存上一个 block 末尾样本，用于检测 block 边界处的冲激
    std::vector<float> lastBlockTailSamplePre;   // 每通道一个（处理前）
    std::vector<float> lastBlockTailSamplePost;  // 每通道一个（处理后）
    int64_t            blockCounter = 0;

    // ---- 内部辅助 ----
    void rebuildStft (double sampleRate, int numChannels);
    void processStftFrame (int ch, const std::vector<float>& targetBinGains, float gainSmoothAlpha);
    void pushMathLogEvent (const MathLogEvent& e);
    void computeBlockStats (const float* data, int numSamples, int channel,
                            bool isPost, int64_t startSample,
                            float prevTailSample, BlockStats& outStats) const;
    void pushBlockStats (const BlockStats& s);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumTagAudioProcessor)
};