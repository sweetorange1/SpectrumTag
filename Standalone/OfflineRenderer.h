#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include <atomic>

// ============================================================================
//  OfflineRenderer —— 离线 STFT/OLA 印章合成器
// ----------------------------------------------------------------------------
//  与 [PluginProcessor.cpp] 的 STFT/OLA 音频处理数学完全一致，但简化为
//  "整段离线处理"模式：不需要 dry/wet crossfade、不需要 automation trigger、
//  不需要 mask 冻结/预热等实时相关的复杂控制。
//
//  输入：
//   - 已解码到内存的输入音频 AudioBuffer（float，单/多声道均可）
//   - 采样率
//   - Mask（rows = fftSize/2+1；cols = 图片在时间轴上占用的列数）
//   - Mask 覆盖的音频时间区间 [startSec, endSec]（对应用户在频谱上拖动
//     图片框水平位置得到的时间范围）
//   - Mask 覆盖的归一化频率范围 [freqLowNorm, freqHighNorm]（∈[0,1]，1=Nyquist）
//   - FFT Size / amplitudeRatio / invert 三个用户参数
//
//  输出：
//   - 与输入同 shape 的 AudioBuffer；时间上：
//     * [0, startSec) 完全 bypass
//     * [startSec, endSec] 应用 STFT/OLA mask
//     * (endSec, end] 完全 bypass
//
//  设计要点：
//   - 整段共用一个 STFT/OLA 环形状态，切换到 mask 段时不重置状态，
//     只是从"unity 增益"平滑过渡到"mask 增益"（用平滑系数即可）
//   - Speed 仅影响 UI 显示，实际时长由用户拖出来的时间区间决定
//   - 端点做短 crossfade（10ms 左右），避免起始/结束的窗口硬切
// ============================================================================

struct OfflineRenderParams
{
    int   fftSize        = 4096;    // 1024/2048/4096/8192
    float amplitudeRatio = 0.0f;    // [0.0, 1.5]
    bool  invert         = false;

    // mask 覆盖的音频时间区间（秒），必须满足 0 <= startSec < endSec <= totalDurationSec
    double startSec = 0.0;
    double endSec   = 0.0;

    // mask 覆盖的归一化频率范围（0=DC, 1=Nyquist）
    float freqLowNorm  = 0.0f;
    float freqHighNorm = 1.0f;

    // mask 网格：rows 必须 = fftSize/2+1；cols 至少 4，最多 4096
    int   maskRows = 0;
    int   maskCols = 0;
};

class OfflineRenderer
{
public:
    OfflineRenderer() = default;

    // 进度回调，[0, 1]，从工作线程调用；若返回 false 表示取消
    using ProgressCallback = std::function<bool (float progress)>;

    // 阻塞式渲染（应在后台线程调用）。返回 true 表示完成，false 表示被取消或参数无效。
    // input/output 尺寸/通道数必须一致。output 可以与 input 相同的 AudioBuffer。
    bool render (const juce::AudioBuffer<float>& input,
                 juce::AudioBuffer<float>&       output,
                 double sampleRate,
                 const OfflineRenderParams&      params,
                 const std::vector<float>&       mask,
                 ProgressCallback                progress);
};

// ----------------------------------------------------------------------------
//  辅助：把图片框归一化坐标 + Content 宽高比 反推 mask cols 数
//   与插件 UI 的规则一致：cols 直接 = 图片框在频谱上的像素宽度
//   （clamp 到 [4, 4096]）。因为 Standalone 端 SpectrumView 会知道自己的
//   实际像素宽度，所以由 UI 端调用时直接传入即可，这里放个纯函数便于复用。
// ----------------------------------------------------------------------------
inline int computeMaskCols (float imgBoxPixelWidth)
{
    return juce::jmax (4, juce::jmin (4096, juce::roundToInt (imgBoxPixelWidth)));
}
