#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <atomic>
#include "SharedUI.h"
#include "OfflineRenderer.h"
#include "../shared/IisaacTelemetry.h"

// ============================================================================
//  SpectrumTagStandaloneWindow
// ----------------------------------------------------------------------------
//  Standalone 主窗口。整体布局与 VST3 [PluginEditor.cpp] 完全一致，
//  但工作模式为"文件模式"：
//   - 拖入音频文件（或点击频谱空白区选择） → 读入 AudioBuffer，
//     计算整段静态时频图，铺满左侧频谱区；
//   - Speed 参数横向缩放整段时频图（>1 拉伸，<1 压缩）；
//   - 鼠标按住频谱空白区可以横向拖动查看被裁掉的部分；
//   - 拖入图片 → 显示可拖动/缩放的图片框；
//   - 点 Print → 弹出目录选择 → 后台线程离线合成 → 输出 <原文件名>_tagged.wav
//     到用户选择的目录（同采样率/位深/声道数）。
// ============================================================================

class StandaloneAudioSpectrogramView;

class SpectrumTagMainComponent : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer
{
public:
    SpectrumTagMainComponent();
    ~SpectrumTagMainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;

private:
    void timerCallback() override;
    void onPrintClicked();

    void pickAudioFile();  // 弹出音频文件选择器
    void loadAudioFile (const juce::File& file);
    bool loadImage    (const juce::File& file);

    void styleHeaderLabel (juce::Label& l, float height, juce::Colour colour);
    void styleControlLabel (juce::Label& l);

    juce::Typeface::Ptr    basementTypeface;
    StandaloneLookAndFeel  lookAndFeel;

    juce::AudioFormatManager formatManager;

    juce::Label                       titleLabel;
    juce::Label                       versionLabel;

    std::unique_ptr<StandaloneAudioSpectrogramView> spectrumView;

    juce::Label    fftSizeLabel    { {}, "FFT Size:" };
    juce::Label    fftScaleLabel   { {}, "FFT scale:" };
    juce::Label    speedLabel      { {}, "Speed:" };
    juce::Label    amplitudeLabel  { {}, "Amplitude Ratio:" };
    juce::Label    invertLabel     { {}, "Invert:" };
    juce::Label    audioFileLabel; // 显示当前载入的音频文件名

    juce::ComboBox fftSizeCombo;
    juce::ComboBox fftScaleCombo;
    juce::Slider   speedSlider;
    juce::Slider   amplitudeSlider;
    juce::ToggleButton invertToggle;
    RoundPrintButton   printButton { "Print" };

    juce::ComponentBoundsConstrainer resizeConstrainer;

    // ---- 音频文件 ----
    juce::File                    currentAudioFile;
    juce::AudioBuffer<float>      currentAudio;
    double                        currentSampleRate = 44100.0;
    int                           currentBitsPerSample = 16;
    juce::String                  currentFormatName;
    juce::AudioChannelSet         currentChannelLayout;

    // ---- 后台渲染线程 ----
    class RenderJob;
    std::unique_ptr<RenderJob>    renderJob;
    std::atomic<bool>             renderRunning { false };
    std::atomic<float>            renderProgress { 0.0f };

    std::unique_ptr<iisaac::telemetry::Session> telemetrySession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumTagMainComponent)
};

// ============================================================================
//  StandaloneAudioSpectrogramView
// ----------------------------------------------------------------------------
//  静态时频图 + 频率刻度 + 内嵌 ImageBoxComponent。
//  与插件的 SpectrumView 区别：不再滚动，而是一次性计算整段音频的时频图并缓存。
//  Speed 参数改为对整个时频图做横向缩放显示；鼠标按住可横向拖动。
// ============================================================================
class StandaloneAudioSpectrogramView : public juce::Component
{
public:
    explicit StandaloneAudioSpectrogramView (juce::Typeface::Ptr tf);

    ImageBoxComponent& getImageBox() { return *imageBox; }
    juce::Rectangle<int> getContentBounds() const { return contentBounds; }

    // 当频谱内容区被点击（且没发生拖动）时触发；主窗口据此弹出音频选择器
    std::function<void()> onEmptyClicked;

    // 由主窗口传入音频：立即（后台）计算整段时频图
    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate, int fftSize);
    void clearAudio();

    // 用户改 FFT Size 后重新计算
    void rebuildSpectrogram (int fftSize);

    // 显示相关参数
    // 注意：linear/mel 决定"频率 → 时频图像素行"的映射，而时频图是预先烘焙好的
    // Image。只 repaint() 重画的是旧图，必须重新烘焙，否则切换后画面不变，
    // 只有重新载入音频（触发 setAudio → 烘焙）才生效。
    void setScaleMode (int m)
    {
        if (m == scaleMode) return;
        scaleMode = m;
        computeSpectrogramImage();
        repaint();
    }
    void setSpeed     (float speed) { horizontalStretch = juce::jmax (0.05f, speed); repaint(); }
    float getSpeed() const          { return horizontalStretch; }

    // 图片框水平位置对应的音频时间区间（秒），仅在音频已加载后有意义
    // 返回 { startSec, endSec }
    std::pair<double, double> getImageBoxTimeRange() const;

    double getAudioDurationSec() const
    {
        return currentAudio.getNumSamples() > 0 && currentSampleRate > 0.0
             ? (double) currentAudio.getNumSamples() / currentSampleRate : 0.0;
    }

    // 图片框像素宽度（用于计算 mask cols）
    float getImageBoxPixelWidth() const;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;

    // 供主窗口读取用户在频谱上通过鼠标的横向拖动状态（图片框位置由 ImageBox 自己保存）
    // 这里没有对外暴露 API：拖动只影响本组件内部的 viewOffsetPx 显示偏移。

private:
    void computeSpectrogramImage();
    void drawFrequencyAxis (juce::Graphics& g);
    void drawPianoKeys (juce::Graphics& g, float maxHz);

    static juce::Colour mapHeatmap (float t);

    juce::Typeface::Ptr typeface;
    std::unique_ptr<ImageBoxComponent> imageBox;

    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> axisBounds;

    juce::Image spectrogram;   // 整段音频的时频图，宽度 = spectrogramNativeWidth 像素
    int         spectrogramNativeWidth  = 0;   // 原生（1x speed）下时频图宽度
    int         spectrogramNativeHeight = 0;
    int         currentFftSize = 4096;

    // 缓存音频，供 rebuildSpectrogram 复用
    juce::AudioBuffer<float> currentAudio;
    double                   currentSampleRate = 44100.0;

    int   scaleMode         = 0;       // 0 linear, 1 mel
    float horizontalStretch = 1.0f;    // Speed 参数：横向缩放系数
    float viewOffsetPx      = 0.0f;    // 视图水平偏移（像素，正=向右拖）

    bool  dragging = false;
    bool  dragMoved = false; // 本次按下过程中是否发生过实际移动
    int   dragStartX = 0;
    int   dragStartY = 0;
    float dragStartOffsetPx = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneAudioSpectrogramView)
};
