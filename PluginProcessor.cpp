#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ============================================================================
//  SpectrumTagAudioProcessor - STFT / OLA 实现（v3）
// ----------------------------------------------------------------------------
//  - 未按 Print：音频完全 bypass，不执行任何 FFT/IFFT，零延迟、零开销。
//  - 按下 Print：STFT（75% overlap，hop = N/4），每个 bin 独立查 mask、
//    平滑增益、保留原始相位、IFFT、OLA 输出。bin 级精度 + 相位保留
//    + 一阶增益平滑 → 彻底消除音量波动，同时保留图片高频细节。
//  - 显示路径：独立实数 FFT，仅写 latestMagnitudes 供 UI 瀑布图读取。
// ============================================================================

namespace
{
    inline int fftChoiceToSize (int choice)
    {
        switch (choice)
        {
            case 0: return 1024;
            case 1: return 2048;
            case 2: return 4096;
            case 3: return 8192;
            default: return 4096;
        }
    }

    // mel <-> hz 转换（HTK mel，仅显示路径使用）
    inline float hzToMel (float hz)  { return 2595.0f * std::log10 (1.0f + hz / 700.0f); }
    inline float melToHz (float mel) { return 700.0f * (std::pow (10.0f, mel / 2595.0f) - 1.0f); }

    // amplitudeRatio (0..1.5) -> 线性 gain，0.0 映射到 -80 dB（≈0.0001）
    inline float ratioToGain (float ratio)
    {
        if (ratio <= 0.0f) return 0.0f;       // -80 dB（比之前 -60dB 更深，增强对比度）
        return ratio;                              // 1.0 = 0dB, 1.5 = +3.5dB
    }
}

// ----------------------------------------------------------------------------
SpectrumTagAudioProcessor::SpectrumTagAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
 #if ! JucePlugin_IsSynth
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
 #endif
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

SpectrumTagAudioProcessor::~SpectrumTagAudioProcessor() = default;

bool SpectrumTagAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled())
        return false;

 #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
 #endif

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
#endif
}

// ============================================================================
//  STFT / OLA：构建 / 帧处理
// ============================================================================
void SpectrumTagAudioProcessor::rebuildStft (double sampleRate, int numChannels)
{
    const int N       = stftSize;
    const int hop     = N / 4;
    const int numBins = N / 2 + 1;

    stftHop  = hop;
    stftStates.clear();
    stftStates.resize ((size_t) numChannels);

    // Hann 窗：w[n] = 0.5 * (1 - cos(2πn/(N-1)))
    std::vector<float> hann ((size_t) N);
    for (int n = 0; n < N; ++n)
        hann[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) n / (float) (N - 1)));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& st = stftStates[(size_t) ch];
        const int order = (int) std::round (std::log2 ((double) N));
        st.fft           = std::make_unique<juce::dsp::FFT> (order);
        st.window        = hann;
        st.fftWork.assign ((size_t) (2 * N), 0.0f);
        st.inputRing.assign ((size_t) N, 0.0f);
        st.dryDelayRing.assign ((size_t) N, 0.0f);
        st.outputRing.assign ((size_t) N, 0.0f);
        st.olaNormRing.assign ((size_t) N, 0.0f);
        st.smoothedGains.assign ((size_t) numBins, 1.0f);
        st.outFifo.assign ((size_t) (N * 2), 0.0f);
        st.inputPos         = 0;
        st.accumCount       = 0;
        st.frameCount       = 0;
        st.dryDelayWritePos = 0;
        st.outFifoWrite     = 0;
        st.outFifoRead      = 0;
        st.outFifoCount     = 0;
    }
}

// 处理一个 STFT 帧：取 inputRing 中最新的 N 个采样 → FFT → 按 bin 修改幅度 → IFFT → OLA
void SpectrumTagAudioProcessor::processStftFrame (int ch,
                                                   const std::vector<float>& targetBinGains,
                                                   float gainSmoothAlpha)
{
    auto& st     = stftStates[(size_t) ch];
    const int N  = stftSize;
    const int hop = stftHop;
    const int numBins = N / 2 + 1;

    // ---- 1) 从 inputRing 取出最新 N 个采样，加窗 ----
    // JUCE real-only FFT 输入：fftWork[0..N-1] 为实数时域样本（连续存放）
    // inputPos 指向"下一个写入位置"，因此最新 N 采样从 inputPos 开始（环形）
    const int frameStart = st.inputPos;   // 最老采样在 inputPos（N 个采样前写入的）
    for (int n = 0; n < N; ++n)
    {
        const int idx = (frameStart + n) % N;
        st.fftWork[(size_t) n] = st.inputRing[(size_t) idx] * st.window[(size_t) n];
    }
    // 清理工作区后半段（非必需，但可避免脏数据干扰调试）
    std::fill (st.fftWork.begin() + N, st.fftWork.end(), 0.0f);

    // ---- 2) 实数 FFT ----
    st.fft->performRealOnlyForwardTransform (st.fftWork.data());

    // ---- 3) 逐 bin 应用增益（保留原始相位）----
    // JUCE real-only 频域打包：
    //   bin0(DC)      -> fftWork[0] (实部), 虚部固定为 0
    //   binN/2(Nyq)   -> fftWork[1] (实部), 虚部固定为 0
    //   bin1..N/2-1   -> fftWork[2k], fftWork[2k+1]

    // k = 0 (DC)
    {
        const float target = targetBinGains[0];
        float& smooth = st.smoothedGains[0];
        smooth += (target - smooth) * gainSmoothAlpha;
        st.fftWork[0] *= smooth;
    }

    // k = 1 .. N/2-1
    for (int k = 1; k < numBins - 1; ++k)
    {
        const float target = targetBinGains[(size_t) k];
        float&      smooth = st.smoothedGains[(size_t) k];
        smooth += (target - smooth) * gainSmoothAlpha;

        st.fftWork[(size_t) (2 * k)]     *= smooth;
        st.fftWork[(size_t) (2 * k + 1)] *= smooth;
    }

    // k = N/2 (Nyquist)
    {
        const int kNyq = numBins - 1;
        const float target = targetBinGains[(size_t) kNyq];
        float& smooth = st.smoothedGains[(size_t) kNyq];
        smooth += (target - smooth) * gainSmoothAlpha;
        st.fftWork[1] *= smooth;
    }

    // ---- 4) 实数 IFFT（JUCE 内部会除以 N）----
    st.fft->performRealOnlyInverseTransform (st.fftWork.data());

    // ---- 5) 加合成窗，OLA 叠加到 outputRing ----
    // IFFT 后时域样本位于 fftWork[0..N-1]
    // 叠加起始位置 = frameStart（对应输入帧起始位置）
    // 同时累积 w^2 作为逐样本归一化权重（WOLA normalization）
    for (int n = 0; n < N; ++n)
    {
        const int outIdx = (frameStart + n) % N;
        const float w = st.window[(size_t) n];
        st.outputRing[(size_t) outIdx] += st.fftWork[(size_t) n] * w;
        st.olaNormRing[(size_t) outIdx] += w * w;
    }

    // ---- 6) 输出就绪 → 推送 hop 个全累积采样到 FIFO ----
    // 前 N/hop 帧是预热期；之后每帧放出 hop 个全累积采样。
    // 关键：从 outputRing 复制后立即清零该位置，这样下一轮 OLA 从此位置开始时不会叠加旧值。
    ++st.frameCount;
    if (st.frameCount >= N / hop)
    {
        // 循环找到"最老"的 hop 个全累积采样位置
        // 当前帧 frameStart 是最新的写入起点，往前 3 帧（3*hop = N - hop）的起点最老。
        // 即：fifoStart = (frameStart + N - 3*hop) % N = (frameStart + hop) % N
        const int fifoStart = (frameStart + hop) % N;
        constexpr float kNormEps = 1.0e-8f;
        for (int i = 0; i < hop; ++i)
        {
            const int pos = (fifoStart + i) % N;
            const float norm = st.olaNormRing[(size_t) pos];
            const float y = (norm > kNormEps)
                ? (st.outputRing[(size_t) pos] / norm)
                : st.outputRing[(size_t) pos];

            st.outFifo[(size_t) st.outFifoWrite] = y;
            st.outputRing[(size_t) pos] = 0.0f;
            st.olaNormRing[(size_t) pos] = 0.0f;
            st.outFifoWrite = (st.outFifoWrite + 1) % (N * 2);
            ++st.outFifoCount;
        }
    }
}

// ============================================================================
//  prepareToPlay / releaseResources
// ============================================================================
void SpectrumTagAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    const int numCh = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    const int scale = (int) *apvts.getRawParameterValue (ParameterIDs::fftScale);
    currentScaleMode = scale;

    // ---- STFT（音频处理路径）----
    const int stftSz = fftChoiceToSize (
        (int) *apvts.getRawParameterValue (ParameterIDs::fftSize));
    stftSize = stftSz;
    stftHop  = stftSz / 4;
    rebuildStft (sampleRate, numCh);

    // ---- 显示用 FFT（独立路径）----
    const int dispSize = stftSz;   // 显示与 STFT 共用同一尺寸
    displayFftSize = dispSize;
    displayHopSize = dispSize / 2;
    const int order = (int) std::round (std::log2 ((double) dispSize));
    displayFft = std::make_unique<juce::dsp::FFT> (order);

    displayWindow.assign ((size_t) dispSize, 0.0f);
    for (int n = 0; n < dispSize; ++n)
        displayWindow[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                              * (float) n / (float) (dispSize - 1)));

    displayInputRing.assign ((size_t) dispSize, 0.0f);
    displayRingWritePos = 0;
    displaySamplesAccum = 0;
    displayFftWork.assign ((size_t) (2 * dispSize), 0.0f);

    {
        const juce::SpinLock::ScopedLockType sl (spectrumLock);
        latestMagnitudes.assign ((size_t) (dispSize / 2 + 1), 0.0f);
    }

    // STFT/OLA 延迟 = stftSize - stftHop = 3*stftSize/4
    setLatencySamples (stftSize - stftHop);

    prevPrintActive = false;
    dryWetMix = 0.0f;
    dryWetTarget = 0.0f;
    dryWetFadeSamplesRemaining = 0;
    dryWetFadeTotalSamples = juce::jmax (1, (int) std::round (0.008 * sampleRate)); // 8ms
}

void SpectrumTagAudioProcessor::releaseResources() {}

// ============================================================================
//  processBlock：bypass（非 Print）/ STFT+OLA（Print 中）
// ============================================================================
void SpectrumTagAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // 输出比输入多的通道清零
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numCh    = juce::jmin (totalNumInputChannels, (int) stftStates.size());
    const int numSamps = buffer.getNumSamples();

    const bool printActive = printRunning.load();
    const double sr        = getSampleRate();

    // ---- 1) 检查参数变化 ----
    const int desiredScale = (int) *apvts.getRawParameterValue (ParameterIDs::fftScale);
    if (desiredScale != currentScaleMode)
        currentScaleMode = desiredScale;

    const int desiredSize = fftChoiceToSize (
        (int) *apvts.getRawParameterValue (ParameterIDs::fftSize));
    if (desiredSize != stftSize && ! printActive)
    {
        // 非 Print 期间才重建 STFT，避免 mid-print 爆音
        stftSize = desiredSize;
        stftHop  = desiredSize / 4;
        rebuildStft (sr, numCh);

        // 同步显示 FFT
        displayFftSize = desiredSize;
        displayHopSize = desiredSize / 2;
        const int order = (int) std::round (std::log2 ((double) desiredSize));
        displayFft = std::make_unique<juce::dsp::FFT> (order);
        displayWindow.assign ((size_t) desiredSize, 0.0f);
        for (int n = 0; n < desiredSize; ++n)
            displayWindow[(size_t) n] = 0.5f * (1.0f - std::cos (
                juce::MathConstants<float>::twoPi * (float) n / (float) (desiredSize - 1)));
        displayInputRing.assign ((size_t) desiredSize, 0.0f);
        displayRingWritePos = 0;
        displaySamplesAccum = 0;
        displayFftWork.assign ((size_t) (2 * desiredSize), 0.0f);
        {
            const juce::SpinLock::ScopedLockType sl (spectrumLock);
            latestMagnitudes.assign ((size_t) (desiredSize / 2 + 1), 0.0f);
        }

        setLatencySamples (desiredSize - desiredSize / 4);

        dryWetFadeTotalSamples = juce::jmax (1, (int) std::round (0.008 * sr));
    }

    if (stftStates.empty()) return;

    // ========================================================================
    //  Print 处理：STFT + OLA
    //
    //  关键设计：
    //   1) sample-major 循环（先 sample 后 channel），游标每帧仅推进一次
    //   2) 非 Print 时也写入 inputRing 预填充，避免 Print 首帧处理空数据
    //   3) 预热期（frameCount < N/hop）输出原始音频而非静音
    // ========================================================================
    const int N      = stftSize;
    const int hop    = stftHop;
    const int numBins = N / 2 + 1;

    // 增益平滑参数（Print 和非 Print 都有效，但非 Print 时 gain 恒为 1.0）
    const float frameSeconds = (float) hop / juce::jmax (1.0, sr);
    const float smoothAlpha  = 1.0f - std::exp (- frameSeconds / 0.010f);

    const float ratio     = *apvts.getRawParameterValue (ParameterIDs::amplitudeRatio);
    const bool  invert    = *apvts.getRawParameterValue (ParameterIDs::invert) > 0.5f;
    const float ratioGain = ratioToGain (ratio);

    std::vector<float> targetBinGains ((size_t) numBins, 1.0f);
    std::vector<float> delayedDrySamples ((size_t) juce::jmax (1, numCh), 0.0f);
    std::vector<float> wetSamples ((size_t) juce::jmax (1, numCh), 0.0f);

    // dry 与 wet 保持同一延迟基准（与插件 latency 一致），避免切换时相位/时序错位
    const int dryDelaySamples = juce::jlimit (0, N - 1, N - hop);

    // ---- Sample-major 循环：每个采样点处理所有通道 ----
    for (int n = 0; n < numSamps; ++n)
    {
        const bool requestedPrint = printRunning.load();
        if (requestedPrint != prevPrintActive)
        {
            prevPrintActive = requestedPrint;
            dryWetTarget = requestedPrint ? 1.0f : 0.0f;
            dryWetFadeSamplesRemaining = juce::jmax (1, dryWetFadeTotalSamples);
        }

        const bool stftPathActive = requestedPrint
            || (dryWetMix > 1.0e-4f)
            || (dryWetFadeSamplesRemaining > 0);

        // ---- Step A) 写入 inputRing + dryDelayRing（所有通道同时写入）----
        bool frameReady = false;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& st = stftStates[(size_t) ch];
            const float inSample = buffer.getReadPointer (ch)[n];

            st.inputRing[(size_t) st.inputPos] = inSample;
            st.inputPos = (st.inputPos + 1) % N;

            const int dryWritePos = st.dryDelayWritePos;
            st.dryDelayRing[(size_t) dryWritePos] = inSample;
            const int dryReadPos = (dryWritePos + N - dryDelaySamples) % N;
            delayedDrySamples[(size_t) ch] = st.dryDelayRing[(size_t) dryReadPos];
            st.dryDelayWritePos = (dryWritePos + 1) % N;

            ++st.accumCount;
            if (st.accumCount >= hop)
                frameReady = true;
        }

        // ---- Step B) 如果累积足够且 STFT 路径活跃 → 处理一帧 ----
        if (frameReady && stftPathActive)
        {
            // 首次进入 Print：重置 OLA 累积状态（inputRing 已预填充，无需重置）
            if (requestedPrint && wasPrintActive == 0)
            {
                for (int ch = 0; ch < numCh; ++ch)
                {
                    auto& st = stftStates[(size_t) ch];
                    std::fill (st.outputRing.begin(),     st.outputRing.end(),     0.0f);
                    std::fill (st.olaNormRing.begin(),    st.olaNormRing.end(),    0.0f);
                    std::fill (st.outFifo.begin(),         st.outFifo.end(),         0.0f);
                    std::fill (st.smoothedGains.begin(),   st.smoothedGains.end(),   1.0f);
                    st.frameCount   = 0;
                    st.outFifoWrite = 0;
                    st.outFifoRead  = 0;
                    st.outFifoCount = 0;
                }
                wasPrintActive = 1;
            }

            // 计算 mask（只计算一次，所有通道共享）
            if (requestedPrint)
            {
                const juce::SpinLock::ScopedLockType pl (printLock);
                if (printMaskCols > 0 && printMaskRows > 0 && printColPerSample > 0.0)
                {
                    const float maxHz = (float) sr * 0.5f;
                    const float fLow  = printFreqLowNorm  * maxHz;
                    const float fHigh = printFreqHighNorm * maxHz;
                    const float denom = juce::jmax (1.0e-6f, fHigh - fLow);

                    // 启动预热期：仅让 OLA 管线填满，不消费图片列，避免“从中间开始绘制”。
                    if (printWarmupFramesRemaining > 0)
                    {
                        --printWarmupFramesRemaining;
                        std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
                    }
                    else
                    {
                        const int maskCol = juce::jlimit (0, printMaskCols - 1,
                                                          (int) std::floor (printColCursorD));

                        for (int k = 0; k < numBins; ++k)
                        {
                            const float hz = (float) k / (float) (N / 2) * maxHz;
                            if (hz < fLow || hz > fHigh)
                            {
                                targetBinGains[(size_t) k] = 1.0f;
                                continue;
                            }
                            const float norm = (hz - fLow) / denom;
                            const int   row  = juce::jlimit (0, printMaskRows - 1,
                                                             (int) std::floor (norm * (printMaskRows - 1)));
                            const float m = printMask[(size_t) (row * printMaskCols + maskCol)];
                            const float g = invert
                                ? juce::jmap (m, ratioGain, 1.0f)
                                : juce::jmap (m, 1.0f,      ratioGain);
                            targetBinGains[(size_t) k] = g;
                        }

                        // 游标只推进一次（所有通道共享同一个时间点）
                        printColCursorD += printColPerSample * (double) hop;
                        if (printColCursorD >= (double) printMaskCols)
                        {
                            printRunning.store (false);
                            std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
                        }
                    }
                }
                else
                {
                    std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
                }
            }
            else
            {
                // 退出 Print 后的尾段：继续以 unity 增益驱动 STFT，保证 wet 尾部连续
                std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
            }

            // 所有通道处理同一个 STFT 帧
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& st = stftStates[(size_t) ch];
                st.accumCount -= hop;
                processStftFrame (ch, targetBinGains, smoothAlpha);
            }
        }

        if (! stftPathActive)
        {
            // 非 Print 且非渐变阶段：重置启动检测，并限制 accum 防止“补账爆发”
            wasPrintActive = 0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& st = stftStates[(size_t) ch];
                if (st.accumCount >= hop)
                    st.accumCount %= hop;
                else if (st.accumCount < 0)
                    st.accumCount = 0;
            }
        }

        // ---- Step C) dry/wet 交叉渐入渐出（同一延迟基准，防止时序错位） ----
        const float mixWet = juce::jlimit (0.0f, 1.0f, dryWetMix);
        const float mixDry = 1.0f - mixWet;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& st = stftStates[(size_t) ch];
            float wet = delayedDrySamples[(size_t) ch]; // FIFO 为空时回退到延迟 dry，避免硬切
            if (st.outFifoCount > 0)
            {
                wet = st.outFifo[(size_t) st.outFifoRead];
                st.outFifoRead = (st.outFifoRead + 1) % (N * 2);
                --st.outFifoCount;
            }
            wetSamples[(size_t) ch] = wet;

            buffer.getWritePointer (ch)[n] = delayedDrySamples[(size_t) ch] * mixDry
                                           + wetSamples[(size_t) ch] * mixWet;
        }

        if (dryWetFadeSamplesRemaining > 0)
        {
            dryWetMix += (dryWetTarget - dryWetMix) / (float) dryWetFadeSamplesRemaining;
            --dryWetFadeSamplesRemaining;
            if (dryWetFadeSamplesRemaining <= 0)
                dryWetMix = dryWetTarget;
        }
        else
        {
            dryWetMix = dryWetTarget;
        }
    }

    // ---- 显示用 FFT（读处理后的 buffer）----
    if (displayFft != nullptr && displayFftSize > 0)
    {
        const int dN = displayFftSize;
        const int dHop = displayHopSize;

        // 显示归一化：使用窗口能量（sum(w^2)）而不是简单 /N，
        // 使白噪声在不同 FFT size 下的亮度更稳定。
        float winSqSum = 0.0f;
        for (float w : displayWindow) winSqSum += w * w;
        const float displayMagNorm = (winSqSum > 1e-12f)
            ? 1.0f / std::sqrt (winSqSum)
            : 1.0f / (float) juce::jmax (1, dN);

        for (int i = 0; i < numSamps; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                mono += buffer.getReadPointer (ch)[i];
            mono /= (float) juce::jmax (1, numCh);

            displayInputRing[(size_t) displayRingWritePos] = mono;
            displayRingWritePos = (displayRingWritePos + 1) % dN;
            ++displaySamplesAccum;

            if (displaySamplesAccum >= dHop)
            {
                displaySamplesAccum -= dHop;   // 保留溢出样本，避免 FFT 窗口漂移
                // JUCE real-only FFT 输入：displayFftWork[0..dN-1] 存放实数时域样本
                for (int n = 0; n < dN; ++n)
                {
                    const int idx = (displayRingWritePos + n) % dN;
                    displayFftWork[(size_t) n] = displayInputRing[(size_t) idx]
                                               * displayWindow[(size_t) n];
                }
                std::fill (displayFftWork.begin() + dN, displayFftWork.end(), 0.0f);

                displayFft->performRealOnlyForwardTransform (displayFftWork.data());
                const int half = dN / 2 + 1;
                std::vector<float> mags ((size_t) half);

                // k=0 (DC)
                mags[0] = std::abs (displayFftWork[0]) * displayMagNorm;

                // k=1..N/2-1
                for (int k = 1; k < half - 1; ++k)
                {
                    const float re = displayFftWork[(size_t) (2 * k)];
                    const float im = displayFftWork[(size_t) (2 * k + 1)];
                    mags[(size_t) k] = std::sqrt (re * re + im * im) * displayMagNorm;
                }

                // k=N/2 (Nyquist)
                mags[(size_t) (half - 1)] = std::abs (displayFftWork[1]) * displayMagNorm;
                {
                    const juce::SpinLock::ScopedLockType sl (spectrumLock);
                    latestMagnitudes = std::move (mags);
                    displayUpdateCounter.fetch_add (1, std::memory_order_release);
                }
            }
        }
    }
}

// ============================================================================
//  Boilerplate
// ============================================================================
juce::AudioProcessorEditor* SpectrumTagAudioProcessor::createEditor()
{
    return new SpectrumTagAudioProcessorEditor (*this);
}
bool SpectrumTagAudioProcessor::hasEditor() const          { return true; }

const juce::String SpectrumTagAudioProcessor::getName() const { return "SpectrumTag"; }
bool   SpectrumTagAudioProcessor::acceptsMidi() const         { return false; }
bool   SpectrumTagAudioProcessor::producesMidi() const        { return false; }
bool   SpectrumTagAudioProcessor::isMidiEffect() const        { return false; }
double SpectrumTagAudioProcessor::getTailLengthSeconds() const{ return 0.0; }

int  SpectrumTagAudioProcessor::getNumPrograms()                                  { return 1; }
int  SpectrumTagAudioProcessor::getCurrentProgram()                               { return 0; }
void SpectrumTagAudioProcessor::setCurrentProgram (int)                           {}
const juce::String SpectrumTagAudioProcessor::getProgramName (int)                { return {}; }
void SpectrumTagAudioProcessor::changeProgramName (int, const juce::String&)      {}

// ===== Editor State 镜像 =====
void SpectrumTagAudioProcessor::setEditorState (const EditorState& s)
{
    const juce::SpinLock::ScopedLockType sl (editorStateLock);
    editorState = s;
    editorState.hasValidValues = true;
}

SpectrumTagAudioProcessor::EditorState SpectrumTagAudioProcessor::getEditorState() const
{
    const juce::SpinLock::ScopedLockType sl (editorStateLock);
    return editorState;
}

// ===== 频谱可视化数据 =====
void SpectrumTagAudioProcessor::getLatestMagnitudeSpectrum (std::vector<float>& dest)
{
    const juce::SpinLock::ScopedLockType sl (spectrumLock);
    dest = latestMagnitudes;
}

// ===== Print 流程 =====
void SpectrumTagAudioProcessor::startPrint (std::vector<float>&& maskData,
                                             int maskRows, int maskCols,
                                             float freqLowNorm, float freqHighNorm,
                                             double durationSeconds)
{
    const juce::SpinLock::ScopedLockType sl (printLock);
    printMask         = std::move (maskData);
    printMaskRows     = maskRows;
    printMaskCols     = maskCols;
    printFreqLowNorm  = juce::jlimit (0.0f, 1.0f, freqLowNorm);
    printFreqHighNorm = juce::jlimit (0.0f, 1.0f, freqHighNorm);
    printDurationSec  = juce::jmax (0.05, durationSeconds);
    printColCursorD   = 0.0;
    printWarmupFramesRemaining = juce::jmax (0, stftSize / juce::jmax (1, stftHop));

    const double sr = getSampleRate();
    const double safeSr = sr > 1000.0 ? sr : 44100.0;
    printColPerSample = (double) juce::jmax (1, maskCols) / (printDurationSec * safeSr);

    // 若 mask 无效，直接不进入 Print，避免“按钮变灰但无处理”的假启动。
    if (printMask.empty() || printMaskRows <= 0 || printMaskCols <= 0)
    {
        printRunning.store (false);
        return;
    }

   #if JUCE_DEBUG
    int maxAccumCount = 0;
    for (auto& st : stftStates)
        maxAccumCount = juce::jmax (maxAccumCount, st.accumCount);
    DBG ("[SpectrumTag] startPrint: maxAccumCount=" + juce::String (maxAccumCount)
         + ", hop=" + juce::String (juce::jmax (1, stftHop))
         + ", warmupFrames=" + juce::String (printWarmupFramesRemaining)
         + ", maskCols=" + juce::String (printMaskCols)
         + ", durationSec=" + juce::String (printDurationSec, 3));
   #endif

    printRunning.store (true);
}

// ===== 状态序列化 =====
void SpectrumTagAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree tree ("SpectrumTagState");
    tree.setProperty ("version", 1, nullptr);

    tree.appendChild (apvts.copyState(), nullptr);

    EditorState s = getEditorState();
    tree.setProperty ("imagePath",     s.imagePath,     nullptr);
    tree.setProperty ("imgRectXNorm",  s.imgRectXNorm,  nullptr);
    tree.setProperty ("imgRectYNorm",  s.imgRectYNorm,  nullptr);
    tree.setProperty ("imgRectWNorm",  s.imgRectWNorm,  nullptr);
    tree.setProperty ("imgRectHNorm",  s.imgRectHNorm,  nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectrumTagAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName ("SpectrumTagState"))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid())
            return;

        if (tree.getNumChildren() > 0)
        {
            auto paramsTree = tree.getChild (0);
            if (paramsTree.hasType ("PARAMETERS"))
                apvts.replaceState (paramsTree);
        }

        EditorState s;
        s.imagePath     = tree.getProperty ("imagePath",     s.imagePath).toString();
        s.imgRectXNorm  = (float) tree.getProperty ("imgRectXNorm",  s.imgRectXNorm);
        s.imgRectYNorm  = (float) tree.getProperty ("imgRectYNorm",  s.imgRectYNorm);
        s.imgRectWNorm  = (float) tree.getProperty ("imgRectWNorm",  s.imgRectWNorm);
        s.imgRectHNorm  = (float) tree.getProperty ("imgRectHNorm",  s.imgRectHNorm);
        s.hasValidValues = true;

        const juce::SpinLock::ScopedLockType sl (editorStateLock);
        editorState = s;
    }
}

// ===== 参数布局 =====
juce::AudioProcessorValueTreeState::ParameterLayout SpectrumTagAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::fftSize, "FFT Size",
        juce::StringArray { "1024", "2048", "4096", "8192" },
        2));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::fftScale, "FFT Scale",
        juce::StringArray { "linear", "mel" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::speed, "Speed",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.0f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + "x"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::amplitudeRatio, "Amplitude Ratio",
        juce::NormalisableRange<float>(0.0f, 1.5f, 0.0f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (juce::roundToInt (v * 100.0f)) + "%";
            })));

    layout.add (std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::invert, "Invert", false));

    return layout;
}

// ===== JUCE 入口 =====
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrumTagAudioProcessor();
}