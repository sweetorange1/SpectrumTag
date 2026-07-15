#include "OfflineRenderer.h"
#include <cmath>
#include <memory>

namespace
{
    // 与插件 PluginProcessor::ratioToGain 完全一致
    inline float ratioToGain (float ratio)
    {
        if (ratio <= 0.0f) return 0.0f;
        return ratio;
    }

    // 单通道 STFT/OLA 状态
    struct StftChannel
    {
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window;         // 长度 N
        std::vector<float> fftWork;        // 长度 2N
        std::vector<float> inputRing;      // 长度 N
        std::vector<float> outputRing;     // 长度 N
        std::vector<float> olaNormRing;    // 长度 N
        std::vector<float> smoothedGains;  // 长度 N/2+1
        std::vector<float> outFifo;        // 长度 2N
        int inputPos = 0;
        int accumCount = 0;
        int frameCount = 0;
        int outFifoWrite = 0;
        int outFifoRead  = 0;
        int outFifoCount = 0;
    };

    void resetChannel (StftChannel& st, int N, int numBins)
    {
        std::fill (st.inputRing.begin(),     st.inputRing.end(),     0.0f);
        std::fill (st.outputRing.begin(),    st.outputRing.end(),    0.0f);
        std::fill (st.olaNormRing.begin(),   st.olaNormRing.end(),   0.0f);
        std::fill (st.smoothedGains.begin(), st.smoothedGains.end(), 1.0f);
        std::fill (st.outFifo.begin(),       st.outFifo.end(),       0.0f);
        juce::ignoreUnused (numBins);
        st.inputPos     = 0;
        st.accumCount   = 0;
        st.frameCount   = 0;
        st.outFifoWrite = 0;
        st.outFifoRead  = 0;
        st.outFifoCount = 0;
    }

    // 与 PluginProcessor::processStftFrame 数学一致：加窗 → FFT → 逐 bin 平滑增益 →
    // IFFT → WOLA 归一化。产生 hop 个稳态样本推入 outFifo。
    void processFrame (StftChannel& st, int N, int hop,
                       const std::vector<float>& targetBinGains,
                       float gainSmoothAlpha)
    {
        const int numBins = N / 2 + 1;
        const int frameStart = st.inputPos;

        for (int n = 0; n < N; ++n)
        {
            const int idx = (frameStart + n) % N;
            st.fftWork[(size_t) n] = st.inputRing[(size_t) idx] * st.window[(size_t) n];
        }
        std::fill (st.fftWork.begin() + N, st.fftWork.end(), 0.0f);

        st.fft->performRealOnlyForwardTransform (st.fftWork.data());

        // k = 0 (DC)
        {
            const float target = targetBinGains[0];
            float& smooth = st.smoothedGains[0];
            smooth += (target - smooth) * gainSmoothAlpha;
            st.fftWork[0] *= smooth;
        }
        for (int k = 1; k < numBins - 1; ++k)
        {
            const float target = targetBinGains[(size_t) k];
            float&      smooth = st.smoothedGains[(size_t) k];
            smooth += (target - smooth) * gainSmoothAlpha;
            st.fftWork[(size_t) (2 * k)]     *= smooth;
            st.fftWork[(size_t) (2 * k + 1)] *= smooth;
        }
        {
            const int kNyq = numBins - 1;
            const float target = targetBinGains[(size_t) kNyq];
            float& smooth = st.smoothedGains[(size_t) kNyq];
            smooth += (target - smooth) * gainSmoothAlpha;
            st.fftWork[1] *= smooth;
        }

        st.fft->performRealOnlyInverseTransform (st.fftWork.data());

        for (int n = 0; n < N; ++n)
        {
            const int outIdx = (frameStart + n) % N;
            const float w = st.window[(size_t) n];
            st.outputRing[(size_t) outIdx]  += st.fftWork[(size_t) n] * w;
            st.olaNormRing[(size_t) outIdx] += w * w;
        }

        ++st.frameCount;
        if (st.frameCount >= N / hop)
        {
            const int fifoStart = frameStart;
            constexpr float kNormEps = 1.0e-8f;
            for (int i = 0; i < hop; ++i)
            {
                const int pos = (fifoStart + i) % N;
                const float norm = st.olaNormRing[(size_t) pos];
                const float y = (norm > kNormEps)
                    ? (st.outputRing[(size_t) pos] / norm)
                    : st.outputRing[(size_t) pos];
                st.outFifo[(size_t) st.outFifoWrite] = y;
                st.outputRing[(size_t) pos]  = 0.0f;
                st.olaNormRing[(size_t) pos] = 0.0f;
                st.outFifoWrite = (st.outFifoWrite + 1) % (int) st.outFifo.size();
                ++st.outFifoCount;
            }
        }
    }

    // 计算某一 mask 列 col 对应的 numBins 个 bin 的目标增益（与插件同一套逻辑）
    void computeBinGainsForCol (int col,
                                const std::vector<float>& mask,
                                int maskRows, int maskCols,
                                int numBins, int N,
                                float sampleRate,
                                float freqLowNorm, float freqHighNorm,
                                float ratioGain, bool invert,
                                std::vector<float>& out)
    {
        const float maxHz = sampleRate * 0.5f;
        const float fLow  = freqLowNorm  * maxHz;
        const float fHigh = freqHighNorm * maxHz;
        const float denom = juce::jmax (1.0e-6f, fHigh - fLow);

        // 先扫 mask 该列的 active row 范围（用于保护带）
        int activeRowMin = maskRows;
        int activeRowMax = -1;
        for (int row = 0; row < maskRows; ++row)
        {
            const float m = mask[(size_t) (row * maskCols + col)];
            if (m > 0.5f)
            {
                activeRowMin = juce::jmin (activeRowMin, row);
                activeRowMax = juce::jmax (activeRowMax, row);
            }
        }
        const bool hasActiveRows = (activeRowMax >= activeRowMin);
        const float binHz = sampleRate / (float) juce::jmax (1, N);

        float activeLowHz  = fLow;
        float activeHighHz = fHigh;
        if (hasActiveRows && maskRows > 1)
        {
            const float rowToNorm = 1.0f / (float) (maskRows - 1);
            activeLowHz  = fLow + ((float) activeRowMin * rowToNorm) * denom;
            activeHighHz = fLow + ((float) activeRowMax * rowToNorm) * denom;
        }
        const float protectPadHz  = juce::jmax (2.0f * binHz, 0.005f * denom);
        const float edgeSlopeHz   = juce::jmax (1.5f * binHz, 0.002f * denom);
        const float protectLowHz  = activeLowHz  - protectPadHz;
        const float protectHighHz = activeHighHz + protectPadHz;

        for (int k = 0; k < numBins; ++k)
        {
            const float hz = (float) k / (float) (N / 2) * maxHz;
            if (hz < fLow || hz > fHigh)
            {
                out[(size_t) k] = 1.0f;
                continue;
            }
            const float norm = (hz - fLow) / denom;
            const int row = juce::jlimit (0, maskRows - 1,
                                          (int) std::floor (norm * (maskRows - 1)));
            const float m = mask[(size_t) (row * maskCols + col)];
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
            out[(size_t) k] = g;
        }

        // 频域轻度平滑（与插件一致）
        std::vector<float> filtered = out;
        for (int k = 1; k < numBins - 1; ++k)
        {
            const float hz = (float) k / (float) (N / 2) * maxHz;
            if (hz < fLow || hz > fHigh) continue;
            filtered[(size_t) k] = 0.2f * out[(size_t) (k - 1)]
                                 + 0.6f * out[(size_t) k]
                                 + 0.2f * out[(size_t) (k + 1)];
        }
        out.swap (filtered);
    }
}

// ============================================================================
bool OfflineRenderer::render (const juce::AudioBuffer<float>& input,
                              juce::AudioBuffer<float>&       output,
                              double sampleRate,
                              const OfflineRenderParams&      params,
                              const std::vector<float>&       mask,
                              ProgressCallback                progress)
{
    const int numCh = input.getNumChannels();
    const int numSamps = input.getNumSamples();
    if (numCh <= 0 || numSamps <= 0 || sampleRate <= 0.0) return false;
    if (params.fftSize < 64 || params.fftSize > 32768) return false;
    if (params.maskRows <= 0 || params.maskCols <= 0) return false;
    if ((int) mask.size() != params.maskRows * params.maskCols) return false;

    // 确保 output 与 input 尺寸一致；先把 input 完整复制过来（bypass 段自动就绪）
    output.setSize (numCh, numSamps, false, true, true);
    for (int ch = 0; ch < numCh; ++ch)
        output.copyFrom (ch, 0, input, ch, 0, numSamps);

    const int N   = params.fftSize;
    const int hop = N / 4;
    const int numBins = N / 2 + 1;

    // mask 在时间轴上的样本区间（clamp 到 [0, numSamps]）
    const int64_t startSample = (int64_t) juce::jlimit<double> (
        0.0, (double) numSamps, params.startSec * sampleRate);
    const int64_t endSample   = (int64_t) juce::jlimit<double> (
        (double) startSample, (double) numSamps, params.endSec * sampleRate);
    if (endSample <= startSample) return false;

    // ---- 初始化每通道 STFT 状态 ----
    std::vector<StftChannel> channels ((size_t) numCh);
    const int order = (int) std::round (std::log2 ((double) N));

    // Hann 窗
    std::vector<float> hann ((size_t) N);
    for (int n = 0; n < N; ++n)
        hann[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) n / (float) (N - 1)));

    for (auto& st : channels)
    {
        st.fft = std::make_unique<juce::dsp::FFT> (order);
        st.window = hann;
        st.fftWork.assign ((size_t) (2 * N), 0.0f);
        st.inputRing.assign ((size_t) N, 0.0f);
        st.outputRing.assign ((size_t) N, 0.0f);
        st.olaNormRing.assign ((size_t) N, 0.0f);
        st.smoothedGains.assign ((size_t) numBins, 1.0f);
        st.outFifo.assign ((size_t) (2 * N), 0.0f);
        st.inputPos = 0;
        st.accumCount = 0;
        st.frameCount = 0;
        st.outFifoWrite = 0;
        st.outFifoRead = 0;
        st.outFifoCount = 0;
    }

    // 帧到帧的时间步长（与插件一致：一帧 hop 样本）
    const float frameSeconds = (float) hop / (float) sampleRate;
    const float smoothAlpha  = 1.0f - std::exp (- frameSeconds / 0.010f);

    // 逐 bin 目标增益缓冲（unity 与 mask 两个模板；不预先构建每列增益，逐帧按需生成）
    std::vector<float> unityGains ((size_t) numBins, 1.0f);
    std::vector<float> maskGains  ((size_t) numBins, 1.0f);

    const float ratioGain = ratioToGain (params.amplitudeRatio);
    const bool  invert    = params.invert;

    // 处理循环：与插件类似的 sample-major 循环。
    //  - 每样本先把 input 写入 inputRing；每 hop 样本触发一次 STFT 帧处理；
    //  - 处理完的 STFT 输出（hop 个稳态样本）从 outFifo 取出 → wet；
    //  - 是否把 wet 覆盖到 output 由 [startSample, endSample] 决定。
    //  - 端点做短 crossfade（20ms），避免硬切造成的边界爆音。
    const int64_t crossfadeSamples = juce::jmax<int64_t> (
        (int64_t) 64,
        (int64_t) std::round (0.020 * sampleRate));   // 20ms
    const int64_t fadeInEnd    = juce::jmin<int64_t> (
        startSample + crossfadeSamples, endSample);
    const int64_t fadeOutStart = juce::jmax<int64_t> (
        endSample - crossfadeSamples, fadeInEnd);

    // OLA 输出的整体延迟 = N - hop（与插件一致）
    // 所以要读回"当前样本 n 对应的 STFT 输出"，实际上 outFifo 里存的是延迟 N-hop 的样本。
    // 简化处理：我们把 wet 写回 output 时按"当前样本位置"对应写入，同时对 dry 侧做同等延迟。
    // 具体做法：为每通道保留 dryDelay ring（长 N-hop），先写入再读出，与 outFifo 严格对齐。
    const int dryDelaySamples = juce::jlimit (0, N - 1, N - hop);
    std::vector<std::vector<float>> dryDelayRing ((size_t) numCh,
        std::vector<float> ((size_t) juce::jmax (1, dryDelaySamples), 0.0f));
    std::vector<int> dryDelayWritePos ((size_t) numCh, 0);

    const int64_t progressReportEvery = juce::jmax<int64_t> (2048, numSamps / 100);
    int64_t nextProgressReport = progressReportEvery;

    for (int64_t n = 0; n < numSamps; ++n)
    {
        // 1) 写入 inputRing / dryDelay
        bool frameReady = false;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& st = channels[(size_t) ch];
            const float inSample = input.getReadPointer (ch)[n];
            st.inputRing[(size_t) st.inputPos] = inSample;
            st.inputPos = (st.inputPos + 1) % N;
            ++st.accumCount;
            if (st.accumCount >= hop) frameReady = true;

            if (dryDelaySamples > 0)
            {
                auto& ring = dryDelayRing[(size_t) ch];
                int& pos = dryDelayWritePos[(size_t) ch];
                ring[(size_t) pos] = inSample;
                pos = (pos + 1) % (int) ring.size();
            }
        }

        // 2) 帧就绪 → 计算目标增益并处理一帧（每通道）
        if (frameReady)
        {
            // mask 列由"当前样本落在 [startSample, endSample) 的相对位置"决定
            std::vector<float>* gainsPtr = &unityGains;
            if (n >= startSample && n < endSample)
            {
                const double rel = (double) (n - startSample)
                                 / juce::jmax<double> (1.0, (double) (endSample - startSample));
                const int col = juce::jlimit (0, params.maskCols - 1,
                                              (int) std::floor (rel * params.maskCols));
                computeBinGainsForCol (col, mask, params.maskRows, params.maskCols,
                                       numBins, N, (float) sampleRate,
                                       params.freqLowNorm, params.freqHighNorm,
                                       ratioGain, invert, maskGains);
                gainsPtr = &maskGains;
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& st = channels[(size_t) ch];
                st.accumCount -= hop;
                processFrame (st, N, hop, *gainsPtr, smoothAlpha);
            }
        }

        // 3) 从 outFifo 取出 wet；若 FIFO 空则用延迟 dry 兜底
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& st = channels[(size_t) ch];

            float delayedDry;
            if (dryDelaySamples > 0)
            {
                auto& ring = dryDelayRing[(size_t) ch];
                const int writePos = dryDelayWritePos[(size_t) ch];
                const int readPos = (writePos + (int) ring.size() - dryDelaySamples) % (int) ring.size();
                delayedDry = ring[(size_t) readPos];
            }
            else
            {
                delayedDry = input.getReadPointer (ch)[n];
            }

            float wet = delayedDry;
            if (st.outFifoCount > 0)
            {
                wet = st.outFifo[(size_t) st.outFifoRead];
                st.outFifoRead = (st.outFifoRead + 1) % (int) st.outFifo.size();
                --st.outFifoCount;
            }

            // 决定当前样本要输出 dry 还是 wet：
            //  - [0, startSample): 完全 dry（此时也没积攒到有意义的 wet，wet≈dry 亦可）
            //  - [startSample, fadeInEnd): 线性淡入 wet
            //  - [fadeInEnd, fadeOutStart): 完全 wet
            //  - [fadeOutStart, endSample): 线性淡出 wet
            //  - [endSample, end]: 完全 dry
            //
            //  注意：由于 STFT 延迟 = N-hop，实际"该样本 wet 的能量"来自过去若干帧的
            //  累积。为了对齐，dry 侧也做同等延迟（delayedDry），这样两路时间对齐。
            float mixWet;
            if (n < startSample || n >= endSample)
                mixWet = 0.0f;
            else if (n < fadeInEnd)
                mixWet = (float) (n - startSample) / (float) juce::jmax<int64_t> (1, fadeInEnd - startSample);
            else if (n >= fadeOutStart)
                mixWet = (float) (endSample - 1 - n) / (float) juce::jmax<int64_t> (1, endSample - fadeOutStart);
            else
                mixWet = 1.0f;
            mixWet = juce::jlimit (0.0f, 1.0f, mixWet);

            const float out = delayedDry * (1.0f - mixWet) + wet * mixWet;
            output.getWritePointer (ch)[n] = out;
        }

        // 4) 进度回调 / 取消检测
        if (n >= nextProgressReport)
        {
            nextProgressReport += progressReportEvery;
            if (progress)
            {
                const float p = (float) n / (float) juce::jmax<int64_t> (1, numSamps);
                if (! progress (p)) return false;
            }
        }
    }

    if (progress) progress (1.0f);
    return true;
}
