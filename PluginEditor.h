#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

// ============================================================================
//  SpectrumTagAudioProcessorEditor
// ----------------------------------------------------------------------------
//  布局（参考 UI.png）：
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │ SpectrumTag   v1.0.0  iisaacbeats.cn                                  │
//   │ ┌──────────────────────────────────────┐   FFT Size:   [2048▾]       │
//   │ │                                      │   FFT scale:  [linear▾]     │
//   │ │  (频谱瀑布 + 中央图片框)              │   Speed:      ━━○━━━        │
//   │ │                                      │   Amplitude:  ━━━━○━        │
//   │ │                                      │                              │
//   │ │                                      │   Invert: ●                  │
//   │ │                                      │              ╭───────╮      │
//   │ │                                      │              │ Print │      │
//   │ └──────────────────────────────────────┘              ╰───────╯      │
//   └──────────────────────────────────────────────────────────────────────┘
// ============================================================================

// ----------------------------------------------------------------------------
//  自定义 LookAndFeel：注入 BasementGrotesque-Black 字体 + 整体暗色风格
// ----------------------------------------------------------------------------
class SpectrumTagLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpectrumTagLookAndFeel();

    // 默认字体（所有控件未显式 setFont 时都会用这个）
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // 滑块：细线 + 圆形 thumb（参考 UI.png）
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;

    // ComboBox：浅灰底 + 黑边 + 黑色三角箭头
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // ToggleButton：小圆点（Invert）
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr typeface;
};

// ----------------------------------------------------------------------------
//  自定义 Print 按钮：白色填充大圆 + 黑色文字
// ----------------------------------------------------------------------------
class RoundPrintButton : public juce::Button
{
public:
    explicit RoundPrintButton (const juce::String& name);
    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;
    void setTypeface (juce::Typeface::Ptr tf) { typeface = std::move (tf); }
private:
    juce::Typeface::Ptr typeface;
};

// ----------------------------------------------------------------------------
//  超链接标签：带下划线的 iisaacbeats.cn
// ----------------------------------------------------------------------------
class HyperlinkLabel : public juce::Component
{
public:
    HyperlinkLabel (const juce::String& text, const juce::URL& url);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void setTypeface (juce::Typeface::Ptr tf) { typeface = std::move (tf); repaint(); }
    void setFontHeight (float h) { fontHeight = h; repaint(); }
private:
    juce::String text;
    juce::URL    url;
    juce::Typeface::Ptr typeface;
    float fontHeight = 16.0f;
    bool  hovering = false;
};

// ----------------------------------------------------------------------------
//  ImageBox：用户可在频谱内拖动/缩放的图片选择框
//   - 单击空白时弹出 FileChooser
//   - 已选图：显示二值化 + 填充后的灰阶预览
//   - 拖动框体（除四角外）：移动位置（仅垂直影响频率范围；水平不影响 mask）
//   - 拖动右下角 handle：等比缩放
// ----------------------------------------------------------------------------
class ImageBoxComponent : public juce::Component
{
public:
    ImageBoxComponent();

    // 设置当前图片（从文件加载并预处理）
    bool loadImageFromFile (const juce::File& file);
    bool hasImage() const { return sourceImage.isValid(); }
    juce::String getImagePath() const { return imagePath; }

    // 把当前图片渲染成 mask（rows = numFreqBins, cols 根据 contentBounds 宽度）
    // 1.0 = 形状内, 0.0 = 形状外
    std::vector<float> generateMask (int numFreqBins, int numCols) const;

    // 当前框在父组件（频谱区）内的归一化矩形（x/y/w/h 都 ∈ [0,1]）
    juce::Rectangle<float> getNormalisedBounds() const { return normRect; }
    void setNormalisedBounds (juce::Rectangle<float> n) { normRect = clampNormRect (n); resizedFromNorm(); }

    // 父组件传入：内容区（不含左侧刻度）的实际像素矩形
    void setContentBounds (juce::Rectangle<int> contentRect);

    // 字体注入
    void setTypeface (juce::Typeface::Ptr tf) { typeface = std::move (tf); repaint(); }

    // 选中/释放回调（让父组件保存到 Processor 状态）
    std::function<void()> onChanged;
    std::function<void(const juce::File&)> onImagePicked;

    void paint (juce::Graphics&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;

private:
    enum class DragMode { None, Move, ResizeBR };

    static juce::Rectangle<float> clampNormRect (juce::Rectangle<float> r);
    void resizedFromNorm();
    void preprocessImage();   // 生成二值化预览
    void openFileChooser();
    // 加载新图后/contentBounds 改变后：把 normRect 调整为图片宽高比
    // （以当前 normRect.height 为基准反推 width；若超界则反过来）
    void fitNormRectToImageAspect();
    juce::Rectangle<int> getResizeHandle() const;
    DragMode hitTest (juce::Point<int> p) const;

    juce::Rectangle<int>    contentBounds;       // 父组件内容区（像素）
    juce::Rectangle<float>  normRect { 0.30f, 0.20f, 0.40f, 0.50f };  // 归一化

    juce::Image  sourceImage;           // 原图（彩色）
    juce::Image  binaryPreview;         // 二值化预览（灰度）
    juce::String imagePath;
    float        binaryThreshold = 0.5f;

    DragMode dragMode = DragMode::None;
    juce::Point<int>       dragStartPos;
    juce::Rectangle<float> dragStartNormRect;

    juce::Typeface::Ptr typeface;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageBoxComponent)
};

// ============================================================================
//  Editor
// ============================================================================
class SpectrumTagAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public  juce::FileDragAndDropTarget,
                                        private juce::Timer
{
public:
    explicit SpectrumTagAudioProcessorEditor (SpectrumTagAudioProcessor&);
    ~SpectrumTagAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- 外部文件拖入支持 ----
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;

private:
    void timerCallback() override;
    void onPrintClicked();
    void persistEditorStateToProcessor();
    void restoreEditorStateFromProcessor();

    SpectrumTagAudioProcessor& processor;

    // ---- 全局字体与外观 ----
    juce::Typeface::Ptr     basementTypeface;
    SpectrumTagLookAndFeel  lookAndFeel;

    // ---- 顶部 ----
    juce::Label                       titleLabel;       // "SpectrumTag"
    juce::Label                       versionLabel;     // "v1.0.0"
    std::unique_ptr<HyperlinkLabel>   websiteLabel;     // iisaacbeats.cn

    // ---- 频谱视图（占位，Phase 3 实现）----
    class SpectrumView;
    std::unique_ptr<SpectrumView> spectrumView;

    // ---- 右侧控制面板 ----
    juce::Label    fftSizeLabel    { {}, "FFT Size:" };
    juce::Label    fftScaleLabel   { {}, "FFT scale:" };
    juce::Label    speedLabel      { {}, "Speed:" };
    juce::Label    amplitudeLabel  { {}, "Amplitude Ratio:" };
    juce::Label    invertLabel     { {}, "Invert:" };

    juce::ComboBox fftSizeCombo;
    juce::ComboBox fftScaleCombo;
    juce::Slider   speedSlider;
    juce::Slider   amplitudeSlider;
    juce::ToggleButton invertToggle;
    RoundPrintButton   printButton { "Print" };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment>  fftSizeAttachment;
    std::unique_ptr<ComboAttachment>  fftScaleAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment;
    std::unique_ptr<SliderAttachment> amplitudeAttachment;
    std::unique_ptr<ButtonAttachment> invertAttachment;

    // ---- 等比缩放约束 ----
    juce::ComponentBoundsConstrainer resizeConstrainer;

    // ---- 工具：给 Label 应用 BasementGrotesque 字体 ----
    void styleHeaderLabel (juce::Label& l, float height, juce::Colour colour);
    void styleControlLabel (juce::Label& l);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumTagAudioProcessorEditor)
};