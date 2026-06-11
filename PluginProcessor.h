#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

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
}

class SpectrumTagAudioProcessor : public juce::AudioProcessor
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
    int                dryWetFadeSamplesRemaining = 0;
    int                dryWetFadeTotalSamples = 0;

    // ===== STFT 参数 =====
    int  stftSize = 4096;
    int  stftHop  = 1024;                         // stftSize / 4 (75% overlap)
    int  currentScaleMode = 0;                    // 0=linear, 1=mel（影响显示，不影响 STFT）
    std::vector<StftChannelState> stftStates;     // 每通道一个

    // ---- 内部辅助 ----
    void rebuildStft (double sampleRate, int numChannels);
    void processStftFrame (int ch, const std::vector<float>& targetBinGains, float gainSmoothAlpha);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumTagAudioProcessor)
};