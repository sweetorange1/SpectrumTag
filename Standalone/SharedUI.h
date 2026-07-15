#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <functional>

// ============================================================================
//  SharedUI —— Standalone 用的 UI 组件集合
// ----------------------------------------------------------------------------
//  这里的组件与 VST3 [PluginEditor.h] 里的定义在视觉上保持完全一致，
//  但完全独立于 juce::AudioProcessor，可以在 juce_add_gui_app 目标里直接使用。
//
//  组件清单（对应 UI.png）：
//    - StandaloneLookAndFeel    深色主题 + BasementGrotesque 字体
//    - RoundPrintButton         右下角白色大圆 Print 按钮
//    - HyperlinkLabel           顶部黄色悬停下划线链接
//    - ImageBoxComponent        频谱内可拖动/缩放的图片选择框（含二值化预处理）
// ============================================================================

namespace SharedColours
{
    const juce::Colour kBgColour            { 0xff1a1c1f };
    const juce::Colour kPanelColour         { 0xff111315 };
    const juce::Colour kSpectrogramBgColour { 0xff000000 };
    const juce::Colour kTextWhite           { 0xfff2f2f2 };
    const juce::Colour kTextSub             { 0xffbfbfbf };
    const juce::Colour kComboBg             { 0xffe9e9e9 };
    const juce::Colour kComboBorder         { 0xff222222 };
    const juce::Colour kSliderTrack         { 0xff7a7a7a };
    const juce::Colour kSliderThumb         { 0xfff2f2f2 };
    const juce::Colour kPrintBg             { 0xfff5f5f5 };
    const juce::Colour kPrintText           { 0xff111111 };
    const juce::Colour kLinkColour          { 0xfff2f2f2 };
    const juce::Colour kLinkHover           { 0xffe4f24a };
    const juce::Colour kImgBoxBg            { 0xffd4d4d4 };
    const juce::Colour kImgBoxYellow        { 0xffe4f24a };
    const juce::Colour kImgBoxBlack         { 0xff111111 };
    const juce::Colour kPianoWhiteKey       { 0xfff0f0f0 };
    const juce::Colour kPianoBlackKey       { 0xff202020 };
    const juce::Colour kAxisColour          { 0xff9a9a9a };
    const juce::Colour kAxisBgColour        { 0xff141517 };
}

// ---- 频率映射工具（linear 模式，Standalone 只用 linear，因为 UI 上没暴露 mel 也够用；
//  但为了保持与插件一致，也保留 log 版本，允许 UI 切换）----
namespace FreqMap
{
    constexpr float kMinDisplayHz = 20.0f;
    constexpr float kMaxDisplayHz = 22000.0f;

    inline float getDisplayTopHz (float nyquistHz)
    {
        return juce::jmax (kMinDisplayHz + 1.0f, juce::jmin (kMaxDisplayHz, nyquistHz));
    }
    inline float frequencyToYNormLinear (float hz, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float t = (juce::jlimit (kMinDisplayHz, topHz, hz) - kMinDisplayHz)
                        / (topHz - kMinDisplayHz);
        return juce::jlimit (0.0f, 1.0f, 1.0f - t);
    }
    inline float yNormToFrequencyLinear (float yNorm, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f - yNorm);
        return kMinDisplayHz + t * (topHz - kMinDisplayHz);
    }
    inline float frequencyToYNormLog (float hz, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        if (hz <= kMinDisplayHz) return 1.0f;
        if (hz >= topHz) return 0.0f;
        const float a = std::log (kMinDisplayHz);
        const float b = std::log (topHz);
        const float t = (std::log (hz) - a) / (b - a);
        return juce::jlimit (0.0f, 1.0f, 1.0f - t);
    }
    inline float yNormToFrequencyLog (float yNorm, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float a = std::log (kMinDisplayHz);
        const float b = std::log (topHz);
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f - yNorm);
        return std::exp (a + t * (b - a));
    }
}

// ----------------------------------------------------------------------------
class StandaloneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StandaloneLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr typeface;
};

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
//  ImageBoxComponent（Standalone 版）
//   与插件版行为一致：
//   - 单击（未选图时）弹出 FileChooser
//   - 拖动主体：移动位置
//   - 拖动右下角 handle：等比缩放
// ----------------------------------------------------------------------------
class ImageBoxComponent : public juce::Component
{
public:
    ImageBoxComponent();

    bool loadImageFromFile (const juce::File& file);
    bool hasImage() const { return sourceImage.isValid(); }
    juce::String getImagePath() const { return imagePath; }

    // 与插件版完全一致的 mask 生成
    std::vector<float> generateMask (int numFreqBins, int numCols) const;

    juce::Rectangle<float> getNormalisedBounds() const { return normRect; }
    void setNormalisedBounds (juce::Rectangle<float> n) { normRect = clampNormRect (n); resizedFromNorm(); }

    void setContentBounds (juce::Rectangle<int> contentRect);

    void setTypeface (juce::Typeface::Ptr tf) { typeface = std::move (tf); repaint(); }

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
    void preprocessImage();
    void openFileChooser();
    void fitNormRectToImageAspect();
    juce::Rectangle<int> getResizeHandle() const;
    DragMode hitTest (juce::Point<int> p) const;

    juce::Rectangle<int>    contentBounds;
    juce::Rectangle<float>  normRect { 0.30f, 0.20f, 0.40f, 0.50f };

    juce::Image  sourceImage;
    juce::Image  binaryPreview;
    juce::String imagePath;
    float        binaryThreshold = 0.5f;

    DragMode dragMode = DragMode::None;
    juce::Point<int>       dragStartPos;
    juce::Rectangle<float> dragStartNormRect;

    juce::Typeface::Ptr typeface;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageBoxComponent)
};
