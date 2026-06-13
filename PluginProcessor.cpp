#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <fstream>
#include <iomanip>

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

    inline juce::String toSafeFilenameTimestamp (const juce::String& iso)
    {
        juce::String s = iso;
        s = s.replaceCharacter (':', '-');
        s = s.replaceCharacter ('T', '_');
        s = s.replaceCharacter ('.', '-');
        s = s.removeCharacters ("+");
        return s;
    }
}

void SpectrumTagAudioProcessor::markPrintClickAndBeginMathLog()
{
    std::lock_guard<std::mutex> lk (mathLogMutex);
    mathLog.sampleRateRounded = (int64_t) juce::jmax (1, juce::roundToInt (getSampleRate() > 1000.0 ? getSampleRate() : 44100.0));
    mathLog.clickTimeIso = juce::Time::getCurrentTime().toISO8601 (true);
    mathLog.printClickSample = mathLog.globalSampleCounter;
    mathLog.collectingPost = true;
    mathLog.postSamplesRemaining = mathLog.sampleRateRounded;
    mathLog.frozenPreEvents.clear();
    mathLog.frozenPreEvents.assign (mathLog.preEvents.begin(), mathLog.preEvents.end());
    mathLog.postEvents.clear();
    // block 级日志：同样冻结点击前的滚动窗口，并清空 post 容器
    mathLog.frozenPreBlockStats.clear();
    mathLog.frozenPreBlockStats.assign (mathLog.preBlockStatsRolling.begin(),
                                        mathLog.preBlockStatsRolling.end());
    mathLog.postBlockStats.clear();
    mathLog.requestFlush = false;
    mathLog.active = true;
}

void SpectrumTagAudioProcessor::pushMathLogEvent (const MathLogEvent& e)
{
    std::lock_guard<std::mutex> lk (mathLogMutex);

    const int64_t sr = juce::jmax ((int64_t) 8000, mathLog.sampleRateRounded);
    const int channels = juce::jmax (1, getTotalNumInputChannels());
    const size_t maxPreEvents = (size_t) juce::jmax ((int64_t) 4096, sr * (int64_t) channels); // 前1秒滚动窗口
    mathLog.preEvents.push_back (e);
    while (mathLog.preEvents.size() > maxPreEvents)
        mathLog.preEvents.pop_front();

    if (mathLog.collectingPost)
    {
        mathLog.postEvents.push_back (e);
        if (e.channel == 0)
        {
            --mathLog.postSamplesRemaining;
            if (mathLog.postSamplesRemaining <= 0)
            {
                mathLog.collectingPost = false;
                mathLog.requestFlush = true;
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  Block 级数学统计
// ----------------------------------------------------------------------------
//  对一段连续采样做"定性 + 定量"的快速分析。
//  采集的指标为定位以下三个 bug 提供决定性证据：
//   (1) 点击 Print 后音量瞬时跳变  → rms/peak/energy 的 block 级阶跃
//   (2) Print 结束回到干声的跳变   → 同上，且 boundaryDiff 会高亮
//   (3) 处理过程中竖直细线（电流声/冲激）
//       → maxAbsDiff 与 slewSpikeCount 是冲激/阶跃噪声的最直接指标
//         （单样本 100us 级别的突跳，在频谱图上即为竖直细线）
// ----------------------------------------------------------------------------
void SpectrumTagAudioProcessor::computeBlockStats (const float* data,
                                                    int numSamples,
                                                    int channel,
                                                    bool isPost,
                                                    int64_t startSample,
                                                    float prevTailSample,
                                                    BlockStats& s) const
{
    s.numSamples = numSamples;
    s.channel = channel;
    s.isPost = isPost;
    s.startSample = startSample;
    s.prevBlockLastSample = prevTailSample;

    if (numSamples <= 0 || data == nullptr)
    {
        s.minVal = 0.0f;
        s.maxVal = 0.0f;
        s.peakAbs = 0.0f;
        s.peakAbsPos = -1;
        s.mean = 0.0;
        s.rms = 0.0;
        s.energy = 0.0;
        s.maxAbsDiff = 0.0f;
        s.maxAbsDiffPos = -1;
        s.slewSpikeCount = 0;
        s.zeroCrossings = 0;
        s.clipCount = 0;
        s.nonFiniteCount = 0;
        s.boundaryDiff = 0.0f;
        return;
    }

    // 阈值：单样本相邻差超过 0.25 视为可疑冲激（人耳对此类阶跃极敏感，
    // 频谱图上呈现为竖直细线/click）；可按需调整。
    constexpr float kSlewThresh = 0.25f;
    constexpr float kClipThresh = 1.0f;

    float minV = data[0];
    float maxV = data[0];
    float peakAbs = std::abs (data[0]);
    int   peakPos = 0;
    double sum = 0.0;
    double sumSq = 0.0;
    float maxDiff = 0.0f;
    int   maxDiffPos = -1;
    int   slewSpikes = 0;
    int   zeroX = 0;
    int   clips = 0;
    int   nonFinite = 0;

    // 跨 block 边界：把上一个 block 的最后一个样本作为 x[-1]，纳入相邻差计算
    {
        const float v = data[0];
        if (! std::isfinite (v) || ! std::isfinite (prevTailSample))
        {
            // 不计入差值，单独累计 nonFinite
        }
        else
        {
            const float d = std::abs (v - prevTailSample);
            if (d > maxDiff) { maxDiff = d; maxDiffPos = 0; }
            if (d > kSlewThresh) ++slewSpikes;
        }
    }
    s.boundaryDiff = std::isfinite (data[0]) && std::isfinite (prevTailSample)
                       ? std::abs (data[0] - prevTailSample)
                       : 0.0f;

    float prev = data[0];
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = data[i];
        if (! std::isfinite (v))
        {
            ++nonFinite;
            continue;
        }

        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
        const float a = std::abs (v);
        if (a > peakAbs) { peakAbs = a; peakPos = i; }
        sum   += (double) v;
        sumSq += (double) v * (double) v;
        if (a >= kClipThresh) ++clips;

        if (i > 0)
        {
            const float d = std::abs (v - prev);
            if (d > maxDiff) { maxDiff = d; maxDiffPos = i; }
            if (d > kSlewThresh) ++slewSpikes;
            // 过零点（不含 0->0 持平）
            if ((prev <= 0.0f && v > 0.0f) || (prev >= 0.0f && v < 0.0f))
                ++zeroX;
        }
        prev = v;
    }

    s.minVal = minV;
    s.maxVal = maxV;
    s.peakAbs = peakAbs;
    s.peakAbsPos = peakPos;
    s.mean = sum / (double) numSamples;
    s.energy = sumSq;
    s.rms = std::sqrt (sumSq / (double) numSamples);
    s.maxAbsDiff = maxDiff;
    s.maxAbsDiffPos = maxDiffPos;
    s.slewSpikeCount = slewSpikes;
    s.zeroCrossings = zeroX;
    s.clipCount = clips;
    s.nonFiniteCount = nonFinite;
}

void SpectrumTagAudioProcessor::pushBlockStats (const BlockStats& s)
{
#if ! SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
    // 临时禁用：跳过 mutex 与 deque/vector 累加，避免实时线程任何额外开销。
    juce::ignoreUnused (s);
    return;
#else
    std::lock_guard<std::mutex> lk (mathLogMutex);

    const int64_t sr = juce::jmax ((int64_t) 8000, mathLog.sampleRateRounded);
    const int channels = juce::jmax (1, getTotalNumInputChannels());
    // 估算每秒 block 数量：使用一个保守的 64 samples/block 上限来限制滚动窗口大小
    // 也按 channel × pre/post(=2) 翻倍，以保证容纳"前1秒"完整数据
    const size_t maxRolling = (size_t) juce::jmax ((int64_t) 2048,
                                                    (sr / 32) * (int64_t) channels * 2);
    mathLog.preBlockStatsRolling.push_back (s);
    while (mathLog.preBlockStatsRolling.size() > maxRolling)
        mathLog.preBlockStatsRolling.pop_front();

    if (mathLog.collectingPost)
    {
        mathLog.postBlockStats.push_back (s);
    }
#endif
}

void SpectrumTagAudioProcessor::flushPrintMathLogToFile()
{
#if ! SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
    // 临时禁用：直接返回，跳过 CSV 文件生成开销。
    // 同时清掉可能积累的请求标志，避免开关切回时一次性 flush 残留旧数据。
    {
        std::lock_guard<std::mutex> lk (mathLogMutex);
        mathLog.requestFlush = false;
    }
    return;
#else
    MathLogSession snap;
    {
        std::lock_guard<std::mutex> lk (mathLogMutex);
        if (! mathLog.requestFlush)
            return;
        mathLog.requestFlush = false;
        snap = mathLog;
    }

    juce::File logDir ("D:\\SpectrumTag\\Log");
    if (! logDir.exists())
        logDir.createDirectory();

    const juce::String safeTs = toSafeFilenameTimestamp (snap.clickTimeIso);
    const juce::File logFile = logDir.getChildFile ("print_math_" + safeTs + ".csv");

    std::ofstream os (logFile.getFullPathName().toRawUTF8(), std::ios::out | std::ios::trunc);
    if (! os.is_open())
        return;

    os << "meta,click_time_iso," << snap.clickTimeIso.toRawUTF8() << "\n";
    os << "meta,print_click_sample," << snap.printClickSample << "\n";
    os << "meta,sample_rate," << snap.sampleRateRounded << "\n";
    os << "meta,stft_size," << stftSize << "\n";
    os << "meta,stft_hop," << stftHop << "\n";
    os << "meta,dry_wet_fade_total_samples," << dryWetFadeTotalSamples << "\n";
    os << "section,type,sample,delta_to_click,channel,stft_frame,print_requested,stft_path_active,frame_ready,print_state_flipped,mask_col,print_col_cursor,fade_delay_samples,fade_samples_remaining,print_warmup_remaining,accum_count,target_gain_min,target_gain_max,target_gain_mean,dry_in,delayed_dry,wet_out,mix,mix_target,out_sample,rebuild_out,mix_error,target_gain_probe,smooth_gain_probe,fft_mag_probe,ifft_probe,ola_norm_probe,out_fifo_count\n";

    auto writeEvent = [&] (const char* section, const MathLogEvent& e)
    {
        const float rebuiltOut = e.delayedDry * (1.0f - e.dryWetMix) + e.wetOut * e.dryWetMix;
        const float mixError = e.outSample - rebuiltOut;
        os << section << ","
           << "event" << ","
           << e.sampleIndex << ","
           << (e.sampleIndex - snap.printClickSample) << ","
           << e.channel << ","
           << e.stftFrame << ","
           << (e.printRequested ? 1 : 0) << ","
           << (e.stftPathActive ? 1 : 0) << ","
           << (e.frameReady ? 1 : 0) << ","
           << (e.printStateFlipped ? 1 : 0) << ","
           << e.maskCol << ","
           << e.printColCursor << ","
           << e.fadeDelaySamples << ","
           << e.fadeSamplesRemaining << ","
           << e.printWarmupRemaining << ","
           << e.accumCount << ","
           << e.targetGainMin << ","
           << e.targetGainMax << ","
           << e.targetGainMean << ","
           << std::setprecision (9) << e.dryIn << ","
           << e.delayedDry << ","
           << e.wetOut << ","
           << e.dryWetMix << ","
           << e.dryWetTarget << ","
           << e.outSample << ","
           << rebuiltOut << ","
           << mixError << ","
           << e.targetGainProbe << ","
           << e.smoothGainProbe << ","
           << e.fftMagProbe << ","
           << e.ifftProbe << ","
           << e.olaNormProbe << ","
           << e.outFifoCount << "\n";
    };

    for (const auto& e : snap.frozenPreEvents) writeEvent ("pre", e);
    for (const auto& e : snap.postEvents)      writeEvent ("post", e);

    // ------------------------------------------------------------------
    //  Block 级别统计（pre = 处理前输入；post = 处理后输出）
    //  与 sample 级日志写入同一 CSV，方便后续脚本统一加载分析。
    //  每个 block 在 pre 和 post 路径上各产生一条记录（每通道一条），
    //  通过 block_index + channel + is_post 可以将它们对齐起来对比。
    //  关键列：
    //    rms / peak_abs / energy / mean(=DC) → 音量与能量层级
    //    max_abs_diff / slew_spike_count    → 冲激/竖直细线（相邻样本突跳）
    //    boundary_diff                      → 跨 block 边界的衔接突跳
    //    zero_crossings                     → 频率特征
    //    clip_count / non_finite_count      → 削波 / 数值异常
    // ------------------------------------------------------------------
    os << "\n";
    os << "section,type,block_index,start_sample,delta_to_click,channel,is_post,num_samples,"
          "print_requested,stft_path_active,mix_snapshot,mix_target_snapshot,"
          "fade_samples_remaining,fade_delay_samples,accum_count_snapshot,out_fifo_count_snapshot,"
          "min,max,peak_abs,peak_abs_pos,mean_dc,rms,energy,"
          "max_abs_diff,max_abs_diff_pos,slew_spike_count,zero_crossings,"
          "clip_count,non_finite_count,boundary_diff,prev_block_last_sample\n";

    auto writeBlockStats = [&] (const char* section, const BlockStats& b)
    {
        os << section << ","
           << "block" << ","
           << b.blockIndex << ","
           << b.startSample << ","
           << (b.startSample - snap.printClickSample) << ","
           << b.channel << ","
           << (b.isPost ? 1 : 0) << ","
           << b.numSamples << ","
           << (b.printRequested ? 1 : 0) << ","
           << (b.stftPathActive ? 1 : 0) << ","
           << b.dryWetMixSnapshot << ","
           << b.dryWetTargetSnapshot << ","
           << b.fadeSamplesRemaining << ","
           << b.fadeDelaySamples << ","
           << b.accumCountSnapshot << ","
           << b.outFifoCountSnapshot << ","
           << std::setprecision (9) << b.minVal << ","
           << b.maxVal << ","
           << b.peakAbs << ","
           << b.peakAbsPos << ","
           << b.mean << ","
           << b.rms << ","
           << b.energy << ","
           << b.maxAbsDiff << ","
           << b.maxAbsDiffPos << ","
           << b.slewSpikeCount << ","
           << b.zeroCrossings << ","
           << b.clipCount << ","
           << b.nonFiniteCount << ","
           << b.boundaryDiff << ","
           << b.prevBlockLastSample << "\n";
    };

    for (const auto& b : snap.frozenPreBlockStats) writeBlockStats ("pre",  b);
    for (const auto& b : snap.postBlockStats)      writeBlockStats ("post", b);

    os.flush();
#endif // SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
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
    // 缓存 printTrigger 参数指针，后续复位/读值都走它。
    printTriggerParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParameterIDs::printTrigger));
    lastPrintTriggerState.store (printTriggerParam != nullptr && printTriggerParam->get());
    apvts.addParameterListener (ParameterIDs::printTrigger, this);
}

SpectrumTagAudioProcessor::~SpectrumTagAudioProcessor()
{
    apvts.removeParameterListener (ParameterIDs::printTrigger, this);
}

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

    const int probeBin = juce::jlimit (0, numBins - 1, numBins / 4);
    st.lastTargetGainProbe = targetBinGains[(size_t) probeBin];

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

    st.lastSmoothGainProbe = st.smoothedGains[(size_t) probeBin];
    if (probeBin == 0)
    {
        st.lastFftMagProbe = std::abs (st.fftWork[0]);
    }
    else if (probeBin == numBins - 1)
    {
        st.lastFftMagProbe = std::abs (st.fftWork[1]);
    }
    else
    {
        const float re = st.fftWork[(size_t) (2 * probeBin)];
        const float im = st.fftWork[(size_t) (2 * probeBin + 1)];
        st.lastFftMagProbe = std::sqrt (re * re + im * im);
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
    //
    //  推导（关键，影响电流声/竖直细线的产生）：
    //   - 当前帧 frame_i 写入区间 [frameStart, frameStart + N)
    //   - 下一帧 frame_{i+1} 写入区间 [frameStart + hop, frameStart + hop + N)
    //   - 因此 frame_i 写完后，下一帧不会再覆盖的"最老"区间是
    //         [frameStart, frameStart + hop)
    //     这一段在 frame_i 时刚好被 N/hop = 4 帧累加完成，∑w² 也已到稳态。
    //   - 旧实现取 fifoStart = (frameStart + hop) % N 是错误的：它推出的
    //     [frameStart + hop, frameStart + 2*hop) 还会被 frame_{i+1} 再写一次，
    //     此时 olaNormRing 只累计了 3 帧 w²（≈1.125 vs 稳态 1.5），归一化分母
    //     偏小 → 输出幅度偏高 ~33%；下一帧从已被清零的累加器再加新贡献，
    //     在帧切换处产生单样本阶跃 = 频谱图竖直细线 + 电流声。
    //
    //  前 N/hop 帧是预热期；从第 N/hop 帧起每帧放出 hop 个稳态归一化的样本。
    ++st.frameCount;
    if (st.frameCount >= N / hop)
    {
        const int fifoStart = frameStart;          // ← 修正：必须从 frameStart 开始
        constexpr float kNormEps = 1.0e-8f;
        for (int i = 0; i < hop; ++i)
        {
            const int pos = (fifoStart + i) % N;
            const float norm = st.olaNormRing[(size_t) pos];
            const float y = (norm > kNormEps)
                ? (st.outputRing[(size_t) pos] / norm)
                : st.outputRing[(size_t) pos];

            st.outFifo[(size_t) st.outFifoWrite] = y;
            st.outputRing[(size_t) pos]   = 0.0f;
            st.olaNormRing[(size_t) pos]  = 0.0f;
            st.outFifoWrite = (st.outFifoWrite + 1) % (N * 2);
            ++st.outFifoCount;
        }
    }

    st.lastIfftProbe = st.fftWork[(size_t) juce::jlimit (0, N - 1, N / 2)];
    const int normProbePos = frameStart;
    st.lastOlaNormProbe = st.olaNormRing[(size_t) normProbePos];
    st.lastProcessedFrame = st.frameCount;
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
    dryWetFadeStartMix = 0.0f;
    dryWetFadeSamplesRemaining = 0;
    dryWetFadeTotalSamples = juce::jmax (1, (int) std::round (0.2 * sampleRate)); // 200ms，覆盖 OLA 8 帧预热期
    printFadeDelaySamples = 0;

    // block 级日志：跨 block 衔接样本初始化（每通道一份）
    lastBlockTailSamplePre.assign  ((size_t) juce::jmax (1, numCh), 0.0f);
    lastBlockTailSamplePost.assign ((size_t) juce::jmax (1, numCh), 0.0f);
    blockCounter = 0;
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

    // ------------------------------------------------------------------
    //  Block 级日志：在【处理前】采集每通道的输入信号统计快照。
    //  统计指标专为定位"音量跳变 + 竖直细线（冲激式电流声）"设计：
    //    rms / peak_abs / mean(=DC) → 音量层级阶跃
    //    max_abs_diff / slew_spike_count → 单样本突跳（频谱图竖直细线）
    //    boundary_diff → 与上一 block 末尾样本的衔接突跳
    // ------------------------------------------------------------------
    int64_t blockStartGlobalSample = 0;
    {
        std::lock_guard<std::mutex> lk (mathLogMutex);
        blockStartGlobalSample = mathLog.globalSampleCounter;
    }
    const int64_t thisBlockIndex = blockCounter;

    if (numCh > 0 && numSamps > 0)
    {
#if SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float prevTail = (ch < (int) lastBlockTailSamplePre.size())
                                       ? lastBlockTailSamplePre[(size_t) ch]
                                       : 0.0f;
            BlockStats s;
            s.blockIndex = thisBlockIndex;
            s.printRequested = printActive;
            s.stftPathActive = printActive
                              || (dryWetMix > 1.0e-4f)
                              || (dryWetFadeSamplesRemaining > 0);
            s.dryWetMixSnapshot = dryWetMix;
            s.dryWetTargetSnapshot = dryWetTarget;
            s.fadeSamplesRemaining = dryWetFadeSamplesRemaining;
            s.fadeDelaySamples = printFadeDelaySamples;
            s.accumCountSnapshot = (ch < (int) stftStates.size())
                                       ? stftStates[(size_t) ch].accumCount : 0;
            s.outFifoCountSnapshot = (ch < (int) stftStates.size())
                                       ? stftStates[(size_t) ch].outFifoCount : 0;

            computeBlockStats (buffer.getReadPointer (ch), numSamps, ch,
                               /*isPost*/ false, blockStartGlobalSample,
                               prevTail, s);
            pushBlockStats (s);

            // 更新衔接尾样本（处理前路径）
            if (ch < (int) lastBlockTailSamplePre.size())
                lastBlockTailSamplePre[(size_t) ch] = buffer.getReadPointer (ch)[numSamps - 1];
        }
#else
        // 日志禁用：仅维护衔接尾样本（成本极低，保留以便开关切回时数据连续）
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (ch < (int) lastBlockTailSamplePre.size())
                lastBlockTailSamplePre[(size_t) ch] = buffer.getReadPointer (ch)[numSamps - 1];
        }
#endif
    }

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

        dryWetFadeTotalSamples = juce::jmax (1, (int) std::round (0.2 * sr));
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
        int usedMaskCol = -1;
        bool printStateFlipped = false;
        const bool requestedPrint = printRunning.load();
        if (requestedPrint != prevPrintActive)
        {
            prevPrintActive = requestedPrint;
            printStateFlipped = true;
            // 目标值立即更新，避免在退出 Print 时因为目标仍为 wet 导致额外能量塌陷。
            // 延迟期间先冻结 mix，等管线稳定后再启动 crossfade。
            dryWetTarget = requestedPrint ? 1.0f : 0.0f;
            printFadeDelaySamples = requestedPrint ? (2 * N) : N;
        }

        const bool stftPathActive = requestedPrint
            || (dryWetMix > 1.0e-4f)
            || (dryWetFadeSamplesRemaining > 0);

        float gainMin = 1.0f;
        float gainMax = 1.0f;
        float gainMean = 1.0f;

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
                    st.accumCount   = 0;
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

                    // ----------------------------------------------------------------
                    //  Fade-in 期间冻结 mask 列推进，避免"图像首字符画一半"。
                    //
                    //  根因（日志 print_math_2026-06-13_22-23-29 验证）：
                    //   - click 后 ~441 samples 才发生 print_state_flipped；
                    //   - 翻转瞬间设置 fade_delay=2N，再走 8 帧 OLA warmup；
                    //   - warmup 一结束 cursor 就开始推进，但此时 dryWetMix 仍在
                    //     从 0 渐变到 1 的 200ms ramp 中（fade_total=8820 samples）；
                    //   - 而 mask 增益已被 mix 系数缩小（g_eff = 1 + (g-1)*mix）→
                    //     前若干列以 mix∈(0,1) 的部分强度施加 → 视觉上首字符
                    //     "只画一半"或"模糊起步"。
                    //
                    //  修复：cursor 推进改由 "fade 已基本完成" 触发；fade 期间
                    //  退化为 unity gain，等同于让 OLA 多空转一会儿。这样图像
                    //  第 0 列必然在 mix≈1 时刻才被消费，强度完整。
                    //  退出时（dryWetTarget=0 → fade-out）则由 print_requested=false
                    //  分支兜底，cursor 已停止推进，不会"末尾被淡化"。
                    // ----------------------------------------------------------------
                    constexpr float kFadeDoneThreshold = 0.999f;
                    const bool fadeInComplete = (dryWetMix >= kFadeDoneThreshold)
                                             || (dryWetTarget < 0.5f); // 退出 print 也算"无需冻结"
                    const bool warmingUp = (printWarmupFramesRemaining > 0);

                    // 启动预热期：仅让 OLA 管线填满，不消费图片列，避免“从中间开始绘制”。
                    if (warmingUp)
                    {
                        --printWarmupFramesRemaining;
                        std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
                    }
                    else if (! fadeInComplete)
                    {
                        // Fade-in 仍在进行：保持 unity gain，cursor 暂不推进，
                        // 等待 mix 到达稳态后再正式开始绘制第 0 列。
                        std::fill (targetBinGains.begin(), targetBinGains.end(), 1.0f);
                    }
                    else
                    {
                        const int maskCol = juce::jlimit (0, printMaskCols - 1,
                                                          (int) std::floor (printColCursorD));
                        usedMaskCol = maskCol;

                        int activeRowMin = printMaskRows;
                        int activeRowMax = -1;
                        for (int row = 0; row < printMaskRows; ++row)
                        {
                            const float m = printMask[(size_t) (row * printMaskCols + maskCol)];
                            if (m > 0.5f)
                            {
                                activeRowMin = juce::jmin (activeRowMin, row);
                                activeRowMax = juce::jmax (activeRowMax, row);
                            }
                        }

                        const bool hasActiveRows = (activeRowMax >= activeRowMin);
                        const float binHz = (float) sr / (float) juce::jmax (1, N);

                        float activeLowHz = fLow;
                        float activeHighHz = fHigh;
                        if (hasActiveRows && printMaskRows > 1)
                        {
                            const float rowToNorm = 1.0f / (float) (printMaskRows - 1);
                            activeLowHz  = fLow + ((float) activeRowMin * rowToNorm) * denom;
                            activeHighHz = fLow + ((float) activeRowMax * rowToNorm) * denom;
                        }

                        // 保护带：黑色形状上下限之外强制旁通；边缘给一个很陡的过渡，抑制误挖孔竖线。
                        const float protectPadHz = juce::jmax (2.0f * binHz, 0.005f * denom);
                        const float edgeSlopeHz  = juce::jmax (1.5f * binHz, 0.002f * denom);
                        const float protectLowHz = activeLowHz  - protectPadHz;
                        const float protectHighHz = activeHighHz + protectPadHz;

                        for (int k = 0; k < numBins; ++k)
                        {
                            const float hz = (float) k / (float) (N / 2) * maxHz;
                            if (hz < fLow || hz > fHigh)
                            {
                                targetBinGains[(size_t) k] = 1.0f;
                                continue;
                            }

                            const float norm = (hz - fLow) / denom;
                            const int row = juce::jlimit (0, printMaskRows - 1,
                                                          (int) std::floor (norm * (printMaskRows - 1)));
                            const float m = printMask[(size_t) (row * printMaskCols + maskCol)];
                            float g = invert
                                ? juce::jmap (m, ratioGain, 1.0f)
                                : juce::jmap (m, 1.0f,      ratioGain);

                            if (hasActiveRows)
                            {
                                if (hz < protectLowHz || hz > protectHighHz)
                                {
                                    g = 1.0f;
                                }
                                else
                                {
                                    const float loEdge = juce::jlimit (0.0f, 1.0f, (hz - protectLowHz) / edgeSlopeHz);
                                    const float hiEdge = juce::jlimit (0.0f, 1.0f, (protectHighHz - hz) / edgeSlopeHz);
                                    const float edgeKeep = juce::jmin (loEdge, hiEdge);
                                    g = 1.0f + (g - 1.0f) * edgeKeep;
                                }
                            }

                            targetBinGains[(size_t) k] = g;
                        }

                        // 频域轻度平滑（仅对掩码覆盖频段），抑制列间量化造成的孤立窄带竖线。
                        {
                            std::vector<float> filtered = targetBinGains;
                            for (int k = 1; k < numBins - 1; ++k)
                            {
                                const float hz = (float) k / (float) (N / 2) * maxHz;
                                if (hz < fLow || hz > fHigh)
                                    continue;

                                filtered[(size_t) k] = 0.2f * targetBinGains[(size_t) (k - 1)]
                                                     + 0.6f * targetBinGains[(size_t) k]
                                                     + 0.2f * targetBinGains[(size_t) (k + 1)];
                            }
                            targetBinGains.swap (filtered);
                        }

                        double gSum = 0.0;
                        gainMin = targetBinGains[0];
                        gainMax = targetBinGains[0];
                        for (float g : targetBinGains)
                        {
                            gainMin = juce::jmin (gainMin, g);
                            gainMax = juce::jmax (gainMax, g);
                            gSum += g;
                        }
                        gainMean = (float) (gSum / (double) juce::jmax (1, (int) targetBinGains.size()));

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
                gainMin = 1.0f;
                gainMax = 1.0f;
                gainMean = 1.0f;
            }

            // ---- 增益与 fade 协同：把 mask 增益向 unity 插值（系数 = 当前 mix）。
            //
            //  动机（修复 wet 路径 -0.84 dB 能量塌陷）：
            //    - 旧实现下，fade 一启动 mask 立即以完整强度施加到 STFT bin，但
            //      时域 mix 仍很小，OLA 累加缓冲中已带入 mask 衰减后的能量。
            //    - 当 mix 增大、最终全 wet 时，wet 信号本身已损失部分能量 →
            //      表现为 RMS 单调下降（日志：0.270 → 0.245，-0.84 dB）。
            //    - 协同插值：g_eff(k) = 1 + (g_target(k) - 1) * mix
            //      mix=0 时 wet 完整保留输入（与 dry 一致，零损失）；
            //      mix=1 时严格等于 mask 设计值。
            //      期间 wet 与 dry 的能量差线性渐变，不再出现 OLA 蓄积的"能量缺口"。
            {
                const float blend = juce::jlimit (0.0f, 1.0f, dryWetMix);
                if (blend < 1.0f - 1.0e-4f)
                {
                    for (auto& g : targetBinGains)
                        g = 1.0f + (g - 1.0f) * blend;
                }
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
            const float dryIn = buffer.getReadPointer (ch)[n];
            float wet = delayedDrySamples[(size_t) ch]; // FIFO 为空时回退到延迟 dry，避免硬切
            if (st.outFifoCount > 0)
            {
                wet = st.outFifo[(size_t) st.outFifoRead];
                st.outFifoRead = (st.outFifoRead + 1) % (N * 2);
                --st.outFifoCount;
            }
            wetSamples[(size_t) ch] = wet;

            const float out = delayedDrySamples[(size_t) ch] * mixDry
                            + wetSamples[(size_t) ch] * mixWet;
            buffer.getWritePointer (ch)[n] = out;

            MathLogEvent e;
            {
                std::lock_guard<std::mutex> lk (mathLogMutex);
                e.sampleIndex = mathLog.globalSampleCounter;
            }
            e.channel = ch;
            e.stftFrame = st.lastProcessedFrame;
            e.printRequested = requestedPrint;
            e.stftPathActive = stftPathActive;
            e.frameReady = frameReady;
            e.printStateFlipped = printStateFlipped;
            e.maskCol = usedMaskCol;
            e.fadeDelaySamples = printFadeDelaySamples;
            e.fadeSamplesRemaining = dryWetFadeSamplesRemaining;
            e.printWarmupRemaining = printWarmupFramesRemaining;
            e.accumCount = st.accumCount;
            e.printColCursor = (float) printColCursorD;
            e.targetGainMin = gainMin;
            e.targetGainMax = gainMax;
            e.targetGainMean = gainMean;
            e.dryIn = dryIn;
            e.delayedDry = delayedDrySamples[(size_t) ch];
            e.wetOut = wetSamples[(size_t) ch];
            e.outSample = out;
            e.dryWetMix = mixWet;
            e.dryWetTarget = dryWetTarget;
            e.targetGainProbe = st.lastTargetGainProbe;
            e.smoothGainProbe = st.lastSmoothGainProbe;
            e.fftMagProbe = st.lastFftMagProbe;
            e.ifftProbe = st.lastIfftProbe;
            e.olaNormProbe = st.lastOlaNormProbe;
            e.outFifoCount = st.outFifoCount;
            pushMathLogEvent (e);
        }

        {
            std::lock_guard<std::mutex> lk (mathLogMutex);
            ++mathLog.globalSampleCounter;
        }

        // 延迟启动 crossfade：等待 STFT 管线（OLA warmup / smoothedGains 恢复）达到稳态
        if (printFadeDelaySamples > 0)
        {
            --printFadeDelaySamples;
            if (printFadeDelaySamples <= 0)
            {
                // 延迟结束，启动实际 crossfade（目标值在状态翻转时已设定）。
                // 记录 fade 起点 mix，使后续推进可基于"已走样本数 / 总样本数"做精确线性 ramp，
                // 杜绝指数 IIR 收尾残差导致的硬切（旧实现末段会从 ~0.62 一次性跳到 1.0）。
                dryWetFadeTotalSamples       = juce::jmax (1, dryWetFadeTotalSamples);
                dryWetFadeSamplesRemaining   = dryWetFadeTotalSamples;
                dryWetFadeStartMix           = dryWetMix;
            }
        }

        if (dryWetFadeSamplesRemaining > 0)
        {
            // 精确线性 ramp：mix(t) = startMix + (target - startMix) * t / total
            //   t = total - remaining + 1（先消耗 1 sample 再赋值，确保 remaining=0 时 mix 严格命中 target）
            const int total = juce::jmax (1, dryWetFadeTotalSamples);
            const int t     = total - dryWetFadeSamplesRemaining + 1; // 1..total
            const float frac = juce::jlimit (0.0f, 1.0f, (float) t / (float) total);
            dryWetMix = dryWetFadeStartMix + (dryWetTarget - dryWetFadeStartMix) * frac;
            --dryWetFadeSamplesRemaining;
            if (dryWetFadeSamplesRemaining <= 0)
                dryWetMix = dryWetTarget;
        }
        else if (printFadeDelaySamples <= 0)
        {
            dryWetMix = dryWetTarget;
        }
    }

    // ------------------------------------------------------------------
    //  Block 级日志：在【处理后】采集每通道的输出信号统计快照。
    //  通过对比同一 block_index 下 pre vs post，可量化插件本身对信号
    //  引入的能量阶跃 / 冲激 / DC 偏移。
    // ------------------------------------------------------------------
    if (numCh > 0 && numSamps > 0)
    {
#if SPECTRUMTAG_PRINT_MATH_LOG_ENABLED
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float prevTail = (ch < (int) lastBlockTailSamplePost.size())
                                       ? lastBlockTailSamplePost[(size_t) ch]
                                       : 0.0f;
            BlockStats s;
            s.blockIndex = thisBlockIndex;
            s.printRequested = printRunning.load();
            s.stftPathActive = s.printRequested
                              || (dryWetMix > 1.0e-4f)
                              || (dryWetFadeSamplesRemaining > 0);
            s.dryWetMixSnapshot = dryWetMix;
            s.dryWetTargetSnapshot = dryWetTarget;
            s.fadeSamplesRemaining = dryWetFadeSamplesRemaining;
            s.fadeDelaySamples = printFadeDelaySamples;
            s.accumCountSnapshot = (ch < (int) stftStates.size())
                                       ? stftStates[(size_t) ch].accumCount : 0;
            s.outFifoCountSnapshot = (ch < (int) stftStates.size())
                                       ? stftStates[(size_t) ch].outFifoCount : 0;

            computeBlockStats (buffer.getReadPointer (ch), numSamps, ch,
                               /*isPost*/ true, blockStartGlobalSample,
                               prevTail, s);
            pushBlockStats (s);

            if (ch < (int) lastBlockTailSamplePost.size())
                lastBlockTailSamplePost[(size_t) ch] = buffer.getReadPointer (ch)[numSamps - 1];
        }
#else
        // 日志禁用：仅维护衔接尾样本
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (ch < (int) lastBlockTailSamplePost.size())
                lastBlockTailSamplePost[(size_t) ch] = buffer.getReadPointer (ch)[numSamps - 1];
        }
#endif
    }
    ++blockCounter;

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
    printWarmupFramesRemaining = juce::jmax (0, 2 * stftSize / juce::jmax (1, stftHop));

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

    // 工程加载后的伪边沿抑制：在接下来的 ~120ms 内忽略所有 trigger 上升沿，
    // 避免 DAW 重放参数时意外启动 Print。同时同步 lastPrintTriggerState，
    // 下一次 false→true 才会被认为算一个"新"边沿。
    if (printTriggerParam != nullptr)
        lastPrintTriggerState.store (printTriggerParam->get());
    automationPrintRequest.store (false);
    triggerSuppressEndTimeMs = juce::Time::currentTimeMillis() + 120;
}

// ===== APVTS::Listener =====
void SpectrumTagAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID != ParameterIDs::printTrigger) return;

    const bool nowOn = newValue >= 0.5f;
    const bool prev  = lastPrintTriggerState.exchange (nowOn);

    // 加载抑制窗口内：不产生请求，仅同步状态。
    if (juce::Time::currentTimeMillis() < triggerSuppressEndTimeMs) return;

    // 上升沿：false → true
    if (nowOn && ! prev)
        automationPrintRequest.store (true);
}

bool SpectrumTagAudioProcessor::consumeAutomationPrintRequest()
{
    return automationPrintRequest.exchange (false);
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

    // Print Trigger：布尔自动化参数，仅在 false→true 上升沿触发一次打印。
    // host 在工程中看到名为 "Print" 的布尔参数，可以录制自动化 / MIDI Learn。
    layout.add (std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::printTrigger, "Print Trigger", false));

    return layout;
}

// ===== JUCE 入口 =====
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrumTagAudioProcessor();
}