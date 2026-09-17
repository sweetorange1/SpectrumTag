#include "StandaloneWindow.h"
#include <JuceHeader.h>
#include <cmath>
#include <memory>

using namespace SharedColours;

namespace
{
    constexpr int kDefaultWidth  = 1240;
    constexpr int kDefaultHeight = 680;
    constexpr int kMinWidth      = 800;
    constexpr int kMinHeight     = 440;

    constexpr int kFreqAxisWidth = 70;
    constexpr int kPianoWidth    = 28;

    constexpr float kMinDisplayHz = FreqMap::kMinDisplayHz;
    constexpr float kMaxDisplayHz = FreqMap::kMaxDisplayHz;

    // 显示归一化：与插件一致，用窗口能量 √(∑w²) 归一化
    inline float displayMagNormForWindow (const std::vector<float>& w, int N)
    {
        float s = 0.0f;
        for (float v : w) s += v * v;
        return s > 1e-12f ? 1.0f / std::sqrt (s) : 1.0f / (float) juce::jmax (1, N);
    }
}

// ============================================================================
//  StandaloneAudioSpectrogramView
// ============================================================================
StandaloneAudioSpectrogramView::StandaloneAudioSpectrogramView (juce::Typeface::Ptr tf)
    : typeface (std::move (tf))
{
    imageBox = std::make_unique<ImageBoxComponent>();
    imageBox->setTypeface (typeface);
    addAndMakeVisible (*imageBox);
    setInterceptsMouseClicks (true, true);
}

void StandaloneAudioSpectrogramView::resized()
{
    auto r = getLocalBounds();
    axisBounds = r.removeFromLeft (kFreqAxisWidth);
    contentBounds = r;

    if (imageBox != nullptr)
        imageBox->setContentBounds (contentBounds);
}

void StandaloneAudioSpectrogramView::setAudio (const juce::AudioBuffer<float>& audio,
                                                double sampleRate, int fftSize)
{
    currentAudio.makeCopyOf (audio);
    currentSampleRate = sampleRate;
    currentFftSize    = fftSize;
    viewOffsetPx = 0.0f;
    computeSpectrogramImage();
    repaint();
}

void StandaloneAudioSpectrogramView::clearAudio()
{
    currentAudio.setSize (0, 0);
    spectrogram = juce::Image();
    spectrogramNativeWidth = 0;
    spectrogramNativeHeight = 0;
    viewOffsetPx = 0.0f;
    repaint();
}

void StandaloneAudioSpectrogramView::rebuildSpectrogram (int fftSize)
{
    if (currentAudio.getNumSamples() <= 0) return;
    currentFftSize = fftSize;
    computeSpectrogramImage();
    repaint();
}

std::pair<double, double> StandaloneAudioSpectrogramView::getImageBoxTimeRange() const
{
    // 图片框的归一化横向区间对应"当前视图窗口"里的可视比例；
    // 而"当前视图窗口"又是整段时频图（时长 = duration）在 speed 拉伸/偏移下的一段。
    //
    // 换算逻辑：
    //   - viewPixel = 图片框在 contentBounds 内的像素位置
    //   - nativePixel = (viewPixel - viewOffsetPx) / horizontalStretch  (相对于原生时频图)
    //   - timeSec = nativePixel / spectrogramNativeWidth * duration
    if (contentBounds.isEmpty() || spectrogramNativeWidth <= 0)
        return { 0.0, 0.0 };

    const double duration = getAudioDurationSec();
    if (duration <= 0.0) return { 0.0, 0.0 };

    // 图片框在 content 内部的像素起止 X
    const auto norm = imageBox->getNormalisedBounds();
    const float viewX0 = norm.getX() * (float) contentBounds.getWidth();
    const float viewX1 = (norm.getX() + norm.getWidth()) * (float) contentBounds.getWidth();

    // View → Native 反变换
    // 显示时: displayX = viewOffsetPx + nativeX * horizontalStretch
    //   → nativeX = (displayX - viewOffsetPx) / horizontalStretch
    // 我们在 paint 中 displayX 就是 viewX（viewX 是 content 内的相对坐标）
    const float stretch = juce::jmax (0.001f, horizontalStretch);
    const float nx0 = (viewX0 - viewOffsetPx) / stretch;
    const float nx1 = (viewX1 - viewOffsetPx) / stretch;

    const float w = (float) spectrogramNativeWidth;
    double t0 = juce::jlimit (0.0, duration, (double) nx0 / (double) juce::jmax (1.0f, w) * duration);
    double t1 = juce::jlimit (0.0, duration, (double) nx1 / (double) juce::jmax (1.0f, w) * duration);
    if (t1 <= t0) t1 = juce::jmin (duration, t0 + 0.001);
    return { t0, t1 };
}

float StandaloneAudioSpectrogramView::getImageBoxPixelWidth() const
{
    if (imageBox == nullptr) return 0.0f;
    return (float) imageBox->getWidth();
}

// 一次性计算整段音频的时频图，宽度按"原生"策略选取（约 3px/帧）
void StandaloneAudioSpectrogramView::computeSpectrogramImage()
{
    if (currentAudio.getNumSamples() <= 0 || currentSampleRate <= 0.0)
    {
        spectrogram = juce::Image();
        spectrogramNativeWidth = 0;
        spectrogramNativeHeight = 0;
        return;
    }

    const int N = juce::jlimit (256, 32768, currentFftSize);
    const int hop = N / 4;
    const int numSamples = currentAudio.getNumSamples();
    const int numFrames = juce::jmax (1, (numSamples - N) / hop + 1);

    // 时频图目标像素高度：默认取 content 高度或 512（选大者），后续显示时按需拉伸
    const int H = juce::jmax (256, contentBounds.getHeight() > 0 ? contentBounds.getHeight() : 512);
    // 时频图目标像素宽度：直接 = 帧数，但 clamp 到合理区间避免超大图（同时也是"1x speed 时的宽度"）
    const int nativeW = juce::jlimit (256, 32768, numFrames);

    spectrogramNativeWidth  = nativeW;
    spectrogramNativeHeight = H;

    spectrogram = juce::Image (juce::Image::RGB, nativeW, H, true);
    {
        juce::Graphics g (spectrogram);
        g.fillAll (kSpectrogramBgColour);
    }

    // 准备 FFT
    const int order = (int) std::round (std::log2 ((double) N));
    juce::dsp::FFT fft (order);

    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
        window[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) n / (float) (N - 1)));
    const float magNorm = displayMagNormForWindow (window, N);

    std::vector<float> fftWork ((size_t) (2 * N), 0.0f);
    std::vector<float> mono   ((size_t) N, 0.0f);
    std::vector<float> mags   ((size_t) (N / 2 + 1), 0.0f);

    const int numCh = currentAudio.getNumChannels();
    const float invCh = 1.0f / (float) juce::jmax (1, numCh);
    const float maxHz = (float) (currentSampleRate * 0.5);
    const float dbFloor = -96.0f;

    juce::Image::BitmapData bmp (spectrogram, juce::Image::BitmapData::readWrite);

    for (int f = 0; f < nativeW; ++f)
    {
        // 该列对应的帧索引（如果 nativeW < numFrames，则线性插值取样；反之为帧本身）
        const int frameIdx = juce::jlimit (0, numFrames - 1,
                                            (int) ((int64_t) f * (int64_t) numFrames / juce::jmax (1, nativeW)));
        const int start = frameIdx * hop;
        for (int n = 0; n < N; ++n)
        {
            const int s = start + n;
            float acc = 0.0f;
            if (s < numSamples)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    acc += currentAudio.getReadPointer (ch)[s];
                acc *= invCh;
            }
            mono[(size_t) n] = acc * window[(size_t) n];
        }
        std::copy (mono.begin(), mono.end(), fftWork.begin());
        std::fill (fftWork.begin() + N, fftWork.end(), 0.0f);

        fft.performRealOnlyForwardTransform (fftWork.data());

        // DC
        mags[0] = std::abs (fftWork[0]) * magNorm;
        const int half = N / 2 + 1;
        for (int k = 1; k < half - 1; ++k)
        {
            const float re = fftWork[(size_t) (2 * k)];
            const float im = fftWork[(size_t) (2 * k + 1)];
            mags[(size_t) k] = std::sqrt (re * re + im * im) * magNorm;
        }
        mags[(size_t) (half - 1)] = std::abs (fftWork[1]) * magNorm;

        // 逐像素行绘制
        const int numBins = half;
        for (int y = 0; y < H; ++y)
        {
            const float yNorm = (float) y / juce::jmax (1, H - 1);
            float hz;
            if (scaleMode == 1) hz = FreqMap::yNormToFrequencyLog (yNorm, maxHz);
            else                hz = FreqMap::yNormToFrequencyLinear (yNorm, maxHz);

            const float binF = (hz / juce::jmax (1.0f, maxHz)) * (float) (numBins - 1);
            const int b0 = juce::jlimit (0, numBins - 1, (int) std::floor (binF));
            const int b1 = juce::jlimit (0, numBins - 1, b0 + 1);
            const float t = juce::jlimit (0.0f, 1.0f, binF - (float) b0);
            const float m = mags[(size_t) b0] + (mags[(size_t) b1] - mags[(size_t) b0]) * t;

            const float db = juce::Decibels::gainToDecibels (juce::jmax (1e-9f, m));
            const float dbNorm = juce::jlimit (0.0f, 1.0f, (db - dbFloor) / (-dbFloor));

            const juce::Colour c = mapHeatmap (dbNorm);
            bmp.setPixelColour (f, y, c);
        }
    }
}

juce::Colour StandaloneAudioSpectrogramView::mapHeatmap (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    const juce::Colour c0 (0xff000000);
    const juce::Colour c1 (0xff2a0a55);
    const juce::Colour c2 (0xffd13a1a);
    const juce::Colour c3 (0xfff8e83a);
    if (t < 0.33f)      return c0.interpolatedWith (c1, t / 0.33f);
    else if (t < 0.66f) return c1.interpolatedWith (c2, (t - 0.33f) / 0.33f);
    else                return c2.interpolatedWith (c3, (t - 0.66f) / 0.34f);
}

void StandaloneAudioSpectrogramView::paint (juce::Graphics& g)
{
    drawFrequencyAxis (g);

    g.setColour (kSpectrogramBgColour);
    g.fillRect (contentBounds);

    if (spectrogram.isValid() && spectrogramNativeWidth > 0)
    {
        // 显示：把 spectrogram（宽 nativeW × 高 H）以 horizontalStretch 缩放绘制到 contentBounds，
        // 并加上 viewOffsetPx 的横向平移；纵向拉伸填满 content 高度。
        g.saveState();
        g.reduceClipRegion (contentBounds);

        const float displayW = (float) spectrogramNativeWidth * horizontalStretch;
        const float dstX = (float) contentBounds.getX() + viewOffsetPx;
        const float dstY = (float) contentBounds.getY();
        const float dstH = (float) contentBounds.getHeight();

        g.drawImage (spectrogram,
                     dstX, dstY, displayW, dstH,
                     0, 0, spectrogramNativeWidth, spectrogramNativeHeight);
        g.restoreState();
    }

    g.setColour (juce::Colour (0xff2a2d30));
    g.drawRect (contentBounds, 1);
}

void StandaloneAudioSpectrogramView::drawFrequencyAxis (juce::Graphics& g)
{
    g.setColour (kAxisBgColour);
    g.fillRect (axisBounds);

    const float maxHz = juce::jmax (100.0f, (float) (currentSampleRate * 0.5));
    juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
    f = f.withHeight (10.0f);
    g.setFont (f);
    g.setColour (kAxisColour);

    if (scaleMode == 1)
    {
        drawPianoKeys (g, maxHz);
        const std::vector<float> hzMarks { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000 };
        const int textX = axisBounds.getRight() - 32;
        for (auto hz : hzMarks)
        {
            if (hz > maxHz) continue;
            const float yNorm = FreqMap::frequencyToYNormLog (hz, maxHz);
            const int y = contentBounds.getY() + juce::roundToInt (yNorm * (contentBounds.getHeight() - 1));
            juce::String label = (hz >= 1000.0f) ? (juce::String (hz / 1000.0f, 0) + "k")
                                                 : juce::String ((int) hz);
            g.drawText (label, textX, y - 6, 30, 12, juce::Justification::centredRight);
        }
    }
    else
    {
        const int N = 10;
        for (int i = 0; i <= N; ++i)
        {
            const float yNorm = (float) i / (float) N;
            const int y = contentBounds.getY() + juce::roundToInt (yNorm * (contentBounds.getHeight() - 1));
            const float hz = FreqMap::yNormToFrequencyLinear (yNorm, maxHz);
            juce::String label = (hz >= 1000.0f) ? (juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k")
                                                 : juce::String ((int) std::round (hz));
            g.drawText (label, axisBounds.getX() + 4, y - 6,
                        axisBounds.getWidth() - 8, 12, juce::Justification::centredRight);
        }
    }
}

void StandaloneAudioSpectrogramView::drawPianoKeys (juce::Graphics& g, float maxHz)
{
    const int kx = axisBounds.getX();
    const int kw = kPianoWidth;
    const int kytop = contentBounds.getY();
    const int kyh   = contentBounds.getHeight();

    auto noteToHz = [] (int midi) { return 440.0f * std::pow (2.0f, (midi - 69) / 12.0f); };
    const int midiStart = 24;
    const int midiEnd   = 120;

    g.setColour (kPianoWhiteKey);
    g.fillRect (kx, kytop, kw, kyh);

    for (int m = midiStart; m <= midiEnd; ++m)
    {
        const float hz = noteToHz (m);
        if (hz > maxHz) break;
        const float yNorm = FreqMap::frequencyToYNormLog (hz, maxHz);
        const int y = kytop + juce::roundToInt (yNorm * (kyh - 1));

        const int pitchClass = m % 12;
        const bool isBlack = (pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                              || pitchClass == 8 || pitchClass == 10);
        if (isBlack)
        {
            g.setColour (kPianoBlackKey);
            g.fillRect (kx, y - 1, juce::roundToInt (kw * 0.6f), 3);
        }
        else
        {
            g.setColour (juce::Colours::darkgrey);
            g.drawLine ((float) kx, (float) y, (float) (kx + kw), (float) y, 0.5f);
        }

        if (pitchClass == 0)
        {
            g.setColour (juce::Colours::black);
            juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
            f = f.withHeight (9.0f);
            g.setFont (f);
            const int oct = m / 12 - 1;
            g.drawText ("C" + juce::String (oct), kx + 2, y - 6, kw - 4, 12,
                        juce::Justification::centredLeft);
        }
    }

    g.setColour (juce::Colour (0xff555555));
    g.drawRect (kx, kytop, kw, kyh, 1);
}

// ---- 鼠标横向拖动：只在频谱内容区（非图片框、非频率刻度）按下时启用 ----
void StandaloneAudioSpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    // 仅当点击落在 contentBounds 内（父视图坐标）时启用拖动
    if (! contentBounds.contains (e.getPosition())) { dragging = false; return; }
    dragging = true;
    dragMoved = false;
    dragStartX = e.getPosition().getX();
    dragStartY = e.getPosition().getY();
    dragStartOffsetPx = viewOffsetPx;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void StandaloneAudioSpectrogramView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const int dx = e.getPosition().getX() - dragStartX;
    const int dy = e.getPosition().getY() - dragStartY;
    if (std::abs (dx) + std::abs (dy) > 3) dragMoved = true;

    viewOffsetPx = dragStartOffsetPx + (float) dx;

    // 限幅：不允许拖到完全看不到时频图
    const float displayW = (float) spectrogramNativeWidth * horizontalStretch;
    const float minOffset = juce::jmin (0.0f, (float) contentBounds.getWidth() - displayW);
    viewOffsetPx = juce::jlimit (minOffset, 0.0f, viewOffsetPx);

    repaint();
}

void StandaloneAudioSpectrogramView::mouseUp (const juce::MouseEvent&)
{
    const bool wasClick = dragging && ! dragMoved;
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    // 未发生拖动的“纯点击”：如果当前没有时频图，则触发回调（弹音频选择器）
    // 如果已有时频图，不做任何事（避免意外弹窗）
    if (wasClick && spectrogram.isNull() && onEmptyClicked)
        onEmptyClicked();
}

// ============================================================================
//  RenderJob —— 后台离线渲染 + WAV 写盘
// ============================================================================
class SpectrumTagMainComponent::RenderJob : public juce::Thread
{
public:
    RenderJob (SpectrumTagMainComponent& owner,
               juce::AudioBuffer<float> input,
               double sampleRate,
               int bitsPerSample,
               const juce::AudioChannelSet& channelSet,
               OfflineRenderParams params,
               std::vector<float> mask,
               juce::File outputFile)
        : juce::Thread ("SpectrumTag.RenderJob"),
          ownerRef (owner),
          input (std::move (input)),
          sampleRate (sampleRate),
          bitsPerSample (bitsPerSample),
          channelSet (channelSet),
          params (std::move (params)),
          mask (std::move (mask)),
          outputFile (std::move (outputFile))
    {}

    void run() override
    {
        ownerRef.renderRunning.store (true);
        ownerRef.renderProgress.store (0.0f);

        juce::AudioBuffer<float> output;
        OfflineRenderer renderer;
        auto progressCb = [this] (float p) -> bool
        {
            ownerRef.renderProgress.store (p);
            return ! threadShouldExit();
        };
        const bool ok = renderer.render (input, output, sampleRate, params, mask, progressCb);

        if (ok)
        {
            // 写 WAV
            juce::WavAudioFormat wav;
            outputFile.deleteFile();
            std::unique_ptr<juce::FileOutputStream> os (outputFile.createOutputStream());
            if (os != nullptr)
            {
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
                    os.get(),
                    sampleRate,
                    (unsigned int) output.getNumChannels(),
                    juce::jlimit (16, 32, bitsPerSample),
                    {},
                    0));
                if (writer != nullptr)
                {
                    os.release(); // writer 会接管所有权
                    writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples());
                    writer->flush();
                    writer.reset(); // 关闭
                    resultOk = true;
                }
            }
        }

        // 通知主线程
        juce::MessageManager::callAsync ([this, ok] ()
        {
            const bool success = ok && resultOk;
            ownerRef.renderRunning.store (false);
            if (success)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    "SpectrumTag",
                    "Successfully exported:\n" + outputFile.getFullPathName());
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "SpectrumTag",
                    "Export failed. Please check the output directory and try again.");
            }
        });
    }

private:
    SpectrumTagMainComponent& ownerRef;
    juce::AudioBuffer<float>  input;
    double                    sampleRate;
    int                       bitsPerSample;
    juce::AudioChannelSet     channelSet;
    OfflineRenderParams       params;
    std::vector<float>        mask;
    juce::File                outputFile;
    bool                      resultOk = false;
};

// ============================================================================
//  SpectrumTagMainComponent
// ============================================================================
SpectrumTagMainComponent::SpectrumTagMainComponent()
{
    formatManager.registerBasicFormats();   // WAV / AIFF / MP3 / FLAC / OGG (若 JUCE 启用)

    basementTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BasementGrotesqueBlack_v1_202_otf,
        BinaryData::BasementGrotesqueBlack_v1_202_otfSize);

    setLookAndFeel (&lookAndFeel);

    // ---- 顶部 ----
    titleLabel.setText ("SpectrumTag", juce::dontSendNotification);
    styleHeaderLabel (titleLabel, 32.0f, kTextWhite);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (juce::String ("v") + ProjectInfo::versionString, juce::dontSendNotification);
    styleHeaderLabel (versionLabel, 18.0f, kTextWhite);
    addAndMakeVisible (versionLabel);

    audioFileLabel.setJustificationType (juce::Justification::centredLeft);
    audioFileLabel.setColour (juce::Label::textColourId, kTextSub);
    audioFileLabel.setFont (juce::Font (basementTypeface).withHeight (13.0f));
    audioFileLabel.setText ("Drop an audio file (WAV / MP3 / FLAC ...) to begin",
                            juce::dontSendNotification);
    addAndMakeVisible (audioFileLabel);

    // ---- 频谱视图 ----
    spectrumView = std::make_unique<StandaloneAudioSpectrogramView> (basementTypeface);
    addAndMakeVisible (*spectrumView);

    spectrumView->getImageBox().onChanged = [this] { repaint(); };
    spectrumView->getImageBox().onImagePicked = [this] (const juce::File&) { repaint(); };
    spectrumView->onEmptyClicked = [this] { pickAudioFile(); };

    // ---- 标签 ----
    styleControlLabel (fftSizeLabel);
    styleControlLabel (fftScaleLabel);
    styleControlLabel (speedLabel);
    styleControlLabel (amplitudeLabel);
    styleControlLabel (invertLabel);
    addAndMakeVisible (fftSizeLabel);
    addAndMakeVisible (fftScaleLabel);
    addAndMakeVisible (speedLabel);
    addAndMakeVisible (amplitudeLabel);
    addAndMakeVisible (invertLabel);

    fftSizeCombo.addItemList ({ "1024", "2048", "4096", "8192" }, 1);
    fftSizeCombo.setSelectedItemIndex (2, juce::dontSendNotification);   // 4096 默认
    addAndMakeVisible (fftSizeCombo);
    fftSizeCombo.onChange = [this]
    {
        const int idx = fftSizeCombo.getSelectedItemIndex();
        const int N = (idx == 0 ? 1024 : idx == 1 ? 2048 : idx == 2 ? 4096 : 8192);
        if (spectrumView != nullptr)
            spectrumView->rebuildSpectrogram (N);
    };

    fftScaleCombo.addItemList ({ "linear", "mel" }, 1);
    fftScaleCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    addAndMakeVisible (fftScaleCombo);
    fftScaleCombo.onChange = [this]
    {
        if (spectrumView != nullptr)
            spectrumView->setScaleMode (fftScaleCombo.getSelectedItemIndex());
    };

    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    speedSlider.setRange (0.1, 4.0, 0.0);
    speedSlider.setSkewFactor (0.5);
    speedSlider.setValue (1.0, juce::dontSendNotification);
    speedSlider.onValueChange = [this]
    {
        if (spectrumView != nullptr)
            spectrumView->setSpeed ((float) speedSlider.getValue());
    };
    addAndMakeVisible (speedSlider);

    amplitudeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amplitudeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amplitudeSlider.setRange (0.0, 1.5, 0.0);
    amplitudeSlider.setValue (0.0, juce::dontSendNotification);
    addAndMakeVisible (amplitudeSlider);

    invertToggle.setButtonText ({});
    addAndMakeVisible (invertToggle);

    printButton.setTypeface (basementTypeface);
    printButton.onClick = [this] { onPrintClicked(); };
    addAndMakeVisible (printButton);

    // 窗口的可缩放性由 DocumentWindow（StandaloneMain.cpp）负责；这里只准备约束器
    const double aspect = (double) kDefaultWidth / (double) kDefaultHeight;
    resizeConstrainer.setFixedAspectRatio (aspect);
    resizeConstrainer.setSizeLimits (kMinWidth, kMinHeight, 4096, 4096);
    setSize (kDefaultWidth, kDefaultHeight);

    startTimerHz (10);

    telemetrySession = std::make_unique<iisaac::telemetry::Session> (
        iisaac::telemetry::forStandalone ("spectrumtag", ProjectInfo::versionString,
                                         ProjectInfo::versionString));
}

SpectrumTagMainComponent::~SpectrumTagMainComponent()
{
    telemetrySession.reset();
    stopTimer();
    if (renderJob != nullptr)
    {
        renderJob->stopThread (2000);
        renderJob.reset();
    }
    setLookAndFeel (nullptr);
}

void SpectrumTagMainComponent::styleHeaderLabel (juce::Label& l, float h, juce::Colour c)
{
    l.setColour (juce::Label::textColourId, c);
    l.setFont (juce::Font (basementTypeface).withHeight (h));
    l.setJustificationType (juce::Justification::centredLeft);
}

void SpectrumTagMainComponent::styleControlLabel (juce::Label& l)
{
    l.setColour (juce::Label::textColourId, kTextWhite);
    l.setFont (juce::Font (basementTypeface).withHeight (18.0f));
    l.setJustificationType (juce::Justification::centredLeft);
}

void SpectrumTagMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (kBgColour);
}

void SpectrumTagMainComponent::resized()
{
    const float scale = (float) getHeight() / (float) kDefaultHeight;
    auto px = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto r = getLocalBounds();

    // 顶部
    const int headerH = px (54);
    auto header = r.removeFromTop (headerH).reduced (px (20), px (8));

    titleLabel.setBounds (header.removeFromLeft (px (250)));
    header.removeFromLeft (px (8));
    versionLabel.setBounds (header.removeFromLeft (px (80)));
    header.removeFromLeft (px (16));
    audioFileLabel.setBounds (header);

    titleLabel.setFont   (juce::Font (basementTypeface).withHeight (32.0f * scale));
    versionLabel.setFont (juce::Font (basementTypeface).withHeight (18.0f * scale));
    audioFileLabel.setFont (juce::Font (basementTypeface).withHeight (13.0f * scale));

    // 主体
    auto body = r.reduced (px (20), 0).withTrimmedBottom (px (20));
    const int rightPanelW = px (280);
    auto rightPanel = body.removeFromRight (rightPanelW);
    body.removeFromRight (px (20));

    if (spectrumView != nullptr)
        spectrumView->setBounds (body);

    const int labelH   = px (22);
    const int comboH   = px (28);
    const int sliderH  = px (18);
    const int spacing  = px (10);
    const int rowGap   = px (24);

    auto controlFont = juce::Font (basementTypeface).withHeight (18.0f * scale);
    fftSizeLabel.setFont (controlFont);
    fftScaleLabel.setFont (controlFont);
    speedLabel.setFont (controlFont);
    amplitudeLabel.setFont (controlFont);
    invertLabel.setFont (controlFont);

    auto layoutSameLine = [&] (juce::Rectangle<int>& panel, juce::Label& lbl, juce::ComboBox& combo)
    {
        auto row = panel.removeFromTop (juce::jmax (labelH, comboH));
        auto comboW = px (110);
        combo.setBounds (row.removeFromRight (comboW).withSizeKeepingCentre (comboW, comboH));
        lbl.setBounds (row);
        panel.removeFromTop (rowGap);
    };

    layoutSameLine (rightPanel, fftSizeLabel,  fftSizeCombo);
    layoutSameLine (rightPanel, fftScaleLabel, fftScaleCombo);

    speedLabel.setBounds (rightPanel.removeFromTop (labelH));
    rightPanel.removeFromTop (spacing);
    speedSlider.setBounds (rightPanel.removeFromTop (sliderH));
    rightPanel.removeFromTop (rowGap);

    amplitudeLabel.setBounds (rightPanel.removeFromTop (labelH));
    rightPanel.removeFromTop (spacing);
    amplitudeSlider.setBounds (rightPanel.removeFromTop (sliderH));
    rightPanel.removeFromTop (rowGap);

    rightPanel.removeFromTop (px (20));

    {
        auto row = rightPanel.removeFromTop (px (28));
        const int dotW = px (24);
        invertToggle.setBounds (row.removeFromLeft (px (110) + dotW)
                                   .withTrimmedLeft (px (110))
                                   .withWidth (dotW));
        invertLabel.setBounds (juce::Rectangle<int> (rightPanel.getX(),
                                                      invertToggle.getY(),
                                                      px (100),
                                                      invertToggle.getHeight()));
    }

    const int printD = px (160);
    auto printArea = juce::Rectangle<int> (
        getWidth() - px (20) - printD,
        getHeight() - px (20) - printD,
        printD, printD);
    printButton.setBounds (printArea);
}

// ============================================================================
//  文件拖入
// ============================================================================
bool SpectrumTagMainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".bmp" || ext == ".gif") return true;
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac"
            || ext == ".aif" || ext == ".aiff" || ext == ".ogg") return true;
    }
    return false;
}

void SpectrumTagMainComponent::filesDropped (const juce::StringArray& files,
                                              int /*x*/, int /*y*/)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (! file.existsAsFile()) continue;
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".bmp" || ext == ".gif")
        {
            loadImage (file);
        }
        else if (ext == ".wav" || ext == ".mp3" || ext == ".flac"
                 || ext == ".aif" || ext == ".aiff" || ext == ".ogg")
        {
            loadAudioFile (file);
        }
    }
}

void SpectrumTagMainComponent::fileDragEnter (const juce::StringArray&, int, int) {}
void SpectrumTagMainComponent::fileDragExit  (const juce::StringArray&)         {}

bool SpectrumTagMainComponent::loadImage (const juce::File& file)
{
    return spectrumView != nullptr
        && spectrumView->getImageBox().loadImageFromFile (file);
}

// 弹出音频文件选择器（点击频谱空白区或未加载时点击 Print 会走到这里）
void SpectrumTagMainComponent::pickAudioFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select an audio file",
        juce::File(),
        "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");

    const int flags = juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this, chooser] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
            loadAudioFile (f);
    });
}

void SpectrumTagMainComponent::loadAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "SpectrumTag",
            "Failed to open audio file:\n" + file.getFullPathName());
        return;
    }
    const int64 total = reader->lengthInSamples;
    if (total <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "SpectrumTag", "Audio file appears to be empty.");
        return;
    }
    // 只读单声道或立体声：多声道也允许，但离线渲染按原通道数处理
    const int numChannels = juce::jmax (1, (int) reader->numChannels);
    juce::AudioBuffer<float> buffer (numChannels, (int) total);
    reader->read (&buffer, 0, (int) total, 0, true, true);

    currentAudio = std::move (buffer);
    currentAudioFile = file;
    currentSampleRate = reader->sampleRate;
    currentBitsPerSample = juce::jlimit (16, 32, (int) reader->bitsPerSample > 0 ? (int) reader->bitsPerSample : 16);
    currentFormatName = reader->getFormatName();
    currentChannelLayout = juce::AudioChannelSet::canonicalChannelSet (numChannels);

    audioFileLabel.setText ("Loaded: " + file.getFileName()
                            + juce::String::formatted ("   |   %.2f s   |   %.1f kHz   |   %d ch",
                                                       (double) currentAudio.getNumSamples() / currentSampleRate,
                                                       currentSampleRate / 1000.0,
                                                       numChannels),
                            juce::dontSendNotification);

    if (spectrumView != nullptr)
    {
        const int idx = fftSizeCombo.getSelectedItemIndex();
        const int N = (idx == 0 ? 1024 : idx == 1 ? 2048 : idx == 2 ? 4096 : 8192);
        spectrumView->setAudio (currentAudio, currentSampleRate, N);
    }
}

// ============================================================================
//  Print
// ============================================================================
void SpectrumTagMainComponent::onPrintClicked()
{
    if (renderRunning.load()) return;

    // 没有音频：直接弹出音频选择器（而不是提示“请先拖入”）
    if (spectrumView == nullptr || currentAudio.getNumSamples() <= 0)
    {
        pickAudioFile();
        return;
    }

    auto& imgBox = spectrumView->getImageBox();
    if (! imgBox.hasImage())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "SpectrumTag", "Please drop or click to load an image first.");
        return;
    }

    // 弹出目录选择
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select output directory",
        currentAudioFile.getParentDirectory(),
        juce::String());

    const int flags = juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync (flags, [this, chooser] (const juce::FileChooser& fc)
    {
        auto dir = fc.getResult();
        if (! dir.isDirectory()) return;

        // 输出文件名：<原名>_tagged.wav；若已存在则加数字后缀
        const juce::String baseName = currentAudioFile.getFileNameWithoutExtension() + "_tagged";
        juce::File outFile = dir.getChildFile (baseName + ".wav");
        int suffix = 1;
        while (outFile.existsAsFile())
        {
            outFile = dir.getChildFile (baseName + "_" + juce::String (suffix) + ".wav");
            ++suffix;
        }

        // 准备渲染参数
        OfflineRenderParams params;
        const int idx = fftSizeCombo.getSelectedItemIndex();
        params.fftSize = (idx == 0 ? 1024 : idx == 1 ? 2048 : idx == 2 ? 4096 : 8192);
        params.amplitudeRatio = (float) amplitudeSlider.getValue();
        params.invert = invertToggle.getToggleState();

        // 图片框 → 时间区间
        auto [t0, t1] = spectrumView->getImageBoxTimeRange();
        params.startSec = t0;
        params.endSec   = t1;

        // 图片框 → 频率区间（用其归一化 Y 反推）
        // 与插件 UI 一致：normRect y=0 = 顶部 = 高频；y=1 = 底部 = 低频
        const auto normR = spectrumView->getImageBox().getNormalisedBounds();
        const float maxHz = (float) (currentSampleRate * 0.5);
        const int scaleMode = fftScaleCombo.getSelectedItemIndex();
        auto yNormToFreq = [&] (float y)
        {
            return scaleMode == 1
                ? FreqMap::yNormToFrequencyLog (y, maxHz)
                : FreqMap::yNormToFrequencyLinear (y, maxHz);
        };
        const float fTop = yNormToFreq (normR.getY());
        const float fBot = yNormToFreq (normR.getBottom());
        const float fLow  = juce::jmin (fTop, fBot);
        const float fHigh = juce::jmax (fTop, fBot);
        params.freqLowNorm  = juce::jlimit (0.0f, 1.0f, fLow  / juce::jmax (1.0f, maxHz));
        params.freqHighNorm = juce::jlimit (0.0f, 1.0f, fHigh / juce::jmax (1.0f, maxHz));

        // mask
        const int rows = params.fftSize / 2 + 1;
        const int cols = computeMaskCols (spectrumView->getImageBoxPixelWidth());
        params.maskRows = rows;
        params.maskCols = cols;
        auto mask = spectrumView->getImageBox().generateMask (rows, cols);

        // 检查区间有效性
        if (params.endSec <= params.startSec + 0.01)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "SpectrumTag",
                "The image box covers a too-short time range.\nPlease move or enlarge the image box on the spectrum.");
            return;
        }

        // 启动后台渲染
        renderProgress.store (0.0f);
        printButton.setEnabled (false);

        renderJob = std::make_unique<RenderJob> (
            *this,
            currentAudio,
            currentSampleRate,
            currentBitsPerSample,
            currentChannelLayout,
            params,
            std::move (mask),
            outFile);
        renderJob->startThread();
    });
}

void SpectrumTagMainComponent::timerCallback()
{
    // 更新按钮状态
    const bool running = renderRunning.load();
    if (! running && ! printButton.isEnabled())
        printButton.setEnabled (true);

    // 显示进度
    if (running)
    {
        const int pct = juce::jlimit (0, 100, (int) std::round (renderProgress.load() * 100.0f));
        audioFileLabel.setText ("Rendering... " + juce::String (pct) + "%",
                                juce::dontSendNotification);
    }
    else if (renderJob != nullptr && ! renderJob->isThreadRunning())
    {
        // 清理已完成的 job
        renderJob.reset();
        if (currentAudio.getNumSamples() > 0)
        {
            audioFileLabel.setText ("Loaded: " + currentAudioFile.getFileName()
                + juce::String::formatted ("   |   %.2f s   |   %.1f kHz   |   %d ch",
                                            (double) currentAudio.getNumSamples() / currentSampleRate,
                                            currentSampleRate / 1000.0,
                                            currentAudio.getNumChannels()),
                juce::dontSendNotification);
        }
    }
}
