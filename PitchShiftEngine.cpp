#include "PitchShiftEngine.h"
#include <rubberband/RubberBandLiveShifter.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using RubberBand::RubberBandLiveShifter;

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
    const int rbOptions =
          RubberBandLiveShifter::OptionWindowShort
        | RubberBandLiveShifter::OptionFormantShifted
        | RubberBandLiveShifter::OptionChannelsApart;

    reportedLatency = 0;

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

                // 记录延迟（五路相同，取任一即可；保守起见取最大）
                // 注意：总延迟 = RubberBand内部延迟 + 输入缓冲延迟(rbBlockSize)
                // 输入需要累积 rbBlockSize 个样本才能调用 shift()，这部分延迟必须报告给宿主
                const int lat = (int)s.shifter->getStartDelay() + s.rbBlockSize;
                reportedLatency = juce::jmax(reportedLatency, lat);
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
            }
        }
    }
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
    // 在音频线程应用 UI 更新的半音偏移（避免 UI 线程直接触碰 shifter）
    for (int b = 0; b < kNumBands; ++b)
    {
        const int st = dotSemitones[(size_t)b].load(std::memory_order_relaxed);
        if (st != appliedSemitones[(size_t)b])
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
            drainFromBand(b, 1, tmpR.data(), numSamples);
        else
            std::memcpy(tmpR.data(), tmpL.data(), (size_t)numSamples * sizeof(float));

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