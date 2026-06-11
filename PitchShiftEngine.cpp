#include "PitchShiftEngine.h"
#include <rubberband/RubberBandLiveShifter.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using RubberBand::RubberBandLiveShifter;

namespace
{
// Probe the effective stream alignment by sending a single impulse through
// the shifter and locating the strongest output sample.
int estimateShifterLatencySamples(RubberBandLiveShifter& shifter, int rbBlockSize)
{
    if (rbBlockSize <= 0)
        return 0;

    const int probeBlocks = 48;
    std::vector<float> in((size_t) rbBlockSize, 0.0f);
    std::vector<float> out((size_t) rbBlockSize, 0.0f);

    const float* inPtrs[1] = { in.data() };
    float* outPtrs[1] = { out.data() };

    bool impulseSent = false;
    int sampleBase = 0;
    float maxAbs = 0.0f;
    int maxIndex = 0;

    for (int block = 0; block < probeBlocks; ++block)
    {
        std::fill(in.begin(), in.end(), 0.0f);
        if (!impulseSent)
        {
            in[0] = 1.0f;
            impulseSent = true;
        }

        shifter.shift(inPtrs, outPtrs);

        for (int i = 0; i < rbBlockSize; ++i)
        {
            const float a = std::abs(out[(size_t) i]);
            if (a > maxAbs)
            {
                maxAbs = a;
                maxIndex = sampleBase + i;
            }
        }

        sampleBase += rbBlockSize;
    }

    if (maxAbs < 1.0e-8f)
        return (int) shifter.getStartDelay();

    return juce::jmax(0, maxIndex);
}

void primeDelayLine(std::vector<float>& ring, int& head, int& tail, int delaySamples)
{
    head = 0;
    tail = 0;
    if (delaySamples <= 0)
        return;

    if ((int) ring.size() < delaySamples)
        ring.resize((size_t) delaySamples, 0.0f);

    std::fill(ring.begin(), ring.end(), 0.0f);
    tail = delaySamples;
}
}

PitchShiftEngine::PitchShiftEngine()
{
    for (int i = 0; i < kNumBands; ++i)
    {
        dotGains[(size_t)i].store(0.0f, std::memory_order_relaxed);
        dotPans [(size_t)i].store(0.0f, std::memory_order_relaxed);
        dotSemitones[(size_t)i].store(kDefaultSemitones[(size_t)i], std::memory_order_relaxed);
        bandEnabled[(size_t)i].store(true, std::memory_order_relaxed);
    }
}

PitchShiftEngine::~PitchShiftEngine() = default;

void PitchShiftEngine::setFilterCenterOffsetSemitones(int centerOffsetSt)
{
    filterCenterOffsetSt.store(juce::jlimit(-48, +48, centerOffsetSt), std::memory_order_relaxed);
}

void PitchShiftEngine::setFilterWidthSemitones(int widthSt)
{
    filterWidthSt.store(juce::jlimit(10, 72, widthSt), std::memory_order_relaxed);
}

void PitchShiftEngine::setPitchQualityMode(int qualityMode)
{
    pitchQualityMode.store(juce::jlimit(0, 2, qualityMode), std::memory_order_relaxed);
}

void PitchShiftEngine::setFormantMode(int mode)
{
    formantMode.store(juce::jlimit(0, 1, mode), std::memory_order_relaxed);
}

int PitchShiftEngine::makeRubberBandOptions(int qualityMode, int formantMode) const
{
    // 质量模式映射到 LiveShifter 可用选项（窗口大小/声道策略）
    int opts = 0;
    switch (juce::jlimit(0, 2, qualityMode))
    {
        case 0: // Fastest
            opts |= RubberBandLiveShifter::OptionResamplerFastest;
            opts |= RubberBandLiveShifter::OptionWindowShort;
            opts |= RubberBandLiveShifter::OptionChannelsTogether;
            break;
        case 2: // Best
            opts |= RubberBandLiveShifter::OptionResamplerBest;
            opts |= RubberBandLiveShifter::OptionWindowMedium;
            opts |= RubberBandLiveShifter::OptionChannelsApart;
            break;
        case 1: // FastestTolerable
        default:
            opts |= RubberBandLiveShifter::OptionResamplerFastestTolerable;
            opts |= RubberBandLiveShifter::OptionWindowShort;
            opts |= RubberBandLiveShifter::OptionChannelsApart;
            break;
    }

    opts |= (juce::jlimit(0, 1, formantMode) == 1)
        ? RubberBandLiveShifter::OptionFormantPreserved
        : RubberBandLiveShifter::OptionFormantShifted;

    return opts;
}

std::pair<float, float> PitchShiftEngine::computeBandCutoffsHz(int semitone, int centerOffset, int widthSt) const
{
    // 规则基线：
    // -36st -> [C1, C3], 0st -> [C4, C6], +36st -> [C7, C9]
    // 等价中心表示：center(0st)=C5，随后按 semitone 线性平移。
    const int st = juce::jlimit(-36, +36, semitone);
    const int width = juce::jlimit(10, 72, widthSt);
    const float halfWidth = 0.5f * (float) width;

    const float centerMidi = 72.0f + (float) st + (float) centerOffset; // C5 + st + 全局偏移
    float lowMidi  = centerMidi - halfWidth;
    float highMidi = centerMidi + halfWidth;

    auto midiToHz = [](float midi) -> float
    {
        return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
    };

    float lowHz  = midiToHz(lowMidi);
    float highHz = midiToHz(highMidi);

    const float nyquistSafe = juce::jmax(200.0f, (float)sampleRate * 0.49f);
    lowHz  = juce::jlimit(10.0f, nyquistSafe * 0.95f, lowHz);
    highHz = juce::jlimit(lowHz + 10.0f, nyquistSafe, highHz);

    return { lowHz, highHz };
}

void PitchShiftEngine::updateBandFilters(int band, int ch, int semitone, int centerOffset, int widthSt)
{
    auto& bc = state[(size_t)band][(size_t)ch];
    if (!bc.shifter)
        return;

    const auto cutoffs = computeBandCutoffsHz(semitone, centerOffset, widthSt);
    const float lowCutHz  = cutoffs.first;
    const float highCutHz = cutoffs.second;

    if (std::abs(bc.appliedLowCutHz - lowCutHz) < 0.01f
        && std::abs(bc.appliedHighCutHz - highCutHz) < 0.01f)
    {
        return;
    }

    const auto hp = juce::IIRCoefficients::makeHighPass(sampleRate, lowCutHz);
    const auto lp = juce::IIRCoefficients::makeLowPass(sampleRate, highCutHz);

    for (auto& f : bc.highPassFilters)
        f.setCoefficients(hp);
    for (auto& f : bc.lowPassFilters)
        f.setCoefficients(lp);

    bc.appliedLowCutHz = lowCutHz;
    bc.appliedHighCutHz = highCutHz;
}

void PitchShiftEngine::processBandFilters(int band, int ch, float* samples, int n)
{
    if (samples == nullptr || n <= 0)
        return;

    auto& bc = state[(size_t)band][(size_t)ch];
    if (!bc.shifter)
        return;

    for (auto& hp : bc.highPassFilters)
        hp.processSamples(samples, n);
    for (auto& lp : bc.lowPassFilters)
        lp.processSamples(samples, n);
}

void PitchShiftEngine::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate           = newSampleRate;
    maxBlockSizePrepared = juce::jmax(1, maxBlockSize);
    numChannelsPrepared  = juce::jlimit(1, 2, numChannels);

    // 为每个 band/ch 创建一个"单声道 LiveShifter"
    // 选项说明：
    //   OptionWindowShort         —— 低延迟窗口，音质/延迟的合理折中（~55ms@44.1k）
    //   OptionFormantShifted      —— 不做共振峰保留（对非人声一般更自然；需要保留人声
    //                                口型可以改为 OptionFormantPreserved）
    //   OptionChannelsApart       —— 多声道独立处理（我们这里每个 shifter 只有 1 ch，
    //                                此选项对单声道无实际影响）
    const int quality = pitchQualityMode.load(std::memory_order_relaxed);
    const int formant = formantMode.load(std::memory_order_relaxed);
    const int rbOptions = makeRubberBandOptions(quality, formant);

    const int centerOffset = filterCenterOffsetSt.load(std::memory_order_relaxed);
    const int width = filterWidthSt.load(std::memory_order_relaxed);

    reportedLatency = 0;
    bandLatencySamples.fill(0);
    bandAlignDelaySamples.fill(0);

    for (int b = 0; b < kNumBands; ++b)
    {
        const int semitone = dotSemitones[(size_t)b].load(std::memory_order_relaxed);
        appliedSemitones[(size_t)b] = semitone;
        const double ratio = std::pow(2.0, (double) semitone / 12.0);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& s = state[(size_t)b][(size_t)ch];

            // 【优化】只为由启用的 band 创建 shifter，禁用 band 释放资源
            if (bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            {
                s.shifter = std::make_unique<RubberBandLiveShifter>(
                    (size_t)std::llround(sampleRate), /* channels */ 1, rbOptions);
                s.shifter->setPitchScale(ratio);

                s.rbBlockSize = (int)s.shifter->getBlockSize();
                jassert(s.rbBlockSize > 0);

                s.inAccum.assign((size_t)s.rbBlockSize, 0.0f);
                s.inAccumSize = 0;

                s.shiftIn .assign((size_t)s.rbBlockSize, 0.0f);
                s.shiftOut.assign((size_t)s.rbBlockSize, 0.0f);

                // outRing 容量：为最坏情况预留几倍 blockSize + 一次 DAW block 的余量
                const int cap = s.rbBlockSize * 4 + maxBlockSizePrepared * 2 + 32;
                s.outRing.assign((size_t)cap, 0.0f);
                s.outHead = 0;
                s.outTail = 0;

                for (auto& f : s.highPassFilters) f.reset();
                for (auto& f : s.lowPassFilters)  f.reset();
                s.appliedLowCutHz = -1.0f;
                s.appliedHighCutHz = -1.0f;
                updateBandFilters(b, ch, semitone, centerOffset, width);

                // 用脉冲探测估算该 band 的 shifter 延迟。
                const int lat = estimateShifterLatencySamples(*s.shifter, s.rbBlockSize);
                bandLatencySamples[(size_t)b] = juce::jmax(bandLatencySamples[(size_t)b], lat);

                // 探测会推进内部状态，重置回实时处理初始态（保留当前 pitch ratio）。
                s.shifter->reset();

                s.alignDelaySamples = 0;
                s.alignRing.clear();
                s.alignHead = 0;
                s.alignTail = 0;
            }
            else
            {
                // 禁用状态：释放 shifter 和相关缓冲区以节省资源
                s.shifter.reset();
                s.rbBlockSize = 0;
                s.inAccum.clear();
                s.inAccumSize = 0;
                s.shiftIn.clear();
                s.shiftOut.clear();
                s.outRing.clear();
                s.outHead = 0;
                s.outTail = 0;
                s.alignDelaySamples = 0;
                s.alignRing.clear();
                s.alignHead = 0;
                s.alignTail = 0;
                for (auto& f : s.highPassFilters) f.reset();
                for (auto& f : s.lowPassFilters)  f.reset();
                s.appliedLowCutHz = -1.0f;
                s.appliedHighCutHz = -1.0f;
            }
        }
    }

    // 以所有启用 band 的最大延迟作为统一时间基准，给每路补齐内部对齐延迟。
    int maxBandLatency = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (!bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            continue;
        maxBandLatency = juce::jmax(maxBandLatency, bandLatencySamples[(size_t)b]);
    }

    for (int b = 0; b < kNumBands; ++b)
    {
        if (!bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            continue;

        const int alignDelay = juce::jmax(0, maxBandLatency - bandLatencySamples[(size_t)b]);
        bandAlignDelaySamples[(size_t)b] = alignDelay;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& s = state[(size_t)b][(size_t)ch];
            s.alignDelaySamples = alignDelay;

            const int cap = juce::jmax(alignDelay + maxBlockSizePrepared * 2 + 32, 64);
            s.alignRing.assign((size_t)cap, 0.0f);
            primeDelayLine(s.alignRing, s.alignHead, s.alignTail, alignDelay);
        }
    }

    reportedLatency = maxBandLatency;

    appliedFilterCenterOffsetSt = centerOffset;
    appliedFilterWidthSt = width;
    appliedPitchQualityMode = quality;
    appliedFormantMode = formant;
}

PitchShiftEngine::DelayBreakdown PitchShiftEngine::getDelayBreakdown() const noexcept
{
    DelayBreakdown out;
    out.shifterLatencySamples = bandLatencySamples;
    out.internalAlignDelaySamples = bandAlignDelaySamples;
    out.reportedToHostSamples = reportedLatency;
    return out;
}

void PitchShiftEngine::reset()
{
    for (int b = 0; b < kNumBands; ++b)
    {
        // 【优化】只 reset 启用的 band
        if (!bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            continue;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& s = state[(size_t)b][(size_t)ch];
            if (s.shifter) s.shifter->reset();

            std::fill(s.inAccum.begin(), s.inAccum.end(), 0.0f);
            s.inAccumSize = 0;

            std::fill(s.outRing.begin(), s.outRing.end(), 0.0f);
            s.outHead = 0;
            s.outTail = 0;

            primeDelayLine(s.alignRing, s.alignHead, s.alignTail, s.alignDelaySamples);

            for (auto& f : s.highPassFilters) f.reset();
            for (auto& f : s.lowPassFilters)  f.reset();
        }
    }
}

void PitchShiftEngine::setDotGain(int index, float gain)
{
    if (index < 0 || index >= kNumBands) return;
    dotGains[(size_t)index].store(juce::jlimit(0.0f, 1.0f, gain), std::memory_order_relaxed);
}

void PitchShiftEngine::setDotPan(int index, float pan)
{
    if (index < 0 || index >= kNumBands) return;
    dotPans[(size_t)index].store(juce::jlimit(-1.0f, 1.0f, pan), std::memory_order_relaxed);
}

void PitchShiftEngine::setDotSemitoneOffset(int index, int semitone)
{
    if (index < 0 || index >= kNumBands) return;

    const int st = juce::jlimit(-36, +36, semitone);
    dotSemitones[(size_t) index].store(st, std::memory_order_relaxed);
}

void PitchShiftEngine::setBandEnabled(int band, bool enabled)
{
    if (band < 0 || band >= kNumBands) return;
    
    // 【优化】只在状态真正改变时才更新（避免不必要的 prepare 调用）
    bool current = bandEnabled[(size_t)band].load(std::memory_order_relaxed);
    if (current != enabled)
    {
        bandEnabled[(size_t)band].store(enabled, std::memory_order_relaxed);
        // 注意：实际释放/创建 shifter 需要在下次 prepare() 时进行
        // 这里只设置标志位，由调用者在适当时机调用 prepare()
    }
}

bool PitchShiftEngine::getBandEnabled(int band) const
{
    if (band < 0 || band >= kNumBands) return false;
    return bandEnabled[(size_t)band].load(std::memory_order_relaxed);
}

// 确保 outRing 至少能再写入 extra 个样本（靠前面 shift 腾出空间）
void PitchShiftEngine::ensureOutRingSpace(BandChannel& b, int extra)
{
    const int cap = (int)b.outRing.size();
    if (b.outTail + extra <= cap)
        return;

    // 向前 shift 已消耗部分
    const int valid = b.outTail - b.outHead;
    if (valid > 0 && b.outHead > 0)
        std::memmove(b.outRing.data(),
                     b.outRing.data() + b.outHead,
                     (size_t)valid * sizeof(float));
    b.outTail -= b.outHead;
    b.outHead = 0;

    // 若还是不够（极端情况），扩容（在 audio 线程应极少发生，但为稳健起见处理）
    if (b.outTail + extra > cap)
    {
        const int newCap = juce::jmax(cap * 2, b.outTail + extra + 32);
        b.outRing.resize((size_t)newCap, 0.0f);
    }
}

void PitchShiftEngine::ensureDelayRingSpace(BandChannel& b, int extra)
{
    const int cap = (int)b.alignRing.size();
    if (cap <= 0)
        return;
    if (b.alignTail + extra <= cap)
        return;

    const int valid = b.alignTail - b.alignHead;
    if (valid > 0 && b.alignHead > 0)
        std::memmove(b.alignRing.data(),
                     b.alignRing.data() + b.alignHead,
                     (size_t)valid * sizeof(float));
    b.alignTail -= b.alignHead;
    b.alignHead = 0;

    if (b.alignTail + extra > cap)
    {
        const int newCap = juce::jmax(cap * 2, b.alignTail + extra + 32);
        b.alignRing.resize((size_t)newCap, 0.0f);
    }
}

void PitchShiftEngine::applyAlignmentDelay(BandChannel& b, float* samples, int n)
{
    if (samples == nullptr || n <= 0 || b.alignDelaySamples <= 0)
        return;

    ensureDelayRingSpace(b, n);

    std::memcpy(b.alignRing.data() + b.alignTail,
                samples,
                (size_t)n * sizeof(float));
    b.alignTail += n;

    const int avail = b.alignTail - b.alignHead;
    const int take = juce::jmin(avail, n);
    if (take > 0)
    {
        std::memcpy(samples,
                    b.alignRing.data() + b.alignHead,
                    (size_t)take * sizeof(float));
        b.alignHead += take;
    }
    if (take < n)
        std::memset(samples + take, 0, (size_t)(n - take) * sizeof(float));
}

void PitchShiftEngine::pumpInputToBand(int band, int ch, const float* src, int n)
{
    auto& b = state[(size_t)band][(size_t)ch];
    if (!b.shifter || b.rbBlockSize <= 0 || n <= 0) return;

    int consumed = 0;
    while (consumed < n)
    {
        const int toCopy = juce::jmin(n - consumed, b.rbBlockSize - b.inAccumSize);
        std::memcpy(b.inAccum.data() + b.inAccumSize,
                    src + consumed,
                    (size_t)toCopy * sizeof(float));
        b.inAccumSize += toCopy;
        consumed      += toCopy;

        if (b.inAccumSize == b.rbBlockSize)
        {
            // 调用 LiveShifter
            // shift() 的参数是 const float* const*（"每个声道一个数组"）
            std::memcpy(b.shiftIn.data(), b.inAccum.data(),
                        (size_t)b.rbBlockSize * sizeof(float));
            const float* inPtrs [1] = { b.shiftIn.data()  };
            float*       outPtrs[1] = { b.shiftOut.data() };
            b.shifter->shift(inPtrs, outPtrs);

            // 把 shiftOut 追加到 outRing
            ensureOutRingSpace(b, b.rbBlockSize);
            std::memcpy(b.outRing.data() + b.outTail,
                        b.shiftOut.data(),
                        (size_t)b.rbBlockSize * sizeof(float));
            b.outTail += b.rbBlockSize;

            b.inAccumSize = 0;
        }
    }
}

int PitchShiftEngine::drainFromBand(int band, int ch, float* dst, int n)
{
    auto& b = state[(size_t)band][(size_t)ch];
    const int avail = b.outTail - b.outHead;
    const int take  = juce::jmin(avail, n);
    if (take > 0)
    {
        std::memcpy(dst, b.outRing.data() + b.outHead, (size_t)take * sizeof(float));
        b.outHead += take;
    }
    if (take < n)
        std::memset(dst + take, 0, (size_t)(n - take) * sizeof(float));
    return take;
}

void PitchShiftEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int centerOffset = filterCenterOffsetSt.load(std::memory_order_relaxed);
    const int width = filterWidthSt.load(std::memory_order_relaxed);
    const bool filterConfigChanged = (centerOffset != appliedFilterCenterOffsetSt)
                                  || (width != appliedFilterWidthSt);

    if (filterConfigChanged)
    {
        appliedFilterCenterOffsetSt = centerOffset;
        appliedFilterWidthSt = width;
    }

    // 在音频线程应用 UI 更新的半音偏移（避免 UI 线程直接触碰 shifter）
    for (int b = 0; b < kNumBands; ++b)
    {
        const int st = dotSemitones[(size_t)b].load(std::memory_order_relaxed);
        const bool semitoneChanged = (st != appliedSemitones[(size_t)b]);

        if (semitoneChanged)
        {
            const double ratio = std::pow(2.0, (double)st / 12.0);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = state[(size_t)b][(size_t)ch];
                if (s.shifter)
                    s.shifter->setPitchScale(ratio);
            }
            appliedSemitones[(size_t)b] = st;
        }

        if (semitoneChanged || filterConfigChanged)
        {
            for (int ch = 0; ch < 2; ++ch)
                updateBandFilters(b, ch, st, centerOffset, width);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jlimit(1, 2, buffer.getNumChannels());
    if (numSamples <= 0) return;

    juce::AudioBuffer<float> inputCopy;
    inputCopy.makeCopyOf(buffer, true);

    activeBandsCount = 0; // 重置活动band计数器

    // 【优化】只处理 gain > 0 的 band（gain=0 时完全跳过，包括输入处理）
    // 这样当 UI 上把圆点拖到最下端时，CPU 会自动下降
    for (int b = 0; b < kNumBands; ++b)
    {
        // 检查 bandEnabled 标志
        if (!bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            continue;

        // 【关键优化】检查 gain 是否为 0，如果是则完全跳过该 band 的所有处理
        const float gain = dotGains[(size_t)b].load(std::memory_order_relaxed);
        if (gain <= 1.0e-6f)
            continue;

        // 统计活动band数量（用于调试显示）
        activeBandsCount++;

        for (int ch = 0; ch < numCh; ++ch)
        {
            // 额外检查：确保该 band/ch 的 shifter 已创建
            if (state[(size_t)b][(size_t)ch].shifter)
                pumpInputToBand(b, ch, inputCopy.getReadPointer(ch), numSamples);
        }
    }

    static thread_local std::vector<float> accL, accR, tmpL, tmpR;
    if ((int)accL.size() < numSamples) accL.assign((size_t)numSamples, 0.0f);
    else std::fill(accL.begin(), accL.begin() + numSamples, 0.0f);
    if ((int)accR.size() < numSamples) accR.assign((size_t)numSamples, 0.0f);
    else std::fill(accR.begin(), accR.begin() + numSamples, 0.0f);
    if ((int)tmpL.size() < numSamples) tmpL.resize((size_t)numSamples);
    if ((int)tmpR.size() < numSamples) tmpR.resize((size_t)numSamples);

    for (int b = 0; b < kNumBands; ++b)
    {
        if (!bandEnabled[(size_t)b].load(std::memory_order_relaxed))
            continue;

        // 【优化】额外检查：确保该 band 的 shifter 存在（防止 prepare 时未创建）
        bool shifterExists = false;
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (state[(size_t)b][(size_t)ch].shifter)
            {
                shifterExists = true;
                break;
            }
        }
        if (!shifterExists)
            continue;

        // 理论上前面输入阶段已过滤 gain=0，这里保留一次防御性检查即可
        const float gain = dotGains[(size_t)b].load(std::memory_order_relaxed);
        if (gain <= 1.0e-6f) continue;

        const float pan  = juce::jlimit(-1.0f, 1.0f,
                                        dotPans[(size_t)b].load(std::memory_order_relaxed));
        drainFromBand(b, 0, tmpL.data(), numSamples);
        if (numCh >= 2)
        {
            drainFromBand(b, 1, tmpR.data(), numSamples);
            processBandFilters(b, 0, tmpL.data(), numSamples);
            processBandFilters(b, 1, tmpR.data(), numSamples);
            applyAlignmentDelay(state[(size_t)b][0], tmpL.data(), numSamples);
            applyAlignmentDelay(state[(size_t)b][1], tmpR.data(), numSamples);
        }
        else
        {
            processBandFilters(b, 0, tmpL.data(), numSamples);
            applyAlignmentDelay(state[(size_t)b][0], tmpL.data(), numSamples);
            std::memcpy(tmpR.data(), tmpL.data(), (size_t)numSamples * sizeof(float));
        }

        // 声道间能量转移系数
        //   outL = aLL * L + aRL * R
        //   outR = aLR * L + aRR * R
        float aLL, aRL, aLR, aRR;
        if (pan >= 0.0f)
        {
            const float p = pan;             // 向右搬
            aLL = 1.0f - p;  aRL = 0.0f;
            aLR = p;         aRR = 1.0f;
        }
        else
        {
            const float p = -pan;            // 向左搬
            aLL = 1.0f;      aRL = p;
            aLR = 0.0f;      aRR = 1.0f - p;
        }

        const float kLL = gain * aLL;
        const float kRL = gain * aRL;
        const float kLR = gain * aLR;
        const float kRR = gain * aRR;

        const float* pL = tmpL.data();
        const float* pR = tmpR.data();
        for (int i = 0; i < numSamples; ++i)
        {
            const float L = pL[i];
            const float R = pR[i];
            accL[(size_t)i] += L * kLL + R * kRL;
            accR[(size_t)i] += L * kLR + R * kRR;
        }
    }

    // ===== 3. 硬削波并写回 buffer =====
    auto hardClip = [](float x) -> float
    {
        return juce::jlimit(-1.0f, 1.0f, x);
    };

    const int outChannels = buffer.getNumChannels();
    if (outChannels >= 2)
    {
        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = hardClip(accL[(size_t)i]);
            outR[i] = hardClip(accR[(size_t)i]);
        }
        for (int ch = 2; ch < outChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }
    else if (outChannels == 1)
    {
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            out[i] = hardClip(0.5f * (accL[(size_t)i] + accR[(size_t)i]));
    }
}