#include "shared/TimeScale.h"
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "shared/ImageMask.h"
#include "source/network/UpdateChecker.h"
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>

// ============================================================================
//  SpectrumTagAudioProcessor - STFT / OLA 实现（v3）
// ----------------------------------------------------------------------------
//  - 未按 Print：保留与 STFT 对齐的干声延迟，频谱分析继续运行。
//  - 按下 Print：STFT（93.75% overlap，hop = N/16），每个 bin 独立查 mask、
//    按二值化掩码修改增益、保留相位，使用 Hann WOLA 平滑重叠帧。
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
            case 3: return 4096; // Legacy automation slot; 8192 is retired.
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

    // 平台标识（与遥测口径一致，<os>-<arch>）
    juce::String getUpdatePlatformString()
    {
    #if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__arm64__)
        const juce::String arch = "arm64";
    #elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
        const juce::String arch = "x64";
    #else
        const juce::String arch = "x86";
    #endif

    #if JUCE_WINDOWS
        const juce::String os = "win";
    #elif JUCE_MAC
        const juce::String os = "mac";
    #elif JUCE_LINUX
        const juce::String os = "linux";
    #elif JUCE_BSD
        const juce::String os = "bsd";
    #else
        const juce::String os = "unknown";
    #endif

        return os + "-" + arch;
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
    const auto preferences=spectrumtag::Preferences::read();
    editorState.imagePath=preferences.imagePath;
    editorState.palette=preferences.palette;editorState.chinese=preferences.chineseTips;editorState.uiScale=preferences.uiScale;
    // 缓存 printTrigger 参数指针，后续复位/读值都走它。
    printTriggerParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParameterIDs::printTrigger));
    lastPrintTriggerState.store (printTriggerParam != nullptr && printTriggerParam->get());
    apvts.addParameterListener (ParameterIDs::printTrigger, this);

#if SPECTRUMTAG_ENABLE_NETWORK
    // 启动时延迟 5 秒检查一次更新（进程级去重，避免多实例重复触发）
    static std::atomic<bool> updateOnceFlag { false };
    if (! updateOnceFlag.exchange (true, std::memory_order_acquire))
    {
        juce::Timer::callAfterDelay (5000, []
        {
            spectrumtag::network::CheckForUpdatesAsync (
                "spectrumtag",
                juce::String (JucePlugin_VersionString),
                getUpdatePlatformString(),
                [] (const spectrumtag::network::UpdateInfo& info)
                {
                    if (info.has_update)
                        spectrumtag::network::ShowUpdateDialog (info);
                });
        });
    }
#endif
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
    const int hop     = N / 16;
    const int numBins = N / 2 + 1;

    stftHop  = hop;
    targetGainScratch.assign((size_t) numBins, 1.0f);
    dryScratch.assign((size_t) numChannels, 0.0f);
    wetScratch.assign((size_t) numChannels, 0.0f);
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
    //   binN/2(Nyq)   -> fftWork[N] (实部), 虚部固定为 0
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
        st.fftWork[(size_t) N] *= smooth;
    }

    st.lastSmoothGainProbe = st.smoothedGains[(size_t) probeBin];
    if (probeBin == 0)
    {
        st.lastFftMagProbe = std::abs (st.fftWork[0]);
    }
    else if (probeBin == numBins - 1)
    {
        st.lastFftMagProbe = std::abs (st.fftWork[(size_t) N]);
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
    // Finalise and clear every hop, including negative-time startup frames. Keeping
    // their normalisation weights until the ring wraps attenuates real input later.
    const bool ready = st.frameCount >= N / hop;
    for (int i = 0; i < hop; ++i)
    {
        const int pos = (frameStart + i) % N;
        const float norm = st.olaNormRing[(size_t) pos];
        if (ready)
        {
            const float y = norm > 1.0e-8f ? st.outputRing[(size_t) pos] / norm : 0.0f;
            st.outFifo[(size_t) st.outFifoWrite] = y;
            st.outFifoWrite = (st.outFifoWrite + 1) % (N * 2);
            ++st.outFifoCount;
        }
        st.outputRing[(size_t) pos] = 0.0f;
        st.olaNormRing[(size_t) pos] = 0.0f;
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
#if SPECTRUMTAG_ARA
    setLatencySamples(0);auditionTransport.reset();transportAdvancing=false;
    prepareToPlayForARA(sampleRate,samplesPerBlock,getMainBusNumOutputChannels(),getProcessingPrecision());return;
#endif
    juce::ignoreUnused (samplesPerBlock);
    if(offlineCaptureActive.exchange(false)){recordingEngine.setArmed(false);cancelPrint();}

    const int numCh = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    const int scale = getFrequencyScale();
    currentScaleMode = scale;

    // ---- STFT（音频处理路径）----
    const int stftSz = fftChoiceToSize (
        (int) *apvts.getRawParameterValue (ParameterIDs::fftSize));
    stftSize = stftSz;
    stftHop  = stftSz / 16;
    rebuildStft (sampleRate, numCh);

    // ---- 显示用 FFT（独立路径）----
    const int dispSize = stftSz;   // 显示与 STFT 共用同一尺寸
    displayFftSize = dispSize;
    displayHopSize = dispSize / 8;
    const int order = (int) std::round (std::log2 ((double) dispSize));
    displayFft = std::make_unique<juce::dsp::FFT> (order);

    displayWindow.assign ((size_t) dispSize, 0.0f);
    for (int n = 0; n < dispSize; ++n)
        displayWindow[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                              * (float) n / (float) (dispSize - 1)));

    displayInputRing.assign ((size_t) dispSize * juce::jmax(1,numCh), 0.0f);
    displayRingWritePos = 0;
    displaySamplesAccum = 0;
    displayFftWork.assign ((size_t) (2 * dispSize), 0.0f);

    {
        const juce::SpinLock::ScopedLockType sl (spectrumLock);
        latestMagnitudes.assign ((size_t) (dispSize / 2 + 1), 0.0f);
    }

    // STFT/OLA 的真实群延迟 = stftSize - 1（首帧输出在预热 N 个样本后，对应 input[0]）
    setLatencySamples (stftSize - 1);

    prevPrintActive = false;
    dryWetMix = 0.0f;
    dryWetTarget = 0.0f;
    dryWetFadeStartMix = 0.0f;
    dryWetFadeSamplesRemaining = 0;
    dryWetFadeTotalSamples = juce::jmax (1, (int) std::round (0.2 * sampleRate)); // 200ms，覆盖 OLA 8 帧预热期
    printFadeDelaySamples = 0;
    visualisedSamples.store(0);
    auditionTransport.reset(); recordingMix=0;renderFadeRemaining=0;fadeFromSnapshot.reset();playbackSnapshot.reset();renderFadeLength=juce::jmax(1,juce::roundToInt(sampleRate*.025));
    ++displayEpoch;
    transportAdvancing.store(false);

    // block 级日志：跨 block 衔接样本初始化（每通道一份）
    lastBlockTailSamplePre.assign  ((size_t) juce::jmax (1, numCh), 0.0f);
    lastBlockTailSamplePost.assign ((size_t) juce::jmax (1, numCh), 0.0f);
    blockCounter = 0;
}

void SpectrumTagAudioProcessor::releaseResources() {
#if SPECTRUMTAG_ARA
    releaseResourcesForARA();
#endif
    // Hosts may suspend immediately after freeze, without a stopped processBlock.
    // An armed-but-not-yet-started pass survives suspension before freeze begins.
    if(offlineCaptureActive.exchange(false)){recordingEngine.setArmed(false);cancelPrint();}
    transportAdvancing=false;auditionTransport.requestHostFollow();
}

// ============================================================================
//  processBlock：bypass（非 Print）/ STFT+OLA（Print 中）
// ============================================================================
void SpectrumTagAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    audioCallbackActive=true;
    struct CallbackExit {std::atomic<bool>& active;std::atomic<juce::uint32>& last;~CallbackExit(){last=juce::Time::getMillisecondCounter();active=false;}} callbackExit{audioCallbackActive,lastAudioCallback};
    lastAudioCallback.store(juce::Time::getMillisecondCounter(),std::memory_order_relaxed);
#if SPECTRUMTAG_ARA
    juce::ScopedNoDenormals araNoDenormals;
    const auto position=getPlayHead()?getPlayHead()->getPosition().orFallback(juce::AudioPlayHead::PositionInfo{}):juce::AudioPlayHead::PositionInfo{};
    double time=0;bool hasTime=false;
    if(auto s=position.getTimeInSamples()){time=*s/juce::jmax(1.,getSampleRate());hasTime=true;}
    else if(auto s=position.getTimeInSeconds()){time=*s;hasTime=true;}
    transportAdvancing=position.getIsPlaying();
    auditionTransport.advance(position.getIsPlaying(),hasTime,time,buffer.getNumSamples(),getSampleRate());
    if(isBoundToARA())processBlockForARA(buffer,isRealtime(),position);
    return;
#endif
    if(!isNonRealtime()&&offlineCaptureActive.exchange(false)){recordingEngine.setArmed(false);cancelPrint();}
    if(isNonRealtime()&&recordingEngine.isArmed())offlineCaptureActive=true;
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // 输出比输入多的通道清零
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numCh    = juce::jmin (totalNumInputChannels, (int) stftStates.size());
    const int numSamps = buffer.getNumSamples();
    bool playing = true,hasHostTime=false;
    double hostTime=0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition()) {
            playing=position->getIsPlaying();
            if(auto samples=position->getTimeInSamples()){hostTime=*samples/juce::jmax(1.0,getSampleRate());hasHostTime=true;}
            else if(auto seconds=position->getTimeInSeconds()){hostTime=*seconds;hasHostTime=std::isfinite(hostTime);}
        }
    if (isNonRealtime()) playing = true;
    const double auditionTime=auditionTransport.advance(playing,hasHostTime,hostTime,numSamps,getSampleRate());
    if(playing)recordingEngine.capture(buffer,auditionTransport.hostPosition(),getSampleRate(),isNonRealtime());
    recordingEngine.setDisplayFft(stftSize);
    const bool wasPlaying=transportAdvancing.exchange(playing,std::memory_order_acq_rel);
    if(!playing){
        if(wasPlaying||!recordingEngine.isEnabled()){printRunning.store(false);automationPrintRequest.store(false);}
        if(wasPlaying)recordingEngine.setArmed(false);
    }


    // -------------------------------------------------------------------
    //  Print Trigger（自动化）—— sample-clock 加载抑制窗口扣减
    //  + 离线/无 UI 场景下由音频线程自行消费 automationPrintRequest。
    //
    //  实时模式（isNonRealtime == false）：保持原行为，
    //    automationPrintRequest 由 Editor 的 30Hz timer 在消息线程里取走，
    //    可以充分复用 UI 当前实时的 ComboBox / ImageBox 状态；
    //    若实时模式下用户没打开 UI，请求会暂时挂起直至 UI 打开（符合常规体验）。
    //  离线/导出（isNonRealtime == true）：
    //    Editor timer 不会按实时节奏跑，宿主多数情况下根本不创建 Editor。
    //    这里直接在音频线程内取走并由 Processor 自行启动 Print，
    //    确保导出时自动化曲线触发的水印能正确写入输出音频。
    //    （此分支会做一次性的 image I/O，但离线渲染本身是非实时批处理，可接受。）
    // -------------------------------------------------------------------
    {
        int remaining = triggerSuppressSamplesRemaining.load (std::memory_order_acquire);
        if (remaining > 0)
        {
            const int next = juce::jmax (0, remaining - juce::jmax (0, numSamps));
            triggerSuppressSamplesRemaining.store (next, std::memory_order_release);
        }
    }

    if (isNonRealtime()
        && automationPrintRequest.load (std::memory_order_acquire)
        && ! printRunning.load())
    {
        // 取走标志位，避免本帧失败后下帧又试一次（仍然失败时按用户视角是"无图片"等
        // 永久性不可用情况，反复重试只会拖慢导出）。
        automationPrintRequest.store (false, std::memory_order_release);
        triggerPrintFromAutomationInternal();
    }

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
    const int desiredScale = getFrequencyScale();
    if (desiredScale != currentScaleMode)
        currentScaleMode = desiredScale;

    const int desiredSize = fftChoiceToSize (
        (int) *apvts.getRawParameterValue (ParameterIDs::fftSize));
    if (desiredSize != stftSize && (! printActive || (! prevPrintActive && dryWetMix <= 1.0e-4f)))
    {
        // 非 Print 期间才重建 STFT，避免 mid-print 爆音
        stftSize = desiredSize;
        stftHop  = desiredSize / 16;
        rebuildStft (sr, numCh);

        // 同步显示 FFT
        displayFftSize = desiredSize;
        displayHopSize = desiredSize / 8;
        const int order = (int) std::round (std::log2 ((double) desiredSize));
        displayFft = std::make_unique<juce::dsp::FFT> (order);
        displayWindow.assign ((size_t) desiredSize, 0.0f);
        for (int n = 0; n < desiredSize; ++n)
            displayWindow[(size_t) n] = 0.5f * (1.0f - std::cos (
                juce::MathConstants<float>::twoPi * (float) n / (float) (desiredSize - 1)));
        displayInputRing.assign ((size_t) desiredSize * juce::jmax(1,numCh), 0.0f);
        displayRingWritePos = 0;
        displaySamplesAccum = 0;
        displayFftWork.assign ((size_t) (2 * desiredSize), 0.0f);
        {
            const juce::SpinLock::ScopedLockType sl (spectrumLock);
            latestMagnitudes.assign ((size_t) (desiredSize / 2 + 1), 0.0f);
        }

        setLatencySamples (desiredSize - 1);

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
    // Dense Hann WOLA already crossfades frames; a second temporal low-pass erases small features.
    const float smoothAlpha = 1.0f;

    const float ratio     = *apvts.getRawParameterValue (ParameterIDs::amplitudeRatio);
    const bool  invert    = *apvts.getRawParameterValue (ParameterIDs::invert) > 0.5f;
    const float ratioGain = ratioToGain (ratio);

    auto& targetBinGains = targetGainScratch;
    auto& delayedDrySamples = dryScratch;
    auto& wetSamples = wetScratch;
    std::fill(targetBinGains.begin(), targetBinGains.end(), 1.0f);

    // dry 与 wet 保持同一延迟基准（与 STFT/OLA 群延迟 stftSize-1 一致），避免切换时相位/时序错位
    const int dryDelaySamples = N - 1;

    // ---- Sample-major 循环：每个采样点处理所有通道 ----
    for (int n = 0; n < numSamps; ++n)
    {
        int usedMaskCol = -1;
        bool printStateFlipped = false;
        const bool requestedPrint = printRunning.load() && playing;
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
                    // Keep the completed hop; resetting before subtracting it skips the next frame.
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
                                                          juce::roundToInt (norm * (printMaskRows - 1)));
                            const float m = printMask[(size_t) (row * printMaskCols + maskCol)];
                            float g = invert
                                ? juce::jmap (m, ratioGain, 1.0f)
                                : juce::jmap (m, 1.0f,      ratioGain);

                            targetBinGains[(size_t) k] = g;
                        }

                        // Preserve narrow strokes: do not blur the gain mask across neighbouring bins.
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
                            printRunning.store (false); // Keep the last column for this frame.
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

    // Only host-aligned gaps can use live input. Local audition has no audio
    // for an uncaptured time, so it stays silent until the next captured segment.
    auto recording=recordingEngine.isEnabled()?recordingEngine.snapshot():nullptr;
    if(recording!=playbackSnapshot){
        const bool sameSource=recording&&playbackSnapshot&&recording->sourceId==playbackSnapshot->sourceId;
        if(sameSource&&recording->rendered!=playbackSnapshot->rendered){fadeFromSnapshot=playbackSnapshot;renderFadeRemaining=renderFadeLength;}
        else if(!sameSource){fadeFromSnapshot.reset();renderFadeRemaining=0;}
        playbackSnapshot=recording;
    }
    if(recording){
        const double rate=juce::jmax(1.0,getSampleRate());
        const bool localAudition=playing&&auditionTransport.isLocal();
        const bool useRecording=playing&&(!recordingEngine.isArmed()||localAudition);
        for(int i=0;i<numSamps;++i){
            const double source=(auditionTime+(i-getLatencySamples())/rate)*recording->sampleRate;
            const auto index=(int64_t)std::floor(source);const float fraction=(float)(source-index);
            const bool captured=recording->contains(index);
            const float target=useRecording&&(captured||localAudition)?1.0f:0.0f;
            recordingMix+=juce::jlimit(-1.0f/128,1.0f/128,target-recordingMix);
            const float mix=renderFadeRemaining>0?1.0f-(float)renderFadeRemaining/renderFadeLength:1.0f;
            for(int c=0;c<numCh;++c){
                const auto read=[&](const auto& snap){const float a=snap->sample(c,index);return a+(snap->sample(c,index+1)-a)*fraction;};
                const float live=localAudition?0.f:buffer.getSample(c,i);
                float cached=captured?read(recording):live;
                if(captured&&renderFadeRemaining>0&&fadeFromSnapshot&&fadeFromSnapshot->contains(index)){
                    const float old=read(fadeFromSnapshot);cached=old+(cached-old)*mix;
                }
                buffer.setSample(c,i,live+(cached-live)*recordingMix);
            }
            if(renderFadeRemaining>0)--renderFadeRemaining;
        }
        if(renderFadeRemaining==0)fadeFromSnapshot.reset();
    }else recordingMix=0;

    if (playing) visualisedSamples.fetch_add((uint64_t) numSamps, std::memory_order_release);

    // Clear only the display analysis window, never the audio/Print delay lines.
    const auto clearSerial=spectrumClearSerial.load();
    if(clearSerial!=audioSpectrumClearSerial){
        audioSpectrumClearSerial=clearSerial;displayRingWritePos=displaySamplesAccum=0;
        std::fill(displayInputRing.begin(),displayInputRing.end(),0.f);
    }
    // ---- 显示用 FFT（读处理后的 buffer）----
    if (playing && displayFft != nullptr && displayFftSize > 0)
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
            for (int ch = 0; ch < numCh; ++ch)
                displayInputRing[(size_t)ch*dN+displayRingWritePos] = buffer.getReadPointer(ch)[i];
            displayRingWritePos = (displayRingWritePos + 1) % dN;
            ++displaySamplesAccum;

            if (displaySamplesAccum >= dHop)
            {
                displaySamplesAccum -= dHop;   // 保留溢出样本，避免 FFT 窗口漂移
                const int half = dN / 2 + 1;
                std::vector<float> mags ((size_t) half,0.f);
                // Match the recorded/ARA display: opposite channel polarity
                // must never erase a visible Print that exists in the audio.
                for(int ch=0;ch<numCh;++ch){
                    for(int n=0;n<dN;++n){const int idx=(displayRingWritePos+n)%dN;displayFftWork[(size_t)n]=displayInputRing[(size_t)ch*dN+idx]*displayWindow[(size_t)n];}
                    std::fill(displayFftWork.begin()+dN,displayFftWork.end(),0.f);
                    displayFft->performRealOnlyForwardTransform(displayFftWork.data());
                    for(int k=0;k<half;++k){const float re=displayFftWork[(size_t)2*k],im=displayFftWork[(size_t)2*k+1];mags[k]+=re*re+im*im;}
                }
                for(auto& magnitude:mags)magnitude=std::sqrt(magnitude/juce::jmax(1,numCh))*displayMagNorm;
                if(editorAttachCount.load(std::memory_order_relaxed)>0) {
                    spectrumtag::SpectrumFrame frame;
                    frame.time=((double)visualisedSamples.load() - numSamps + i + 1 - dN*0.5)/getSampleRate();
                    frame.sampleRate=getSampleRate();frame.interval=(double)dHop/getSampleRate();
                    frame.bins=juce::jmin(2049,half);frame.epoch=displayEpoch;frame.clearSerial=audioSpectrumClearSerial;
                    for(int k=0;k<frame.bins;++k) {
                        const float db=juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f,mags[(size_t)k]));
                        frame.level[(size_t)k]=(juce::uint8)juce::jlimit(0,255,juce::roundToInt((db+96.0f)*255.0f/96.0f));
                    }
                    spectrumQueue.push(frame);
                }
                {
                    const juce::SpinLock::ScopedLockType sl (spectrumLock);
                    latestMagnitudes = std::move (mags);
                    displayUpdateCounter.fetch_add (1, std::memory_order_release);
                }
            }
        }
    }
}

void SpectrumTagAudioProcessor::pollTransportTimeout()
{
    if(filePlayerMode)return;
    if(audioCallbackActive.load()||!transportAdvancing.load())return;
    const auto last=lastAudioCallback.load();
    const auto limit=(juce::uint32)juce::jmax(1000.0,4000.0*getBlockSize()/juce::jmax(1.0,getSampleRate()));
    if(last!=0&&juce::Time::getMillisecondCounter()-last>limit){
        transportAdvancing=false;printRunning=false;automationPrintRequest=false;offlineCaptureActive=false;
        recordingEngine.setArmed(false);auditionTransport.requestHostFollow();
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
    if (maskRows <= 0 || maskCols <= 0 || maskRows > 8193 || maskCols > 4096
        || maskData.size() != (size_t) maskRows * (size_t) maskCols
        || !std::isfinite(durationSeconds) || durationSeconds <= 0
        || !std::isfinite(freqLowNorm) || !std::isfinite(freqHighNorm) || freqLowNorm >= freqHighNorm)
        return;
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
    tree.setProperty ("version", 6, nullptr);
#if !SPECTRUMTAG_ARA
    tree.setProperty("recordMode",recordingEngine.isEnabled(),nullptr);
    tree.setProperty("recordingFile",recordingEngine.save(),nullptr);
#endif

    tree.appendChild (apvts.copyState(), nullptr);

    EditorState s = getEditorState();
    tree.setProperty("palette",s.palette,nullptr);
    tree.setProperty("imageVisible",s.imageVisible,nullptr);
    tree.setProperty("languageChinese",s.chinese,nullptr);tree.setProperty("uiScale",s.uiScale,nullptr);
    tree.setProperty("viewEnd",s.viewEnd,nullptr);tree.setProperty("viewZoom",s.viewZoom,nullptr);
    tree.setProperty("viewFollow",s.viewFollow,nullptr);tree.setProperty("viewFit",s.viewFit,nullptr);tree.setProperty("viewValid",s.viewValid,nullptr);
    tree.setProperty("selectedPrint",s.selectedPrint,nullptr);tree.setProperty("placementId",s.placementId,nullptr);
    tree.setProperty ("imgBoxWidthPx", s.imgBoxWidthPx, nullptr);
    tree.setProperty("spectrumHeightPx",s.spectrumHeightPx,nullptr);
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
            if (paramsTree.hasType ("PARAMETERS")) {
                // A reused instance loading a pre-1.4.9 project must leave insight mode.
                if(!paramsTree.getChildWithProperty("id",ParameterIDs::frequencyPreset).isValid()) {
                    juce::ValueTree choice("PARAM");choice.setProperty("id",ParameterIDs::frequencyPreset,nullptr);
                    choice.setProperty("value",0.f,nullptr);paramsTree.appendChild(choice,nullptr);
                }
                if ((int)tree.getProperty("version",0) < 6) {
                    auto speedNode=paramsTree.getChildWithProperty("id",ParameterIDs::speed);
                    const int preset=(int)paramsTree.getChildWithProperty("id",ParameterIDs::frequencyPreset).getProperty("value",0);
                    float speed=(float)speedNode.getProperty("value",1.f);
                    double zoom=(double)tree.getProperty("viewZoom",1.0);
                    if(!std::isfinite(speed)||speed<.1f||speed>4) speed=1;
                    if(!std::isfinite(zoom)||zoom<=0) zoom=1;
                    const int height=juce::jlimit(1,4000,(int)tree.getProperty("spectrumHeightPx",550));
                    spectrumtag::timing::migrateLegacy(preset+1,height,speed,zoom);
                    if(speedNode.isValid())speedNode.setProperty("value",speed,nullptr);
                    tree.setProperty("viewZoom",zoom,nullptr);
                }
                apvts.replaceState (paramsTree);
#if SPECTRUMTAG_ARA
                araWrapperFrequencyScale=getFrequencyScale();araFrequencyRecallRequested=true;
#endif
            }
        }

        undoChanges.clear();redoChanges.clear();
#if !SPECTRUMTAG_ARA
        recordingEngine.setDisplayFft(1024<<juce::jlimit(0,2,(int)apvts.getRawParameterValue(ParameterIDs::fftSize)->load()));
        recordingEngine.restore(tree.getProperty("recordingFile").toString(),(bool)tree.getProperty("recordMode",false));
#endif
        EditorState s;
        s.palette=juce::jlimit(0,7,(int)tree.getProperty("palette",0));
        s.imageVisible=(bool)tree.getProperty("imageVisible",true);
        const auto preferences=spectrumtag::Preferences::read();
        s.chinese=(bool)tree.getProperty("languageChinese",preferences.chineseTips);s.uiScale=juce::jlimit(.75f,2.5f,(float)tree.getProperty("uiScale",preferences.uiScale));
        s.viewEnd=(double)tree.getProperty("viewEnd",0.0);s.viewZoom=(double)tree.getProperty("viewZoom",1.0);
        if(!std::isfinite(s.viewEnd))s.viewEnd=0;if(!std::isfinite(s.viewZoom)||s.viewZoom<=0)s.viewZoom=1;
        s.viewFollow=(bool)tree.getProperty("viewFollow",true);s.viewFit=(bool)tree.getProperty("viewFit",false);s.viewValid=(bool)tree.getProperty("viewValid",false);
        s.selectedPrint=tree.getProperty("selectedPrint").toString();s.placementId=tree.getProperty("placementId").toString();
        s.imagePath     = tree.getProperty ("imagePath",     s.imagePath).toString();
        s.imgRectXNorm  = (float) tree.getProperty ("imgRectXNorm",  s.imgRectXNorm);
        s.imgRectYNorm  = (float) tree.getProperty ("imgRectYNorm",  s.imgRectYNorm);
        s.imgRectWNorm  = (float) tree.getProperty ("imgRectWNorm",  s.imgRectWNorm);
        s.imgRectHNorm  = (float) tree.getProperty ("imgRectHNorm",  s.imgRectHNorm);
        s.imgBoxWidthPx = (float)tree.getProperty("imgBoxWidthPx", s.imgBoxWidthPx);
        s.spectrumHeightPx=juce::jlimit(1,4000,(int)tree.getProperty("spectrumHeightPx",550));
        s.hasValidValues = true;

        const juce::SpinLock::ScopedLockType sl (editorStateLock);
        editorState = s;
        stateRevision.fetch_add(1);
    }

    // 工程加载后的伪边沿抑制：在接下来的 ~100ms 等量样本内忽略所有 trigger 上升沿，
    // 避免 DAW 重放参数时意外启动 Print。
    //  - 之前用墙钟（juce::Time::currentTimeMillis）计时，在【离线导出/渲染】场景下
    //    宿主推进 audio block 远快于实时，墙钟 100~120ms 内可能已渲染了好几秒音频，
    //    导致自动化曲线开头的第一次上升沿被误吞掉 → 水印缺失。
    //  - 改为 sample-clock：在 processBlock 入口按 numSamples 扣减，与音频时间锚定，
    //    实时与离线行为一致。
    if (printTriggerParam != nullptr)
        lastPrintTriggerState.store (printTriggerParam->get());
    automationPrintRequest.store (false);

    const double sr = getSampleRate();
    const int suppressSamples = juce::jmax (1024,
        (int) std::round ((sr > 1000.0 ? sr : 44100.0) * 0.10));
    triggerSuppressSamplesRemaining.store (suppressSamples, std::memory_order_release);
    triggerSuppressEndTimeMs = juce::Time::currentTimeMillis() + 120; // 兼容字段（不再使用）
}

// ===== APVTS::Listener =====
void SpectrumTagAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID != ParameterIDs::printTrigger) return;

    const bool nowOn = newValue >= 0.5f;
    const bool prev  = lastPrintTriggerState.exchange (nowOn);

    // 加载抑制窗口内：不产生请求，仅同步状态。
    // 用 sample-clock：只要 processBlock 还没把窗口扣减到 0 就视作"加载抑制中"。
    if (triggerSuppressSamplesRemaining.load (std::memory_order_acquire) > 0) return;

    // 上升沿：false → true
    if (nowOn && ! prev)
        automationPrintRequest.store (true);
}

bool SpectrumTagAudioProcessor::consumeAutomationPrintRequest()
{
    return automationPrintRequest.exchange (false);
}

void SpectrumTagAudioProcessor::notifyEditorAttached()
{
    editorAttachCount.fetch_add (1, std::memory_order_acq_rel);
}

void SpectrumTagAudioProcessor::notifyEditorDetached()
{
    int prev = editorAttachCount.fetch_sub (1, std::memory_order_acq_rel);
    if (prev <= 0)
        editorAttachCount.store (0, std::memory_order_release); // 防御性归零
}

// ----------------------------------------------------------------------------
//  triggerPrintFromAutomationInternal
//   离线导出（isNonRealtime）或宿主未创建 Editor 时由 processBlock 入口调用。
//   完整复现 Editor::onPrintClicked 中的 mask 生成与 startPrint 逻辑，
//   但所有数据来源都改为 Processor 自身（EditorState 镜像 + APVTS）。
//
//   注意：此函数会做文件 I/O（juce::ImageFileFormat::loadFrom），
//   因此仅在以下两种场景调用是安全/可接受的：
//     1) isNonRealtime() == true：宿主导出/冻结，本身就不要求实时；
//     2) editorAttachCount == 0：实时模式下用户没打开过 UI，极小概率，
//        I/O 影响有限且必要；用户关心的"声音不卡"前提下打印也才有意义。
//   返回 true = 成功调起 startPrint；false = 未启动（mask/图片不可用等）。
// ----------------------------------------------------------------------------
bool SpectrumTagAudioProcessor::triggerPrintFromAutomationInternal()
{
    if (printRunning.load()) return false;

    // 1) 取 EditorState 镜像（图片路径 + 归一化矩形）
    EditorState state = getEditorState();
    if (! state.hasValidValues || state.imagePath.isEmpty()) return false;

    juce::File imgFile (state.imagePath);
    if (! imgFile.existsAsFile()) return false;

    juce::Image src = juce::ImageFileFormat::loadFrom (imgFile);
    if (! src.isValid()) return false;

    const spectrumtag::image::Settings conversion {
        (int)*apvts.getRawParameterValue(ParameterIDs::imageMode),
        *apvts.getRawParameterValue(ParameterIDs::imageAmount) };
    auto binary = spectrumtag::image::binarize(src, conversion);

    // 3) 计算 rows / cols / 频率范围 / duration
    const int fftSizeIdx = (int) *apvts.getRawParameterValue (ParameterIDs::fftSize);
    const int fftSize    = (fftSizeIdx == 0 ? 1024 : fftSizeIdx == 1 ? 2048
                          : 4096);
    const int rows = fftSize / 2 + 1;

    // 离线/无 UI 场景下没有真实组件像素宽度。
    // 从 EditorState 镜像中读取 UI 持久化时保存的实际 boxWidthPx，
    // 使离线路径的 cols 和 duration 与预览路径完全一致，
    // Editor-closed Print uses the same mode-independent clock as the viewport.
    const float boxWidthPx = juce::jmax (1.0f, state.imgBoxWidthPx);
    int cols = juce::jmax (4, juce::jmin (4096, juce::roundToInt (boxWidthPx)));

    const float speed = (float) *apvts.getRawParameterValue (ParameterIDs::speed);
    const double seconds = (double)boxWidthPx / (spectrumtag::timing::pixelsPerSecond * speed * (state.viewValid?juce::jmax(.000001,state.viewZoom):1.));

    // Use the same Hz mapping and image warp as the editor, also with no UI open.
    constexpr float kMinDisplayHz = 20.0f;
    const float ranges[]={20000,24000,22050,48000};
    const float kMaxDisplayHz=ranges[juce::jlimit(0,3,(int)*apvts.getRawParameterValue(ParameterIDs::frequencyRange))];
    const double sr = getSampleRate() > 1000.0 ? getSampleRate() : 44100.0;
    const float maxHz = (float) (sr * 0.5);
    const float topHz = juce::jmax (kMinDisplayHz + 1.0f, juce::jmin (kMaxDisplayHz, maxHz));

    const float yTop = juce::jlimit (0.0f, 1.0f, state.imgRectYNorm);
    const float yBot = juce::jlimit (0.0f, 1.0f, state.imgRectYNorm + state.imgRectHNorm);
    const int scale=getFrequencyScale();
    auto yToHz = [&] (float y) { return spectrumtag::frequency::fromY(y,topHz,scale); };
    const float fHz_top = yToHz(yTop);
    const float fHz_bottom = yToHz(yBot);
    const float fLow  = juce::jmin (fHz_top, fHz_bottom);
    const float fHigh = juce::jmax (fHz_top, fHz_bottom);
    const float lowNorm  = juce::jlimit (0.0f, 1.0f, fLow  / juce::jmax (1.0f, maxHz));
    const float highNorm = juce::jlimit (0.0f, 1.0f, fHigh / juce::jmax (1.0f, maxHz));

    auto mask = spectrumtag::image::rasterize(binary, rows, cols, scale, fLow, fHigh);

    startPrint (std::move (mask), rows, cols, lowNorm, highNorm, seconds);
    return true;
}

// ===== 参数布局 =====
juce::AudioProcessorValueTreeState::ParameterLayout SpectrumTagAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::fftSize, "FFT Size",
        juce::StringArray { "1024", "2048", "4096", "4096 (legacy)" },
        2));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::fftScale, "FFT Scale",
        juce::StringArray { "linear", "log" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::speed, "Speed",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.0f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 3) + "x"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::amplitudeRatio, "Amplitude Ratio",
        juce::NormalisableRange<float>(0.0f, 0.33f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (v * 100.0f, 1) + "%";
            }).withValueFromStringFunction([](const juce::String& text){return juce::jlimit(0.0f,.33f,text.getFloatValue()/100); })));

    layout.add (std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::invert, "Invert", false));

    // Print Trigger：布尔自动化参数，仅在 false→true 上升沿触发一次打印。
    // host 在工程中看到名为 "Print" 的布尔参数，可以录制自动化 / MIDI Learn。
    layout.add (std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::printTrigger, "Print Trigger", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterIDs::imageMode, "Image B/W Mode",
        juce::StringArray { "Auto (Otsu)", "Detail (Adaptive)", "Manual", "Alpha silhouette" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterIDs::imageAmount, "Image B/W Amount",
        juce::NormalisableRange<float>(0.0f,1.0f,0.001f),0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterIDs::noiseFill,"Noise Fill",juce::NormalisableRange<float>(0.0f,1.0f,.001f),0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value,int){return juce::String(juce::roundToInt(value*100))+"%";}).withValueFromStringFunction([](const juce::String& text){return juce::jlimit(0.0f,1.0f,text.getFloatValue()/100); })));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterIDs::frequencyRange,"Frequency range",
        juce::StringArray{"20 kHz","24 kHz","22.05 kHz","48 kHz"},0));
    // Appended parameter preserves every existing parameter index and the old
    // fftScale 0/1 host automation. Retired slot 2 resolves to Log; the editor shows six modes.
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterIDs::frequencyPreset,"Frequency scale preset",juce::StringArray{"Linear / Log","insight","Log","Mel (RX)","Bark","Extended Log"},3));
    return layout;
}

// ===== JUCE 入口 =====
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrumTagAudioProcessor();
}
namespace {
bool sameEditor(const SpectrumTagAudioProcessor::EditorState& a,const SpectrumTagAudioProcessor::EditorState& b){
    return a.imagePath==b.imagePath&&a.palette==b.palette&&a.imageVisible==b.imageVisible&&a.chinese==b.chinese&&a.uiScale==b.uiScale
        &&a.imgRectXNorm==b.imgRectXNorm&&a.imgRectYNorm==b.imgRectYNorm&&a.imgRectWNorm==b.imgRectWNorm&&a.imgRectHNorm==b.imgRectHNorm
        &&a.viewEnd==b.viewEnd&&a.viewZoom==b.viewZoom&&a.viewFollow==b.viewFollow&&a.viewFit==b.viewFit&&a.viewValid==b.viewValid
        &&a.selectedPrint==b.selectedPrint&&a.placementId==b.placementId&&a.spectrumHeightPx==b.spectrumHeightPx;
}
}
SpectrumTagAudioProcessor::UserState SpectrumTagAudioProcessor::captureUserState() const {
    UserState s;for(auto* parameter:getParameters())s.parameters.push_back(parameter==printTriggerParam?0:parameter->getValue());
    s.editor=getEditorState();s.document=recording().document();s.armed=recording().isArmed();return s;
}
void SpectrumTagAudioProcessor::commitUserChange(UserState before,UserState after,bool sourceChange){
    if(restoringUserState)return;
    if(!sourceChange&&before.parameters==after.parameters&&sameEditor(before.editor,after.editor)
        &&spectrumtag::sameEdits(before.document.edits,after.document.edits)&&before.armed==after.armed)return;
    if(!sourceChange){before.document.source.reset();after.document.source.reset();}
    undoChanges.push_back({std::move(before),std::move(after),sourceChange});
    if(undoChanges.size()>64)undoChanges.erase(undoChanges.begin());redoChanges.clear();
}
void SpectrumTagAudioProcessor::clearRecordedAudio(){
    if(pendingClear)return;
    beforeClear=captureUserState();beforeClear.armed=false;
    cancelPrint();auditionTransport.requestHostFollow();clearSpectrumHistory();
    pendingClear=recordingEngine.clearKeepingUndo();
}
void SpectrumTagAudioProcessor::finishPendingClear(){
    if(!pendingClear||!pendingClear->ready.load())return;
    beforeClear.document=pendingClear->before;
    auto after=captureUserState();after.document={};after.armed=false;
    commitUserChange(std::move(beforeClear),std::move(after),true);pendingClear.reset();
}
bool SpectrumTagAudioProcessor::undoUserChange(bool redo){
    finishPendingClear();if(pendingClear)return false;
    auto& from=redo?redoChanges:undoChanges;auto& to=redo?undoChanges:redoChanges;if(from.empty())return false;
    auto change=std::move(from.back());from.pop_back();const auto& target=redo?change.after:change.before;
    restoringUserState=true;cancelPrint();
#if SPECTRUMTAG_ARA
    if(target.ara){if(auto* view=getEditorView<spectrumtag::ara::View>())view->restoreState(*target.ara);clearSpectrumHistory();}
    else
#endif
    if(change.sourceChange){recordingEngine.restoreDocument(target.document);if(!target.document.enabled)clearSpectrumHistory();}
    else if(!spectrumtag::sameEdits(change.before.document.edits,change.after.document.edits))recording().setEdits(target.document.edits?*target.document.edits:std::vector<spectrumtag::PrintEdit>{});
    if(change.sourceChange||change.before.armed!=change.after.armed)recordingEngine.setArmed(target.armed);
    const auto& parameters=getParameters();
    for(int i=0;i<parameters.size()&&i<(int)target.parameters.size();++i)
        if(parameters[i]!=printTriggerParam&&change.before.parameters[(size_t)i]!=change.after.parameters[(size_t)i]){
            parameters[i]->beginChangeGesture();parameters[i]->setValueNotifyingHost(target.parameters[(size_t)i]);parameters[i]->endChangeGesture();
        }
    rememberARAFrequencyChoice();
    setEditorState(target.editor);stateRevision.fetch_add(1);
    auditionTransport.requestHostFollow();restoringUserState=false;to.push_back(std::move(change));return true;
}

void SpectrumTagAudioProcessor::clearARA(){
#if SPECTRUMTAG_ARA
    if(auto* view=getEditorView<spectrumtag::ara::View>();view&&view->loaded){
        auto before=captureUserState();before.ara=std::make_shared<const spectrumtag::ara::ViewState>(view->captureState());
        view->clear();cancelPrint();clearSpectrumHistory();
        auto state=getEditorState();state.selectedPrint={};state.placementId={};state.viewValid=false;state.viewFit=true;setEditorState(state);stateRevision.fetch_add(1);
        auto after=captureUserState();after.ara=std::make_shared<const spectrumtag::ara::ViewState>(view->captureState());
        before.document.source.reset();after.document.source.reset();commitUserChange(std::move(before),std::move(after),true);
    }
#endif
}
void SpectrumTagAudioProcessor::loadARA(){
#if SPECTRUMTAG_ARA
    if(auto* view=getEditorView<spectrumtag::ara::View>()){
        view->setRate(getSampleRate()>0?getSampleRate():48000.);
        std::vector<spectrumtag::ara::Region*> regions;
        if(auto* renderer=getPlaybackRenderer())regions=renderer->getPlaybackRegions<spectrumtag::ara::Region>();
        view->load(regions);pollARA();
    }
#endif
}
juce::String SpectrumTagAudioProcessor::araStatus()const{
#if SPECTRUMTAG_ARA
    if(auto* view=getEditorView<spectrumtag::ara::View>())return view->statusText(getEditorState().chinese);
    return getEditorState().chinese?juce::String::fromUTF8("请从 Cubase 音频事件的扩展中打开 SpectrumTag ARA。"):juce::String("Open SpectrumTag ARA from the Cubase audio event extension.");
#else
    return {};
#endif
}
void SpectrumTagAudioProcessor::rememberARAFrequencyChoice(){
#if SPECTRUMTAG_ARA
    if(auto* view=getEditorView<spectrumtag::ara::View>())view->rememberFrequencyChoice(getFrequencyScale());
#endif
}
void SpectrumTagAudioProcessor::pollARA(){
#if SPECTRUMTAG_ARA
    if(auto* view=getEditorView<spectrumtag::ara::View>()){
        view->setRate(getSampleRate()>0?getSampleRate():48000.);view->tick();
        const bool archiveRestored=view->restoreRevision!=araRestoreRevision;
        const bool contextChanged=araHistoryKey!=view->key||araSelectionRevision!=view->selectionRevision;
        if(archiveRestored){araHistory.clear();undoChanges.clear();redoChanges.clear();araRestoreRevision=view->restoreRevision;}
        if(!view->selected.empty()&&!view->getDocumentController()->isHostEditingDocument()
            &&(contextChanged||archiveRestored||araFrequencyRecallRequested.load())){
            const auto choice=view->frequencyChoice();
            const int mode=choice.value_or(araWrapperFrequencyScale.load());
            araFrequencyRecallRequested=false;
            if(mode>=0){
                const juce::ScopedValueSetter<bool> guard(restoringUserState,true);
                auto assign=[&](const juce::String& id,float value){auto* p=apvts.getParameter(id);p->setValueNotifyingHost(p->convertTo0to1(value));};
                assign(ParameterIDs::fftScale,mode==1?1.f:0.f);
                assign(ParameterIDs::frequencyPreset,(float)juce::jmax(0,mode-1));
                stateRevision.fetch_add(1);
            }
        }
        if(contextChanged){
            if(araHistoryKey.isNotEmpty())araHistory[araHistoryKey]={std::move(undoChanges),std::move(redoChanges)};
            araHistoryKey=view->key;auto& history=araHistory[araHistoryKey];undoChanges=history.undo;redoChanges=history.redo;araSelectionRevision=view->selectionRevision;
            auto state=getEditorState();state.selectedPrint={};state.placementId={};state.viewValid=false;state.viewFollow=false;state.viewFit=true;setEditorState(state);stateRevision.fetch_add(1);
        }
    }
#endif
}
