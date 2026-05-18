#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// 前向声明，避免在头文件里引入 Rubber Band 头（保持编译隔离）
namespace RubberBand { class RubberBandLiveShifter; }

/**
 * PitchShiftEngine
 * ----------------
 * 与界面解耦的音频处理引擎。
 *
 * 变调算法：Rubber Band Library 的 RubberBandLiveShifter（R3 引擎的实时低延迟版本）。
 *          这是业界级的变调不变速实现，质量上对应 Rubber Band "Finer" 引擎，
 *          内部使用相位声码器 + 瞬态保留 + 峰值追踪等技术，显著优于朴素 PV。
 *
 * 五路 band（对应 UI 的 5 个圆点，从左到右）：
 *   -24 半音 / -12 半音 / 原信号 / +12 半音 / +24 半音
 *
 * 每一路信号：
 *   1. 独立经过 RubberBandLiveShifter 做 pitch shift
 *   2. 乘以该路的 gain（圆点高度决定）
 *   3. 按该路 pan（圆点颜色决定，-1 = 纯红全左，+1 = 纯蓝全右，0 = 白不处理）
 *      分配到 L/R
 * 5 路求和 → 硬削波 → 输出
 *
 * 由于 LiveShifter 的 shift() 必须按"固定 blockSize"调用，而 DAW 传入的
 * numSamples 不固定，这里用输入/输出环形缓冲适配任意块大小。
 *
 * LiveShifter 的启动延迟（getStartDelay()）通过 PluginProcessor 调用
 * setLatencySamples() 上报给 DAW，由 DAW 做延迟补偿；每路 band 的延迟相同，
 * 因此 5 路在输出端天然对齐，无需额外处理。
 *
 * 线程安全：
 *   UI 线程调用 setDotGain / setDotPan（lock-free atomic）。
 *   Audio 线程调用 prepare / process / reset（单线程）。
 */
class PitchShiftEngine
{
public:
    static constexpr int kNumBands = 5;
    // 5 路默认半音偏移（从左到右）
    static constexpr std::array<int, kNumBands> kDefaultSemitones { -24, -12, 0, +12, +24 };

    PitchShiftEngine();
    ~PitchShiftEngine();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

    // === UI → Audio 参数更新（lock-free） ===
    void setDotGain(int index, float gain);
    void setDotPan (int index, float pan);
    void setDotSemitoneOffset(int index, int semitone);

    // 启用/禁用每个band
    void setBandEnabled(int band, bool enabled);
    bool getBandEnabled(int band) const;

    // 获取当前活动的band数量（用于调试：gain > 0 的 band 数量）
    int getActiveBandsCount() const noexcept { return activeBandsCount.load(std::memory_order_relaxed); }

    // 供 PluginProcessor 上报给 DAW 做延迟补偿
    int  getLatencySamples() const noexcept { return reportedLatency; }

private:
    double sampleRate           = 44100.0;
    int    numChannelsPrepared  = 2;
    int    maxBlockSizePrepared = 512;
    int    reportedLatency      = 0;

    // 每路 band × 每声道一个独立的 LiveShifter（单声道实例）
    // 使用 unique_ptr 是因为 RubberBandLiveShifter 的构造必须给出 sampleRate/channels，
    // 所以只能在 prepare() 中延迟创建
    struct BandChannel
    {
        std::unique_ptr<RubberBand::RubberBandLiveShifter> shifter;

        int                rbBlockSize = 0;
        // 输入积累缓冲：凑够 rbBlockSize 就调一次 shift()
        std::vector<float> inAccum;
        int                inAccumSize = 0;
        // 单次 shift() 的输入/输出临时缓冲（长度 = rbBlockSize）
        std::vector<float> shiftIn;
        std::vector<float> shiftOut;
        // 输出环：已经 shift 出来、等待被 DAW 读走的样本
        std::vector<float> outRing;
        int                outHead = 0; // 待读起点
        int                outTail = 0; // 已写末端（exclusive），outTail >= outHead
    };

    std::array<std::array<BandChannel, 2>, kNumBands> state;

    // UI 参数
    std::array<std::atomic<float>, kNumBands> dotGains;
    std::array<std::atomic<float>, kNumBands> dotPans;
    std::array<std::atomic<int>,   kNumBands> dotSemitones;

    // 仅在音频线程读写：记录每个 band 已应用到 shifter 的半音值
    std::array<int, kNumBands> appliedSemitones { -24, -12, 0, +12, +24 };

    // 启用/禁用每个band
    std::array<std::atomic<bool>, kNumBands> bandEnabled;

    // 当前活动的band数量（gain > 0 的band数量，用于调试显示）
    mutable std::atomic<int> activeBandsCount { 0 };

    // 辅助：
    // 把 n 个输入样本喂入某 band/ch，内部按 rbBlockSize 调用 shift()，
    // 产出的样本追加到 outRing
    void pumpInputToBand(int band, int ch, const float* src, int n);
    // 从 outRing 消耗最多 n 个样本到 dst，返回实际取到的样本数
    // 不足 n 时剩余位置保持为 0（由调用方清零）
    int  drainFromBand(int band, int ch, float* dst, int n);
    // outRing 空间管理：保证能再写入 extra 个样本，必要时向前 shift
    void ensureOutRingSpace(BandChannel& b, int extra);
};
