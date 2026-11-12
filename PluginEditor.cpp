#include "shared/PrintPreview.h"
#include "shared/TimeScale.h"
#include "shared/PlatformFonts.h"
#include <JuceHeader.h>
#include "PluginEditor.h"
#include "shared/ImageMask.h"
#include "PluginProcessor.h"
#include <cmath>

// ============================================================================
//  SpectrumTag - Editor 实现（Phase 2 ~ 6 完整版）
// ============================================================================

namespace
{
    // 默认窗口尺寸
    constexpr int kDefaultWidth  = 1240;
    constexpr int kDefaultHeight = 680;
    constexpr int kMinWidth      = 800;
    constexpr int kMinHeight     = 440;

    // 调色板（取自 UI.png）
    const juce::Colour kBgColour       { 0xff1a1c1f };
const juce::Colour kPanelColour    { 0xff111315 };
// 频谱图（瀑布图）专用背景：纯黑，确保未绘制频谱数据时为黑底而非面板灰底。
const juce::Colour kSpectrogramBgColour { 0xff000000 };
    const juce::Colour kTextWhite      { 0xfff2f2f2 };
    const juce::Colour kTextSub        { 0xffbfbfbf };
    const juce::Colour kComboBg        { 0xffe9e9e9 };
    const juce::Colour kComboBorder    { 0xff222222 };
    const juce::Colour kSliderTrack    { 0xff7a7a7a };
    const juce::Colour kSliderThumb    { 0xfff2f2f2 };
    const juce::Colour kPrintBg        { 0xfff5f5f5 };
    const juce::Colour kPrintText      { 0xff111111 };
    const juce::Colour kLinkColour     { 0xfff2f2f2 };
    const juce::Colour kLinkHover      { 0xffe4f24a };
    const juce::Colour kImgBoxBg       { 0xffd4d4d4 };  // 图片框背景灰
    const juce::Colour kImgBoxYellow   { 0xffe4f24a };
    const juce::Colour kImgBoxBlack    { 0xff111111 };
    const juce::Colour kPianoWhiteKey  { 0xfff0f0f0 };
    const juce::Colour kPianoBlackKey  { 0xff202020 };
    const juce::Colour kAxisColour     { 0xff9a9a9a };
    const juce::Colour kAxisBgColour   { 0xff141517 };
    const juce::Colour kButtonBg       { 0xff23282b };
    const juce::Colour kButtonText     { 0xfff2f2f2 };
    const juce::Colour kProgressTrack  { 0xff2b3033 };

    inline bool containsWideCharacters (const juce::String& text)
    {
        for (int i = 0; i < text.length(); ++i)
            if ((juce::juce_wchar) text[i] >= 0x2e80)
                return true;
        return false;
    }

    constexpr int kFreqAxisWidth = 70;   // 左侧频率刻度宽度（mel 模式包含钢琴键）
    constexpr int kPianoWidth    = 28;   // mel 模式下钢琴键宽度

    // 显示频率范围固定为 20Hz ~ 22000Hz（同时受 Nyquist 上限约束）
    constexpr float kMinDisplayHz = 20.0f;
    constexpr float kMaxDisplayHz = 22000.0f;

    inline float frequencyToYNorm_Log(float hz,float topHz) {
        return spectrumtag::frequency::toY(hz,topHz,1);
    }

}

// ============================================================================
//  SpectrumTagLookAndFeel
// ============================================================================
SpectrumTagLookAndFeel::SpectrumTagLookAndFeel()
{
    typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BasementGrotesqueBlack_v1_202_otf,
        BinaryData::BasementGrotesqueBlack_v1_202_otfSize);

    // 解析一次并缓存：getTypefaceForFont 之后可能成为全局默认，
    // 在内部再构造 Font 会重新进入本函数（栈溢出）。
    chineseTypeface = spectrumtag::ui::chineseTypeface();

    setColour (juce::Label::textColourId,           kTextWhite);
    setColour (juce::Slider::backgroundColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId,         kSliderTrack);
    setColour (juce::Slider::thumbColourId,         kSliderThumb);
    setColour (juce::ComboBox::backgroundColourId,  kComboBg);
    setColour (juce::ComboBox::textColourId,        juce::Colours::black);
    setColour (juce::ComboBox::outlineColourId,     kComboBorder);
    setColour (juce::ComboBox::arrowColourId,       juce::Colours::black);
    setColour (juce::PopupMenu::backgroundColourId, kComboBg);
    setColour (juce::PopupMenu::textColourId,       juce::Colours::black);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffd0d0d0));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId,    kTextWhite);
    setColour (juce::ToggleButton::tickColourId,    kTextWhite);

    // 扁平按钮与进度条：与深色面板同一套语言，避免 JUCE 默认渐变外观。
    setColour (juce::TextButton::buttonColourId,    kButtonBg);
    setColour (juce::TextButton::buttonOnColourId,  kPrintBg);
    setColour (juce::TextButton::textColourOffId,   kButtonText);
    setColour (juce::TextButton::textColourOnId,    kPrintText);
    setColour (juce::Slider::textBoxBackgroundColourId, kComboBg);
    setColour (juce::Slider::textBoxTextColourId,   juce::Colours::black);
    setColour (juce::Slider::textBoxOutlineColourId, kComboBorder);
    setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xffc8c8c8));
    setColour (juce::ProgressBar::backgroundColourId, kProgressTrack);
    setColour (juce::ProgressBar::foregroundColourId, kTextWhite);
}

juce::Typeface::Ptr SpectrumTagLookAndFeel::getTypefaceForFont (const juce::Font&)
{
    // 直接返回缓存字体：本对象可能是全局默认 LookAndFeel，
    // 若在此构造 Font 并解析字形会重新进入本函数。
    if (chinese && chineseTypeface != nullptr) return chineseTypeface;
    return typeface;
}

juce::Font SpectrumTagLookAndFeel::getUiFont (float height) const
{
    if (chinese)
        return spectrumtag::ui::chineseFont (height);
    return juce::Font (typeface).withHeight (height);
}

juce::Font SpectrumTagLookAndFeel::getPopupMenuFont()
{
    if (chinese)
        return spectrumtag::ui::chineseFont (15.0f);
    return juce::Font (typeface).withHeight (15.0f);
}

void SpectrumTagLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPos, float, float,
                                                const juce::Slider::SliderStyle, juce::Slider& s)
{
    const float trackY  = y + height * 0.5f;
    const float trackX0 = (float) x + 2.0f;
    const float trackX1 = (float) (x + width) - 2.0f;

    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.drawLine (trackX0, trackY, trackX1, trackY, 1.5f);

    const float thumbDiameter = 12.0f;
    const float thumbX = juce::jlimit (trackX0, trackX1, sliderPos) - thumbDiameter * 0.5f;
    const float thumbY = trackY - thumbDiameter * 0.5f;
    g.setColour (s.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (thumbX, thumbY, thumbDiameter, thumbDiameter);
}

void SpectrumTagLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                            int buttonX, int buttonY, int buttonW, int buttonH,
                                            juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 2.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) (buttonX + buttonW * 0.5f);
    const float cy = (float) (buttonY + buttonH * 0.5f);
    const float aw = 8.0f, ah = 5.0f;
    arrow.addTriangle (cx - aw * 0.5f, cy - ah * 0.5f,
                       cx + aw * 0.5f, cy - ah * 0.5f,
                       cx,             cy + ah * 0.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font SpectrumTagLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    const float height = juce::jmin (15.0f, box.getHeight() * 0.7f);
    if (chinese) return spectrumtag::ui::chineseFont (height);
    return juce::Font (typeface).withHeight (height);
}

void SpectrumTagLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId));
}

void SpectrumTagLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                                bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float dotSize = juce::jmin (14.0f, r.getHeight() - 2.0f);
    juce::Rectangle<float> dot (0.0f, (r.getHeight() - dotSize) * 0.5f, dotSize, dotSize);

    g.setColour (b.findColour (juce::ToggleButton::tickColourId));
    if (b.getToggleState())
        g.fillEllipse (dot);
    else
        g.drawEllipse (dot.reduced (1.0f), 1.5f);
}

// ----------------------------------------------------------------------------
//  TextButton：与工具栏圆角色块一致，默认不再使用 JUCE 的渐变/描边外观。
//  "spectrumTagStyle"：primary（白底黑字，主操作）/ ghost（无底色无描边，纯文字入口）
//  / outline（只有描边、不填充，填充色即父组件底色，工具按钮用）
// ----------------------------------------------------------------------------
void SpectrumTagLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour& backgroundColour,
                                                   bool isOver, bool isDown)
{
    const auto style   = button.getProperties()["spectrumTagStyle"].toString();
    const bool primary = style == "primary";
    const bool ghost   = style == "ghost";

    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jlimit (5.0f, 14.0f, bounds.getHeight() * 0.12f);

    if (ghost)
    {
        if (isOver || isDown)
        {
            g.setColour (juce::Colours::white.withAlpha (isDown ? 0.10f : 0.06f));
            g.fillRoundedRectangle (bounds, radius);
        }
        return;
    }

    //   "outline"：只有描边、不填充，底色透出父组件背景。
    //   语言切换 / 导出诊断 / 音频设备三个工具按钮统一用它。
    if (style == "outline")
    {
        if (isOver || isDown)
        {
            g.setColour (juce::Colours::white.withAlpha (isDown ? 0.10f : 0.06f));
            g.fillRoundedRectangle (bounds, radius);
        }
        const float alpha = !button.isEnabled() ? 0.12f : ((isOver || isDown) ? 0.42f : 0.26f);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
        return;
    }

    if (style == "round")
    {
        // 圆形主按钮（右下角导出），沿用原 Print 圆按钮的白底视觉。
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        juce::Rectangle<float> circle (bounds.getCentreX() - diameter * 0.5f,
                                       bounds.getCentreY() - diameter * 0.5f,
                                       diameter, diameter);
        juce::Colour fill = juce::Colour (0xfff5f5f5);
        if (! button.isEnabled())   fill = fill.withMultipliedAlpha (0.40f);
        else if (isDown)            fill = juce::Colour (0xffd6d6d6);
        else if (isOver)            fill = juce::Colours::white;
        g.setColour (fill);
        g.fillEllipse (circle);
        return;
    }

    juce::Colour fill = primary ? juce::Colour (0xfff5f5f5) : backgroundColour;
    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.40f);
    else if (isDown)
        fill = primary ? juce::Colour (0xffd6d6d6) : fill.brighter (0.16f);
    else if (isOver)
        fill = primary ? juce::Colours::white : fill.brighter (0.10f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    if (! primary)
    {
        g.setColour (juce::Colours::white.withAlpha (button.isEnabled() ? 0.08f : 0.04f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

void SpectrumTagLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                             bool isOver, bool)
{
    const auto style   = button.getProperties()["spectrumTagStyle"].toString();
    const bool primary = style == "primary";
    const bool ghost   = style == "ghost";
    const bool round   = style == "round";
    const bool outline = style == "outline";

    auto colour = button.findColour (button.getToggleState()
                                         ? juce::TextButton::textColourOnId
                                         : juce::TextButton::textColourOffId);

    if ((primary || round) && ! button.getToggleState())
        colour = kPrintText;
    else if (ghost || outline)
        colour = (isOver && button.isEnabled()) ? kTextWhite : kTextSub;

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (5, 1),
                      juce::Justification::centred, 1, 0.78f);
}

juce::Font SpectrumTagLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    // 大按钮（如右下角 128px 的导出按钮）使用成比例的大字号，普通工具栏按钮保持小字。
    const float height = buttonHeight > 60 ? juce::jlimit (20.0f, 36.0f, (float) buttonHeight * 0.28f)
                                           : juce::jlimit (11.0f, 15.0f, (float) buttonHeight * 0.52f);

    if (chinese || containsWideCharacters (button.getButtonText()))
        return spectrumtag::ui::chineseFont (height);

    return juce::Font (typeface).withHeight (height);
}

juce::Label* SpectrumTagLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    // 与 ComboBox 相同的浅灰数值块，替换 JUCE 默认的深色描边块。
    label->setColour (juce::Label::backgroundColourId, kComboBg);
    label->setColour (juce::Label::textColourId, juce::Colours::black);
    label->setColour (juce::Label::outlineColourId, kComboBorder);
    label->setColour (juce::TextEditor::backgroundColourId, kComboBg);
    label->setColour (juce::TextEditor::textColourId, juce::Colours::black);
    label->setColour (juce::TextEditor::outlineColourId, kComboBorder);
    label->setFont (getUiFont (14.0f));
    label->setJustificationType (juce::Justification::centred);

    return label;
}

void SpectrumTagLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar,
                                              int width, int height, double progress,
                                              const juce::String& textToShow)
{
    const float textWidth = textToShow.isEmpty() ? 0.0f : 54.0f;
    auto track = juce::Rectangle<float> (0.0f, 0.0f, (float) width - textWidth, (float) height)
                     .withSizeKeepingCentre ((float) width - textWidth, 8.0f);

    g.setColour (bar.findColour (juce::ProgressBar::backgroundColourId));
    g.fillRoundedRectangle (track, 4.0f);

    const auto filled = track.withWidth (track.getWidth() * (float) juce::jlimit (0.0, 1.0, progress));
    if (filled.getWidth() > 1.0f)
    {
        g.setColour (bar.findColour (juce::ProgressBar::foregroundColourId));
        g.fillRoundedRectangle (filled, 4.0f);
    }

    if (textToShow.isNotEmpty())
    {
        g.setColour (kTextWhite);
        g.setFont (chinese ? spectrumtag::ui::chineseFont (14.0f)
                           : juce::Font (typeface).withHeight (14.0f));
        g.drawText (textToShow, width - (int) textWidth, 0, (int) textWidth, height,
                    juce::Justification::centredRight, false);
    }
}

// ============================================================================
//  TransportButton / TrashButton
// ============================================================================
namespace {
    void paintIconChip (juce::Graphics& g, juce::Button& button, bool isOver, bool isDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = juce::jmin (5.0f, bounds.getHeight() * 0.3f);

        auto fill = kButtonBg;
        if (! button.isEnabled())  fill = fill.withMultipliedAlpha (0.40f);
        else if (isDown)           fill = fill.brighter (0.16f);
        else if (isOver)           fill = fill.brighter (0.10f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (juce::Colours::white.withAlpha (button.isEnabled() ? 0.08f : 0.04f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    juce::Colour iconColour (const juce::Button& button)
    {
        return juce::Colours::white.withAlpha (button.isEnabled() ? 1.0f : 0.35f);
    }
}

TransportButton::TransportButton() : juce::Button ("Play / Pause")
{
    setComponentID ("transportPlay");
}

void TransportButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    // 独立版播放按钮不带底框边框，仅绘制图标，悬停/按下用透明度反馈。
    auto area = getLocalBounds().toFloat();
    const float iconSize = juce::jmin (area.getHeight() * 0.62f, area.getWidth() * 0.58f);
    auto icon = juce::Rectangle<float> (iconSize, iconSize).withCentre (area.getCentre());

    const float alpha = isEnabled() ? (isDown ? 0.75f : isOver ? 1.0f : 0.9f) : 0.35f;
    g.setColour (juce::Colours::white.withAlpha (alpha));

    if (playing)
    {
        const float barW = icon.getWidth() * 0.28f;
        const float gap = icon.getWidth() * 0.2f;
        const float total = barW * 2.0f + gap;
        const float x0 = icon.getCentreX() - total * 0.5f;
        g.fillRoundedRectangle (x0, icon.getY(), barW, icon.getHeight(), 1.2f);
        g.fillRoundedRectangle (x0 + barW + gap, icon.getY(), barW, icon.getHeight(), 1.2f);
    }
    else
    {
        // Slightly inset on the left so the triangle looks optically centred.
        auto triangle = icon.reduced (icon.getWidth() * 0.08f, 0.0f);
        juce::Path path;
        path.addTriangle (triangle.getX(), triangle.getY(),
                          triangle.getX(), triangle.getBottom(),
                          triangle.getRight(), triangle.getCentreY());
        g.fillPath (path);
    }
}

TrashButton::TrashButton() : juce::Button ("Clear")
{
    setComponentID ("trashClear");
}

void TrashButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    paintIconChip (g, *this, isOver, isDown);

    auto area = getLocalBounds().toFloat();
    const float size = juce::jmin (area.getHeight() * 0.52f, area.getWidth() * 0.42f);
    auto icon = juce::Rectangle<float> (size * 0.82f, size).withCentre (area.getCentre());

    g.setColour (iconColour (*this));
    const float stroke = 1.5f;

    // Lid + handle.
    const float lidY = icon.getY() + icon.getHeight() * 0.20f;
    g.drawLine (icon.getX(), lidY, icon.getRight(), lidY, stroke);
    g.drawLine (icon.getX() + icon.getWidth() * 0.34f, lidY - icon.getHeight() * 0.12f,
                icon.getX() + icon.getWidth() * 0.66f, lidY - icon.getHeight() * 0.12f, stroke);
    g.drawLine (icon.getX() + icon.getWidth() * 0.34f, lidY - icon.getHeight() * 0.12f,
                icon.getX() + icon.getWidth() * 0.34f, lidY, stroke);
    g.drawLine (icon.getX() + icon.getWidth() * 0.66f, lidY - icon.getHeight() * 0.12f,
                icon.getX() + icon.getWidth() * 0.66f, lidY, stroke);

    // Body.
    const float top = lidY + stroke;
    const float bottom = icon.getBottom() - icon.getHeight() * 0.04f;
    const float inset = icon.getWidth() * 0.14f;
    juce::Path body;
    body.startNewSubPath (icon.getX() + inset, top);
    body.lineTo (icon.getX() + inset * 1.5f, bottom);
    body.lineTo (icon.getRight() - inset * 1.5f, bottom);
    body.lineTo (icon.getRight() - inset, top);
    g.strokePath (body, juce::PathStrokeType (stroke));

    // Two ribs.
    const float ribTop = top + (bottom - top) * 0.24f;
    const float ribBottom = bottom - (bottom - top) * 0.18f;
    g.drawLine (icon.getCentreX() - icon.getWidth() * 0.16f, ribTop,
                icon.getCentreX() - icon.getWidth() * 0.16f, ribBottom, stroke);
    g.drawLine (icon.getCentreX() + icon.getWidth() * 0.16f, ribTop,
                icon.getCentreX() + icon.getWidth() * 0.16f, ribBottom, stroke);
}

// ============================================================================
//  RoundPrintButton
// ============================================================================
RoundPrintButton::RoundPrintButton (const juce::String& name) : juce::Button (name) {}

void RoundPrintButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    // 圆角矩形 + 深色描边：明确是一个可点的按钮，而不是频谱上的一个圆点。
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = juce::jmin (r.getHeight(), r.getWidth()) * 0.30f;

    juce::Colour bg = kPrintBg;
    if (! isEnabled())  bg = juce::Colour (0xff7a7a7a);
    else if (isDown)    bg = juce::Colour (0xffd0d0d0);
    else if (isOver)    bg = juce::Colours::white;

    g.setColour (bg);
    g.fillRoundedRectangle (r, corner);

    const float borderAlpha = isEnabled() ? (isOver || isDown ? 1.0f : 0.85f) : 0.45f;
    g.setColour (kPrintText.withAlpha (borderAlpha));
    g.drawRoundedRectangle (r.reduced (0.75f), corner, 1.6f);

    juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
    f = f.withHeight (r.getHeight() * 0.46f * typeScale);
    g.setFont (f);
    g.setColour (kPrintText);
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

// ============================================================================
//  HyperlinkLabel
// ============================================================================
HyperlinkLabel::HyperlinkLabel (const juce::String& t, const juce::URL& u)
    : text (t), url (u)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HyperlinkLabel::paint (juce::Graphics& g)
{
    juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
    f = f.withHeight (fontHeight);
    g.setFont (f);
    g.setColour (hovering ? kLinkHover : kLinkColour);

    auto bounds = getLocalBounds();
    g.drawText (text, bounds, juce::Justification::centredLeft, false);

    const float textWidth = juce::GlyphArrangement::getStringWidth (f, text);
    const float underlineY = (float) bounds.getCentreY() + fontHeight * 0.42f;
    g.drawLine (0.0f, underlineY, textWidth, underlineY, 1.5f);
}

void HyperlinkLabel::mouseDown (const juce::MouseEvent&) { url.launchInDefaultBrowser(); }
void HyperlinkLabel::mouseEnter (const juce::MouseEvent&) { hovering = true;  repaint(); }
void HyperlinkLabel::mouseExit  (const juce::MouseEvent&) { hovering = false; repaint(); }

// ============================================================================
//  ImageBoxComponent
// ============================================================================
ImageBoxComponent::ImageBoxComponent()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ImageBoxComponent::clampNormRect(juce::Rectangle<float> r){
    // Full plot width is available at every Speed. Keep a grab area visible at
    // the horizontal edges, while frequency placement stays in the plot.
    r.setX(juce::jlimit(-r.getWidth()+.02f,.98f,r.getX()));
    r.setY(juce::jlimit(0.0f,juce::jmax(0.0f,1-r.getHeight()),r.getY()));return r;
}
void ImageBoxComponent::setContentBounds(juce::Rectangle<int> r){
    if(r==contentBounds)return;contentBounds=r;fitNormRectToImageAspect();resizedFromNorm();
}
void ImageBoxComponent::resizedFromNorm(){
    if(contentBounds.isEmpty())return;
    const int h=juce::jmax(1,juce::roundToInt(normRect.getHeight()*contentBounds.getHeight()));
    const int w=hasImage()?juce::jmax(1,juce::roundToInt(h*aspect())):juce::jmax(1,juce::roundToInt(normRect.getWidth()*contentBounds.getWidth()));
    setBounds(contentBounds.getX()+juce::roundToInt(normRect.getX()*contentBounds.getWidth()),contentBounds.getY()+juce::roundToInt(normRect.getY()*contentBounds.getHeight()),w,h);
}
bool ImageBoxComponent::loadImageFromFile (const juce::File& file)
{
    auto img = juce::ImageFileFormat::loadFrom (file);
    if (! img.isValid()) return false;
    sourceImage = img;
    imagePath   = file.getFullPathName();
    preprocessImage();
    fitNormRectToImageAspect();   // 选框立即匹配图片宽高比
    resizedFromNorm();
    repaint();
    if (onChanged) onChanged();
    return true;
}

// 加载新图后调用：以当前 normRect 中心为锚，按图片像素宽高比调整 normRect 的宽/高
// 这样用户不需要再手动拖动 handle，选框就会跟图片对齐
void ImageBoxComponent::fitNormRectToImageAspect(){
    if(contentBounds.isEmpty())return;
    const auto centre=normRect.getCentre();
    const float ratio=aspect(),w=(float)contentBounds.getWidth(),h=(float)contentBounds.getHeight();
    float height=juce::jmax(1.0f,normRect.getHeight()*h),width=height*ratio;
    const float factor=juce::jmin(1.0f,juce::jmin(.98f*w/width,.98f*h/height));width*=factor;height*=factor;
    normRect=clampNormRect({centre.x-width/w*.5f,centre.y-height/h*.5f,width/w,height/h});
}
void ImageBoxComponent::preprocessImage(){
    binaryMask=spectrumtag::image::binarize(sourceImage,{conversionMode,conversionAmount});
    requestedConversion=appliedConversion=0;
}
void ImageBoxComponent::setConversion(int mode,float amount,bool inverted,float ratio){
    const bool changed=mode!=conversionMode||std::abs(amount-conversionAmount)>.000001f;
    if(!changed&&inverted==previewInverted&&std::abs(ratio-previewRatio)<.000001f)return;
    conversionMode=mode;conversionAmount=amount;previewInverted=inverted;previewRatio=ratio;
    if(changed&&hasImage())requestedConversion=converter.request(sourceImage,mode,amount);
    // Gain/Invert are inexpensive paint colours, not a rebuilt bitmap.
    repaint();
}
bool ImageBoxComponent::refreshConversion(){
    const auto result=converter.result();
    if(result&&requestedConversion!=0&&result->generation>appliedConversion&&result->generation<=requestedConversion
        &&result->source==sourceImage&&result->mode==conversionMode){
        binaryMask=result->mask;appliedConversion=result->generation;repaint();return true;
    }
    return false;
}
std::vector<float> ImageBoxComponent::generateMask(int rows,int cols,int scale,float low,float high,bool completedPreview) const
{
    const auto mask=(completedPreview||conversionReady())?binaryMask:spectrumtag::image::binarize(sourceImage,{conversionMode,conversionAmount});
    return spectrumtag::image::rasterize(mask,rows,cols,scale,low,high);
}

juce::Rectangle<int> ImageBoxComponent::getResizeHandle() const
{
    const int s = juce::jlimit (10, 24, juce::jmin (getWidth(), getHeight()) / 6);
    return { getWidth() - s, getHeight() - s, s, s };
}

ImageBoxComponent::DragMode ImageBoxComponent::hitTest (juce::Point<int> p) const
{
    if (getResizeHandle().contains (p)) return DragMode::ResizeBR;
    if (getLocalBounds().contains (p))  return DragMode::Move;
    return DragMode::None;
}

void ImageBoxComponent::mouseMove (const juce::MouseEvent& e)
{
    auto m = hitTest (e.getPosition());
    if (m == DragMode::ResizeBR)      setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor);
    else if (m == DragMode::Move)     setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else                              setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ImageBoxComponent::mouseDown(const juce::MouseEvent& e){
    if(!e.mods.isLeftButtonDown())return;
    if(onGestureStart)onGestureStart();dragMode=hitTest(e.getPosition());dragStartPos=e.getPosition()+getPosition();dragStartNormRect=normRect;
    // Ctrl+拖动：原对象留在原位变成 Print，拖走的是它的副本。
    duplicatePending=e.mods.isCtrlDown()&&hasImage();
}
void ImageBoxComponent::mouseDrag(const juce::MouseEvent& e){
    if(externalDrag)return;  // 这一手势已由频谱视图转发，避免同一次拖动被应用两次
    dragToParentPoint(e.getPosition()+getPosition());
}
void ImageBoxComponent::dragToParentPoint(juce::Point<int> parentPoint){
    if(dragMode==DragMode::None||contentBounds.isEmpty())return;
    if(duplicatePending){
        // 越过阈值才复制，避免 Ctrl+单击的抖动多出一个对象。
        if((parentPoint-dragStartPos).getDistanceFromOrigin()<3.f)return;
        duplicatePending=false;
        const auto rect=dragStartNormRect;
        const bool resize=dragMode==DragMode::ResizeBR;
        if(onDuplicateDrag){
            onDuplicateDrag();
            // 副本摆回手势开始时的矩形，再按同一累计位移继续拖动。
            if(hasImage()){setPrintBounds(rect);dragStartNormRect=rect;dragMode=resize?DragMode::ResizeBR:DragMode::Move;}
        }
    }
    const auto delta=parentPoint-dragStartPos;
    auto next=dragStartNormRect;
    if(dragMode==DragMode::Move)next.setPosition(next.getX()+(float)delta.x/contentBounds.getWidth(),next.getY()+(float)delta.y/contentBounds.getHeight());
    else {
        const float ratio=aspect();const float startHeight=dragStartNormRect.getHeight()*contentBounds.getHeight();
        const float newHeight=juce::jmax(12.0f,startHeight+((float)delta.x/ratio+delta.y)*.5f);
        const float height=juce::jmin(newHeight,juce::jmin(contentBounds.getHeight()*.98f,contentBounds.getWidth()*.98f/ratio));
        next.setSize(height*ratio/contentBounds.getWidth(),height/contentBounds.getHeight());
    }
    normRect=clampNormRect(next);resizedFromNorm();if(onChanged)onChanged();
}
// ---- 由父视图接手的手势（已打印对象被拖动后立刻退回未打印态）----
void ImageBoxComponent::beginExternalDrag(juce::Point<int> parentPoint,bool resizeAtHandle){
    externalDrag=true;
    dragMode=resizeAtHandle?DragMode::ResizeBR:DragMode::Move;
    dragStartPos=parentPoint;dragStartNormRect=normRect;
}
void ImageBoxComponent::continueExternalDrag(juce::Point<int> parentPoint){dragToParentPoint(parentPoint);}
void ImageBoxComponent::endExternalDrag(){
    if(!externalDrag)return;
    externalDrag=false;dragMode=DragMode::None;
    if(onGestureEnd)onGestureEnd();
}
void ImageBoxComponent::mouseUp(const juce::MouseEvent& e){
    const bool wasEmpty=!hasImage();
    const bool moved=(e.getPosition()+getPosition()-dragStartPos).getDistanceFromOrigin()>=4.f;
    if(externalDrag){externalDrag=false;dragMode=DragMode::None;if(onGestureEnd)onGestureEnd();return;}
    dragMode=DragMode::None;
    if(onGestureEnd)onGestureEnd();
    // The empty box is the "drop a picture" guide; a plain click opens the
    // chooser instead of doing nothing. Dragging still repositions the guide.
    if(wasEmpty&&!moved&&e.mods.isLeftButtonDown())openFileChooser();
}
void ImageBoxComponent::mouseDoubleClick(const juce::MouseEvent& e){if(e.mods.isLeftButtonDown())openFileChooser();}

void ImageBoxComponent::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        chinese?juce::String::fromUTF8("选择图片"):juce::String("Choose picture"),
        juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            // 是否允许导入（未加载音频时拒绝）、是否需要先打印上一张由编辑器决定。
            if (file.existsAsFile() && onImageRequested) onImageRequested (file);
        });
}

void ImageBoxComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // 半透明灰底
    g.setColour (kImgBoxBg.withAlpha (0.55f));
    g.fillRect (r);

    if(hasImage()&&binaryMask.isValid()){
        const auto shade=(juce::uint8)juce::roundToInt(255*std::sqrt(juce::jlimit(0.0f,1.0f,previewRatio)));
        const juce::Colour dim(shade,shade,shade);
        g.setColour(previewInverted?dim:juce::Colours::white);g.fillRect(r);
        g.setColour(previewInverted?juce::Colours::white:dim);
        g.drawImage(binaryMask,r,juce::RectanglePlacement::centred,true);
    }
    else
    {
        // 空工作区：这个框就是"把图片放进来"的引导，拖入或单击/双击都可以。
        const float titleH = juce::jlimit (13.0f, 20.0f, r.getHeight() * 0.10f);
        const float subH   = juce::jmax (11.0f, titleH * 0.72f);
        const bool  roomy  = r.getHeight() >= 110.0f && r.getWidth() >= 150.0f;
        const float icon   = juce::jlimit (18.0f, 40.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.18f);

        auto content = r.reduced (10.0f, 8.0f);
        if (roomy)
        {
            auto iconArea = content.removeFromTop (juce::jmin (content.getHeight() * 0.42f, icon * 1.35f));
            auto frame = juce::Rectangle<float> (icon * 1.18f, icon).withCentre (iconArea.getCentre());
            g.setColour (kImgBoxBlack.withAlpha (0.80f));
            g.drawRoundedRectangle (frame.reduced (1.0f), 3.0f, 1.6f);
            g.fillEllipse (frame.getX() + frame.getWidth() * 0.18f, frame.getY() + frame.getHeight() * 0.20f,
                           frame.getWidth() * 0.15f, frame.getWidth() * 0.15f);
            juce::Path hill;
            hill.startNewSubPath (frame.getX() + frame.getWidth() * 0.12f, frame.getBottom() - frame.getHeight() * 0.16f);
            hill.lineTo (frame.getX() + frame.getWidth() * 0.38f, frame.getY() + frame.getHeight() * 0.48f);
            hill.lineTo (frame.getX() + frame.getWidth() * 0.56f, frame.getBottom() - frame.getHeight() * 0.30f);
            hill.lineTo (frame.getX() + frame.getWidth() * 0.72f, frame.getY() + frame.getHeight() * 0.58f);
            hill.lineTo (frame.getRight() - frame.getWidth() * 0.10f, frame.getBottom() - frame.getHeight() * 0.16f);
            g.strokePath (hill, juce::PathStrokeType (1.4f));
            content.removeFromTop (2.0f);
        }

        auto text = content.withSizeKeepingCentre (content.getWidth(),
            juce::jmin (content.getHeight(), titleH * 1.6f + (roomy ? subH * 1.7f : 0.0f)));
        juce::Font baseFont = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
        g.setFont (chinese ? spectrumtag::ui::chineseFont (titleH) : baseFont.withHeight (titleH));
        g.setColour (kImgBoxBlack);
        g.drawFittedText (chinese ? juce::String::fromUTF8 ("拖入图片") : juce::String ("Drop a picture"),
                          text.removeFromTop (juce::jmin (text.getHeight(), titleH * 1.6f)).toNearestInt(),
                          juce::Justification::centred, 1, 0.9f);
        if (roomy && text.getHeight() >= subH)
        {
            g.setFont (chinese ? spectrumtag::ui::chineseFont (subH) : baseFont.withHeight (subH));
            g.setColour (kImgBoxBlack.withAlpha (0.80f));
            g.drawFittedText (chinese ? juce::String::fromUTF8 ("或单击 / 双击此处选择")
                                      : juce::String ("or click / double-click to choose"),
                              text.removeFromTop (subH * 1.7f).toNearestInt(),
                              juce::Justification::centred, 1, 0.9f);
        }
    }

    // 黑黄交错虚线边框
    {
        juce::Path p;
        p.addRectangle (r.reduced (1.0f));
        // 黄色底虚线（更长 dash）
        const float yellowDashes[] = { 10.0f, 0.0f };
        g.setColour (kImgBoxYellow);
        juce::PathStrokeType (2.0f).createDashedStroke (p, p, yellowDashes, 2);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        // 黑色短虚线叠加
        g.setColour (kImgBoxBlack);
        const float blackDashes[] = { 6.0f, 6.0f };
        juce::Path p2;
        p2.addRectangle (r.reduced (1.0f));
        juce::PathStrokeType (2.0f).createDashedStroke (p2, p2, blackDashes, 2);
        g.strokePath (p2, juce::PathStrokeType (2.0f));
    }

    // 右下角缩放 handle 标识
    auto h = getResizeHandle().toFloat();
    g.setColour (kImgBoxBlack.withAlpha (0.6f));
    for (int i = 0; i < 3; ++i)
    {
        const float off = 4.0f + i * 4.0f;
        g.drawLine (h.getX() + off, h.getBottom() - 2.0f,
                    h.getRight() - 2.0f, h.getY() + off, 1.2f);
    }
}

// ============================================================================
//  SpectrumView：瀑布图 + 频率刻度 + 钢琴键 + 内嵌 ImageBox
// ============================================================================
class FitSpectrumButton final : public juce::Button {
public:
    FitSpectrumButton():juce::Button("Fit spectrum") {setComponentID("fitSpectrum");}
    void paintButton(juce::Graphics& g,bool over,bool down) override {
        auto r=getLocalBounds().toFloat().reduced(1);g.setColour(juce::Colour(over||down?0xf5485055:0xd51b2023));g.fillRoundedRectangle(r,5);
        g.setColour(juce::Colours::white.withAlpha(.9f));r=r.reduced(7);const float n=4;
        juce::Path p;
        p.startNewSubPath(r.getX()+n,r.getY());p.lineTo(r.getTopLeft());p.lineTo(r.getX(),r.getY()+n);
        p.startNewSubPath(r.getRight()-n,r.getY());p.lineTo(r.getTopRight());p.lineTo(r.getRight(),r.getY()+n);
        p.startNewSubPath(r.getX(),r.getBottom()-n);p.lineTo(r.getBottomLeft());p.lineTo(r.getX()+n,r.getBottom());
        p.startNewSubPath(r.getRight()-n,r.getBottom());p.lineTo(r.getBottomRight());p.lineTo(r.getRight(),r.getBottom()-n);
        g.strokePath(p,juce::PathStrokeType(1.6f));
    }
};
class SpectrumTagAudioProcessorEditor::SpectrumView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumView(SpectrumTagAudioProcessor& p,juce::Typeface::Ptr tf)
        : processor(p),typeface(std::move(tf)),history(p.getSpectrumHistory()) {
        setComponentID("spectrumView");setWantsKeyboardFocus(true);lastClearSerial=p.getSpectrumClearSerial();
        imageBox=std::make_unique<ImageBoxComponent>();imageBox->setTypeface(typeface);
        addAndMakeVisible(imageClip);imageClip.setInterceptsMouseClicks(false,true);imageClip.addAndMakeVisible(*imageBox);
        imageBox->addMouseListener(this,true);
        fitButton.onClick=[this]{if(onNavigation)onNavigation();resetView();};addAndMakeVisible(fitButton);
        printImageButton.setComponentID("imagePrint");printImageButton.setTypeface(typeface);
        printImageButton.onClick=[this]{if(onImagePrint)onImagePrint();};
        addAndMakeVisible(printImageButton);printImageButton.setVisible(false);
        setPalette(p.getEditorState().palette);
    }
    ~SpectrumView() override { imageBox->removeMouseListener(this); }
    ImageBoxComponent& getImageBox(){return *imageBox;}
    juce::Rectangle<int> getContentBounds() const{return contentBounds;}
    void setPalette(int value){previewImage={};palette=juce::jlimit(0,7,value);for(int i=0;i<256;++i)lut[(size_t)i]=spectrumtag::spectrumColour(palette,i/255.0f);dirty=true;repaint();}
    void setScaleMode(int value){if(value==3)value=1; // Retired Super Vision is the same frequency curve as Log.
        if(scaleMode!=value){
        scaleMode=value;getProperties().set("scaleMode",scaleMode);
        dirty=true;repaint();
    }}
    void matchPresetTime(double anchor){
        view.zoomX=1;fit=false;panActive=false;
        if(recorded)view.follow=false;
        view.endTime=anchor+(1.-getImageBoxX()/juce::jmax(1,contentBounds.getWidth()))*timeSpan();
        dirty=true;repaint();
    }
    // 图片对象三态：Empty=没有对象（有音频时显示投放引导）/ Placing=未打印
    // （图片可见，可拖动、可改参数）/ Printed=已打印未聚焦（只看到频谱里的挖空）
    // / Focused=已打印且聚焦（图片 + 边框 + Delete 提示）。
    enum class ImageStage { Empty, Placing, Printed, Focused };
    bool timelineHasPrints() const {const auto edits=processor.recording().edits();return edits&&!edits->empty();}
    ImageStage imageStage() const {
        // 状态3 不显示源图，只靠“选中的打印”判定。
        if(selectedPrint())return ImageStage::Focused;
        if(imageBox->hasImage())return ImageStage::Placing;
        return timelineHasPrints()?ImageStage::Printed:ImageStage::Empty;
    }
    bool canAcceptImage() const {return processor.hasLoadedAudio()||!processor.filePlayerMode;}
    // 状态切换后立刻刷新画布，而不是等下一次 tick。
    void refreshCanvas(){refreshCanvasState();}
    // Called when Print is refused because no real audio is loaded. Emphasise
    // the on-canvas guidance instead of silently doing nothing.
    void flashAudioHint(){audioHintFlashUntil=juce::Time::getMillisecondCounter()+2200;repaint(audioBannerBounds());}
    // 退回未打印态：这次打印马上由图片接替，绘制时只挖它自己那条频带。
    void markPrintRevealed(const juce::String& id){imageRevealId=id;}
    void refreshDeletedPrint(){
        // An in-flight raster may still refer to the deleted edit. Retire that
        // request now; paint masks the obsolete footprint until the new audio
        // snapshot arrives, even with a stopped transport and no timer tick yet.
        rasterEpoch=renderer.invalidate();requestedRaster=appliedRaster=0;dirty=true;repaint();
    }
    void clearDisplay(){
        rasterEpoch=renderer.invalidate();requestedRaster=appliedRaster=0;
        waterfall={};waterfallSource.reset();waterfallStart=waterfallEnd=0;overviewImage={};overviewSource.reset();
        preview.reset();previewImage={};selected={};dragEdit.reset();movingPrint=false;imageRevealId={};
        panActive=false;fitAfterCapture=false;recorded.reset();recordMode=processor.recording().isEnabled();
        lastClearSerial=processor.getSpectrumClearSerial();view.reset(now());fit=false;
        getProperties().set("previewActive",false);resized();dirty=true;repaint();
    }
    void resetView(){
        panActive=false;
        if(processor.recording().isEnabled())recorded=processor.recording().snapshot();
        view.reset(now());fit=true;
        if(recorded&&!recorded->empty()){view.zoomX=baseSpan()/juce::jmax(.05,recorded->end()-timelineStart());view.endTime=recorded->end();view.follow=false;}
        updateImageBounds();dirty=true;repaint();
    }
    void resetFileTimeView(){
        // Loading a file must not silently multiply Speed by a whole-file Fit.
        // The overview still contains the complete file; Fit is an explicit action.
        const auto audio=processor.recording().snapshot();
        view.reset((audio?audio->start():0.)+baseSpan());view.follow=false;
        fit=false;panActive=false;updateImageBounds();dirty=true;repaint();
    }
    std::function<void(const juce::String&)> onSelected;
    std::function<void()> onDelete,onNavigation,onMoveBegin,onRequestAudio,onImagePrint,onRevertFocused,onRevertEnd;
    std::function<void(float)> onWheelSpeed;
    std::function<void(const spectrumtag::PrintEdit&)> onMoveEnd;
    void setPrintButtonTooltip(const juce::String& text){printImageButton.setTooltip(text);}
    void setChinese(bool value){chinese=value;imageBox->setChinese(value);printImageButton.setButtonText(juce::String::fromUTF8(value?"打印":"Print"));printImageButton.setTypeface(value?spectrumtag::ui::chineseTypeface():typeface);printImageButton.setTypeScale(value?spectrumtag::ui::chineseScale:1.0f);fitButton.setTooltip(juce::String::fromUTF8(processor.isLoadMode()?(value?"全览：显示已加载音频事件的完整范围。":"Fit the complete range of loaded audio events."):(value?"全览：显示整个录音范围。未录音时返回实时视图。":"Fit the recorded range. Before recording, return to the live view.")));repaint();}
    void setPreview(std::optional<spectrumtag::PrintEdit> value,bool released=false,bool anchor=false){
        const bool rebuild=!preview||!value||!spectrumtag::sameEdit(*preview,*value);
        preview=std::move(value);previewReleased=released;
        if(preview&&anchor){view.zoomX=preview->viewZoom;view.follow=false;fit=false;view.endTime=preview->start+(1.-(double)imageBox->getX()/juce::jmax(1,contentBounds.getWidth()))*timeSpan();}
        if(rebuild)previewImage={};
        getProperties().set("previewActive",preview.has_value());refreshCanvasState();dirty=true;repaint();
    }
    void releasePreview(){previewReleased=true;dirty=true;repaint();}
    void constrainNavigation(bool allowEmpty=false){
        if(!recorded||recorded->empty()||view.follow)return;
        const double span=timeSpan(),first=timelineStart(),last=recorded->end();
        if(processor.filePlayerMode&&allowEmpty)return;
        view.endTime=span>=last-first?(first+last+span)*.5:juce::jlimit(first+span,last,view.endTime);
    }
    void useAbsoluteSpeed(float speed){
        if(selectedPrint())return;
        // The parameter already holds the new value. Recover the previous
        // visible span before removing Fit's extra zoom, then retain its centre.
        const double oldSpan=lastSpeed>0?contentBounds.getWidth()/(spectrumtag::timing::pixelsPerSecond*lastSpeed*view.zoomX):timeSpan();
        const double centre=view.endTime-oldSpan*.5;
        const bool empty=recorded&&(view.endTime<=recorded->start()||view.endTime-oldSpan>=recorded->end());
        view.zoomX=1;fit=false;panActive=false;lastSpeed=speed;
        if(!view.follow)view.endTime=centre+contentBounds.getWidth()/(spectrumtag::timing::pixelsPerSecond*speed)*.5;
        constrainNavigation(empty);dirty=true;repaint();
    }
    double timeSpan() const{return baseSpan()/view.zoomX;}
    double zoom() const{return view.zoomX;}
    double visibleSeconds() const{return timeSpan();}
    bool isFitView() const{return fit&&recorded&&!recorded->empty();}
    double fitSpeed() const{return processor.getAPVTS().getRawParameterValue(ParameterIDs::speed)->load()*view.zoomX;}
    float topHz() const {const float tops[]={20000,24000,22050,48000};return juce::jlimit(21.f,(float)sourceRate()*.5f,tops[juce::jlimit(0,3,(int)processor.getAPVTS().getRawParameterValue(ParameterIDs::frequencyRange)->load())]);}
    void saveView(SpectrumTagAudioProcessor::EditorState& s) const {s.viewEnd=view.endTime;s.viewZoom=view.zoomX;s.viewFollow=view.follow;s.viewFit=fit;s.viewValid=true;s.selectedPrint=selected;}
    void restoreView(const SpectrumTagAudioProcessor::EditorState& s){
        panActive=false;
        if(s.viewValid){view.endTime=s.viewEnd;view.zoomX=juce::jmax(.000001,s.viewZoom);view.follow=s.viewFollow;fit=s.viewFit;selected=s.selectedPrint;restored=true;}
        else if(processor.filePlayerMode){selected=s.selectedPrint;restored=false;resetFileTimeView();}
        dirty=true;
    }
    void restorePrintView(const spectrumtag::PrintEdit& e){
        panActive=false;
        view.zoomX=juce::jmax(.000001,e.viewZoom);view.follow=false;fit=false;
        // A saved zoom describes the canvas at Print time. The current host
        // rectangle (or overview height) may differ. Restore the aspect of the
        // actual time/frequency footprint before fitting the source image to it.
        // This is view-only: selection must not move or re-render the audio.
        const auto footprint=editBounds(e);
        // Preserve the original pixel-rounded width when it already matches.
        if(footprint.getWidth()>0&&footprint.getHeight()>0&&e.imageAspect>0
            &&std::abs(e.imageAspect*footprint.getHeight()-footprint.getWidth())>1.f)
            view.zoomX*=e.imageAspect*footprint.getHeight()/footprint.getWidth();
        view.endTime=e.start+e.duration*.5+timeSpan()*.5;
        alignImageToPrint(e);
    }
    void alignImageToPrint(const spectrumtag::PrintEdit& e){
        dirty=true;const auto b=editBounds(e).translated(-(float)contentBounds.getX(),-(float)contentBounds.getY());
        imageBox->setPrintBounds({b.getX()/contentBounds.getWidth(),b.getY()/contentBounds.getHeight(),b.getWidth()/contentBounds.getWidth(),b.getHeight()/contentBounds.getHeight()});
    }
    double imageStart() const {
        return view.endTime-baseSpan()/view.zoomX+getImageBoxX()/juce::jmax(1,contentBounds.getWidth())*baseSpan()/view.zoomX;
    }
    // 让图片对象的左边缘正好落在时间 t 上（Ctrl+V 在播放位置新建对象时用）。
    void placeImageStartAt(double t){
        if(contentBounds.getWidth()<1)return;
        const double span=timeSpan();
        if(span<=0)return;
        // 播放位置不在当前视窗内时先跟过去，保证新对象可见。
        if(t<view.endTime-span||t>view.endTime){view.follow=false;fit=false;view.endTime=t+span*.85;}
        auto n=imageBox->getNormalisedBounds();
        const double viewStart=view.endTime-span;
        n.setX((float)juce::jlimit((double)-n.getWidth()+.02,0.98,(t-viewStart)/span));
        imageBox->setPrintBounds(n);
        dirty=true;repaint();
    }
    juce::String selectedEdit() const {return selected;}
    std::optional<spectrumtag::PrintEdit> selectedPrint() const {
        auto edits=processor.recording().edits();if(edits)for(const auto& e:*edits)if(e.id==selected)return e;return {};
    }
    void selectEdit(juce::String id){selected=std::move(id);repaint();}
    void removeSelected(){if(selected.isNotEmpty()){processor.recording().remove(selected);selected={};repaint();}}
    bool keyPressed(const juce::KeyPress& key) override {
        if(key.getKeyCode()==juce::KeyPress::deleteKey&&(selected.isNotEmpty()||imageBox->hasImage())){if(onDelete)onDelete();return true;}return false;
    }
    void resized() override {
        auto r=getLocalBounds();statusBounds=r.removeFromBottom(20);
        overviewBounds=processor.recording().isEnabled()?r.removeFromBottom(82).withTrimmedLeft(kFreqAxisWidth):juce::Rectangle<int>();
        axisBounds=r.removeFromLeft(kFreqAxisWidth);contentBounds=r;
        imageClip.setBounds(contentBounds);fitButton.setBounds(contentBounds.getRight()-36,contentBounds.getY()+6,30,30);fitButton.toFront(false);updateImageBounds();layoutPrintImageButton();dirty=true;
    }
    void tick() {
        if(lastClearSerial!=processor.getSpectrumClearSerial())clearDisplay();
        processor.drainSpectrumFrames(incoming);
        bool changed=false;
        // 三态决定画布内容：空对象框=投放引导，未打印/聚焦=显示图片，
        // 已打印未聚焦=只显示频谱里的挖空。没有音频时整框隐藏。
        const bool hasImage=imageBox->hasImage();
        if(hasImage!=lastHasImage){lastHasImage=hasImage;repaint();}
        refreshCanvasState();
        const bool audioLoaded=processor.hasLoadedAudio();
        if(audioLoaded!=lastAudioLoaded){lastAudioLoaded=audioLoaded;repaint();}
        if(processor.recording().isEnabled()!=recordMode){
            recordMode=processor.recording().isEnabled();rasterEpoch=renderer.invalidate();waterfall={};waterfallSource.reset();overviewImage={};resized();changed=true;
            if(!recordMode){recorded.reset();view.reset(now());fit=false;fitAfterCapture=false;}
        }
        const auto latest=recordMode?processor.recording().snapshot():nullptr;
        if(latest!=recorded){const bool first=!recorded||(latest&&recorded->sourceId!=latest->sourceId);recorded=latest;previewImage={};changed=true;
            if(first&&recorded&&(!restored||(view.follow&&!processor.isTransportAdvancing()))){
                if(processor.recording().isArmed()&&processor.isTransportAdvancing()){view.reset(now());fit=false;}
                else if(processor.filePlayerMode)resetFileTimeView();
                else resetView();
            }
            if(first&&recorded&&restored&&(view.endTime<=recorded->start()||view.endTime-timeSpan()>=recorded->end()))constrainNavigation();restored=false;
            if(fit&&recorded&&!recorded->empty()){view.zoomX=baseSpan()/juce::jmax(.05,recorded->end()-timelineStart());view.endTime=recorded->end();}
        }
        const bool playing=processor.isTransportAdvancing(),armed=processor.recording().isArmed();
        if(recordMode&&wasArmed&&!armed)fitAfterCapture=true;
        if(recordMode&&fitAfterCapture&&!processor.recording().isBusy()){resetView();fitAfterCapture=false;changed=true;}
        wasPlaying=playing;wasArmed=armed;
        const auto desired=processor.recording().edits();
        if(observedEdits&&desired&&std::any_of(observedEdits->begin(),observedEdits->end(),[&](const auto& previous){return std::none_of(desired->begin(),desired->end(),[&](const auto& current){return previous.id==current.id;});}))refreshDeletedPrint();
        observedEdits=desired;
        const auto exists=[&](const juce::String& id){return desired&&std::any_of(desired->begin(),desired->end(),[&](const auto& e){return e.id==id;});};
        if(preview&&!exists(preview->id))setPreview({});
        if(selected.isNotEmpty()&&!exists(selected)){selected={};dragEdit.reset();movingPrint=false;repaint();}
        if(preview&&previewReleased)repaint();
        if(recordMode&&lastCursor!=processor.audition().position()){lastCursor=processor.audition().position();repaint();}
        for(const auto& frame:incoming){if(!recordMode&&!history->frames.empty()&&history->frames.back()->epoch!=frame.epoch)view.reset(now());history->append(frame);}
        if(!recordMode&&!incoming.empty())changed=true;
        const float speed=processor.getAPVTS().getRawParameterValue(ParameterIDs::speed)->load();
        if(speed!=lastSpeed){if(lastSpeed>0)fit=false;lastSpeed=speed;changed=true;}
        const float top=topHz();if(top!=lastTop){lastTop=top;changed=true;}
        const double followEnd=now();if(view.follow&&view.endTime!=followEnd){view.endTime=followEnd;changed=true;}
        if(panActive){
            const auto clock=juce::Time::getMillisecondCounterHiRes();const double dt=juce::jlimit(0.,.1,(clock-panTick)/1000.);panTick=clock;
            view.endTime+=(panTarget-view.endTime)*(1.-std::exp(-dt/.045));
            if(std::abs(panTarget-view.endTime)<timeSpan()/juce::jmax(1,contentBounds.getWidth())*.05){view.endTime=panTarget;panActive=false;}
            changed=true;
        }
        if(auto result=renderer.result();result&&result->epoch==rasterEpoch&&result->generation>appliedRaster
            &&result->image.getWidth()==contentBounds.getWidth()&&result->image.getHeight()==contentBounds.getHeight()
            &&result->palette==palette&&result->scale==scaleMode&&result->top==top
            &&((!recorded&&!result->source)||(recorded&&result->source==recorded))){
            waterfall=result->image;waterfallSource=result->source;waterfallStart=result->start;waterfallEnd=result->end;appliedRaster=result->generation;
            imageRevealId={};  // 新栅格已不再含这次打印，不再需要为它保留旧频谱
            getProperties().set("rasterRevision",result->source?result->source->revision:-1);
            getProperties().set("rasterApplied",(juce::int64)appliedRaster);getProperties().set("rasterMs",result->milliseconds);
            repaint();
        }
        // A snapshot can be published before its worker clears busy. If its
        // raster arrives during that interval, retirement must be retried on
        // later ticks even when there is no new raster to adopt.
        if(preview&&previewReleased&&waterfall.isValid()&&waterfallSource==recorded&&recorded&&!processor.recording().isBusy())
            for(const auto& e:recorded->edits)if(SPECTRUMTAG_ARA?(e.id==preview->id&&e.amount==preview->amount&&e.imageMode==preview->imageMode&&e.ratio==preview->ratio&&e.invert==preview->invert&&e.compensation==preview->compensation&&e.fillVersion==preview->fillVersion&&e.speed==preview->speed&&e.scaleMode==preview->scaleMode&&e.displayTop==preview->displayTop&&std::abs(e.start-preview->start)<1.e-6&&std::abs(e.duration-preview->duration)<1.e-6):spectrumtag::sameAudioEdit(e,*preview)){setPreview({});break;}
        if(changed){dirty=true;repaint();}
        if(dirty)render();
        if(audioHintFlashUntil!=0){
            if(juce::Time::getMillisecondCounter()<audioHintFlashUntil)repaint(audioBannerBounds());
            else{audioHintFlashUntil=0;repaint(audioBannerBounds());}
        }
        getProperties().set("recordMode",recordMode);getProperties().set("panActive",panActive);getProperties().set("panTarget",panTarget);
        getProperties().set("audioLoaded",audioLoaded);getProperties().set("audioHintActive",audioHintFlashUntil>juce::Time::getMillisecondCounter());
        getProperties().set("imageVisible",imageBox->isVisible());
        getProperties().set("imageStart",imageStart());getProperties().set("audioBannerY",audioBannerBounds().getY());
        getProperties().set("displayStart",view.endTime-timeSpan());getProperties().set("displayEnd",view.endTime);getProperties().set("displayTop",top);getProperties().set("overviewStart",timelineStart());getProperties().set("overviewEnd",recorded?recorded->end():0.);
    }
    juce::var diagnosticState(bool probePixels=false) const {
        using namespace spectrumtag::diagnostics;auto o=object();auto result=renderer.result();
        put(o,"recordMode",recordMode);put(o,"viewSnapshotRevision",recorded?recorded->revision:-1);
        put(o,"waterfallRevision",waterfallSource?waterfallSource->revision:-1);put(o,"waterfallMatchesViewSource",recorded==waterfallSource);
        put(o,"requestedRaster",(juce::int64)requestedRaster);put(o,"appliedRaster",(juce::int64)appliedRaster);put(o,"epoch",(juce::int64)rasterEpoch);
        put(o,"resultGeneration",result?(juce::int64)result->generation:-1);put(o,"resultEpoch",result?(juce::int64)result->epoch:-1);
        put(o,"resultRevision",result&&result->source?result->source->revision:-1);put(o,"resultMatchesViewSource",result&&result->source==recorded);
        put(o,"resultMatchesGeometry",result&&result->image.getWidth()==contentBounds.getWidth()&&result->image.getHeight()==contentBounds.getHeight());
        put(o,"resultMatchesStyle",result&&result->palette==palette&&result->scale==scaleMode&&result->top==topHz());
        put(o,"resultMs",result?result->milliseconds:0.);put(o,"dirty",dirty);put(o,"previewActive",preview.has_value());put(o,"previewReleased",previewReleased);
        put(o,"contentBounds",rectangle(contentBounds));put(o,"imageBounds",rectangle(imageBox->getBounds()));put(o,"imageVisible",imageBox->isVisible());
        put(o,"imageLoaded",imageBox->hasImage());put(o,"imageConversionReady",imageBox->conversionReady());
        put(o,"viewStart",view.endTime-timeSpan());put(o,"viewEnd",view.endTime);put(o,"viewZoom",view.zoomX);put(o,"follow",view.follow);put(o,"fit",fit);
        put(o,"scale",scaleMode);put(o,"rangeHz",topHz());put(o,"palette",palette);put(o,"waterfallStart",waterfallStart);put(o,"waterfallEnd",waterfallEnd);
        put(o,"waterfallValid",waterfall.isValid());put(o,"waterfallFormat",(int)waterfall.getFormat());put(o,"waterfallWidth",waterfall.getWidth());put(o,"waterfallHeight",waterfall.getHeight());
        put(o,"paintCount",(juce::int64)paintCount);put(o,"paintedRaster",(juce::int64)paintedRaster);put(o,"paintAgeMs",lastPaintTime>0?juce::Time::getMillisecondCounterHiRes()-lastPaintTime:-1.);
        put(o,"showing",isShowing());put(o,"liveFrameCount",(int)history->frames.size());
        if(probePixels){juce::Array<juce::var> probes;const auto edits=processor.recording().edits();
            if(edits)for(size_t i=0;i<std::min<size_t>(edits->size(),64);++i){auto entry=object();const auto& e=(*edits)[i];auto bounds=editBounds(e);
                put(entry,"index",(int)i);put(entry,"selected",e.id==selected);put(entry,"bounds",bounds.toString());put(entry,"intersectsViewport",bounds.intersects(contentBounds.toFloat()));
                double total=0,squares=0;int count=0;float peak=0;
                if(waterfall.isValid()&&waterfallEnd>waterfallStart&&recorded){const auto nyquist=recorded->sampleRate*.5;
                    for(int x=0;x<32;++x)for(int y=0;y<32;++y){const double time=e.start+(x+.5)*e.duration/32.;
                        const double px=(time-waterfallStart)/(waterfallEnd-waterfallStart)*waterfall.getWidth();
                        const double py=spectrumtag::frequency::toY((e.low+(e.high-e.low)*(y+.5)/32.)*nyquist,topHz(),scaleMode)*waterfall.getHeight();
                        if(!std::isfinite(px)||!std::isfinite(py)||px<0||py<0||px>=waterfall.getWidth()||py>=waterfall.getHeight())continue;
                        const float value=waterfall.getPixelAt((int)px,(int)py).getBrightness();total+=value;squares+=value*value;peak=juce::jmax(peak,value);++count;
                    }}
                put(entry,"rasterProbePixels",count);put(entry,"rasterMeanBrightness",total/juce::jmax(1,count));put(entry,"rasterPeakBrightness",peak);
                put(entry,"rasterBrightnessVariance",juce::jmax(0.,squares/juce::jmax(1,count)-std::pow(total/juce::jmax(1,count),2)));probes.add(std::move(entry));
            }put(o,"printRasterProbes",probes);
        }
        return o;
    }
    void mouseWheelMove(const juce::MouseEvent& event,const juce::MouseWheelDetails& wheel) override {
        const auto point=event.getEventRelativeTo(this).position;if(!contentBounds.toFloat().contains(point))return;
        if(onNavigation)onNavigation();
        const float delta=wheel.deltaY!=0?wheel.deltaY:wheel.deltaX;
        const float x=juce::jlimit(0.f,1.f,(point.x-contentBounds.getX())/juce::jmax(1,contentBounds.getWidth()));
        if(event.mods.isShiftDown()||event.mods.isCtrlDown()){
            panActive=false;
            const auto previous=timeSpan();const double anchor=view.endTime-(1-x)*previous;auto* p=processor.getAPVTS().getParameter(ParameterIDs::speed);
            const bool empty=recorded&&(view.endTime<=recorded->start()||view.endTime-previous>=recorded->end());
            const float value=juce::jlimit(.1f,4.f,p->convertFrom0to1(p->getValue())*std::exp(delta));
            if(onWheelSpeed)onWheelSpeed(value);
            if(!selectedPrint())view.zoomX=1;lastSpeed=value;
            if(!view.follow)view.endTime=anchor+(1-x)*timeSpan();
            constrainNavigation(empty);
        }else{
            if(view.follow)view.endTime=now();
            if(!panActive)panTarget=view.endTime;
            // One quarter of the previous travel, with a short, non-inertial ease.
            panTarget-=delta*timeSpan()*.375;
            if(!recorded){const double oldest=history->frames.empty()?0:history->frames.front()->time;panTarget=juce::jlimit(juce::jmin(now(),oldest+timeSpan()),now(),panTarget);}
            view.follow=false;panActive=true;panTick=juce::Time::getMillisecondCounterHiRes();
        }
        fit=false;dirty=true;repaint();
    }
    void mouseMove(const juce::MouseEvent& event) override {
        const auto point=event.getEventRelativeTo(this).position;
        if(audioBannerBounds().toFloat().contains(point)){setMouseCursor(juce::MouseCursor::PointingHandCursor);return;}
        if(overviewBounds.toFloat().contains(point)){
            const auto handle=overviewHitTest(point);
            setMouseCursor(handle==OverviewHandle::ResizeLeft||handle==OverviewHandle::ResizeRight
                ?juce::MouseCursor::LeftRightResizeCursor:juce::MouseCursor::PointingHandCursor);
            return;
        }
        auto edit=selectedPrint();
        setMouseCursor(edit&&resizeHandle(editBounds(*edit)).contains(point)
            ?juce::MouseCursor::BottomRightCornerResizeCursor:juce::MouseCursor::NormalCursor);
    }
    void mouseDown(const juce::MouseEvent& event) override {
        panActive=false;dragEdit.reset();movingPrint=false;resizingPrint=false;overviewHandle=OverviewHandle::None;draggingImage=false;revertGesture=false;
        const auto point=event.getEventRelativeTo(this).position;
        // 未加载音频时的引导条：点击等同"加载音频"按钮。
        if(event.mods.isLeftButtonDown()&&audioBannerBounds().toFloat().contains(point)){if(onRequestAudio)onRequestAudio();return;}
        // 没有音频时点击频谱：不渲染，只高亮"先加载音频"。
        if(event.mods.isLeftButtonDown()&&!canAcceptImage()&&contentBounds.toFloat().contains(point)){flashAudioHint();return;}
        if(!recorded||recorded->empty())return;
        if(isShowing())grabKeyboardFocus();
        if(overviewBounds.toFloat().contains(point)){
            if(onNavigation)onNavigation();
            overviewHandle=overviewHitTest(point);
            if(overviewHandle==OverviewHandle::ResizeLeft||overviewHandle==OverviewHandle::ResizeRight){
                view.follow=false;fit=false;panActive=false;repaint();
            }else{
                overviewHandle=OverviewHandle::Scrub;
                overviewScrubTo(point.x);
            }
            return;
        }
        if(event.eventComponent==imageBox.get())return;
        if(!contentBounds.toFloat().contains(point))return;
        const double clickedTime=view.endTime-timeSpan()+(point.x-contentBounds.getX())/contentBounds.getWidth()*timeSpan();
        auto edits=processor.recording().edits();juce::String id;
        // The selected corner has priority over overlapping Prints. A corner
        // click still recalls the saved view; the drag begins in that view.
        if(auto edit=selectedPrint();edit&&event.mods.isLeftButtonDown()&&resizeHandle(editBounds(*edit)).contains(point)){
            id=edit->id;resizingPrint=true;
        }else if(edits)for(auto it=edits->rbegin();it!=edits->rend();++it)if(editBounds(*it).contains(point)){id=it->id;break;}
        // Always reselect, including the already selected Print: a click restores
        // its saved aspect/view after temporary wheel stretching, without DSP.
        if(onSelected)onSelected(id);else selected=id;
        if(id.isNotEmpty()){dragEdit=selectedPrint();dragPoint=point;if(dragEdit)dragBounds=editBounds(*dragEdit);}
        if(id.isEmpty())processor.auditionAt(clickedTime);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if(overviewHandle!=OverviewHandle::None&&event.mods.isLeftButtonDown()){
            const auto x=event.getEventRelativeTo(this).position.x;
            if(overviewHandle==OverviewHandle::Scrub)overviewScrubTo(x);
            else overviewResizeTo(x);
            return;
        }
        if(draggingImage){
            if(!event.mods.isLeftButtonDown())return;
            imageBox->continueExternalDrag(imageBoxPoint(event.getEventRelativeTo(this).position));
            return;
        }
        if(!dragEdit||!event.mods.isLeftButtonDown()||!recorded)return;
        const auto delta=event.getEventRelativeTo(this).position-dragPoint;
        if(!movingPrint&&delta.getDistanceFromOrigin()<3.f)return;
        // 已打印对象被直接拖动（状态2/状态3）：这一手势立即退回未打印态，
        // 图片按打印范围摆回，并把剩下的位移继续交给图片框，用户不必再点一次。
        if(!movingPrint&&dragEdit->id==selected&&onRevertFocused){
            const auto point=event.getEventRelativeTo(this).position;
            const bool resize=resizingPrint;
            revertGesture=true;
            onRevertFocused();
            dragEdit.reset();resizingPrint=false;
            if(imageBox->hasImage()&&imageBox->isVisible()){
                draggingImage=true;
                imageBox->beginExternalDrag(imageBoxPoint(dragPoint),resize);
                imageBox->continueExternalDrag(imageBoxPoint(point));
            }
            return;
        }
        if(!movingPrint){
            if(onMoveBegin)onMoveBegin();movingPrint=true;
            // Build the lightweight image once; geometry gestures stretch this
            // image without mask generation or audio work on mouse events.
            setPreview(*dragEdit);ensurePreviewImage();
        }
        auto e=*dragEdit;
        if(resizingPrint){
            const float aspect=juce::jmax(.001f,e.imageAspect);
            const float available=juce::jmax(.001f,juce::jmin(contentBounds.getBottom()-dragBounds.getY(),(float)(900./timeSpan()*contentBounds.getWidth()/aspect)));
            const float height=juce::jlimit(juce::jmin(12.f,available),available,
                dragBounds.getHeight()+(delta.x/aspect+delta.y)*.5f);
            e.duration=(double)(height*aspect)/contentBounds.getWidth()*timeSpan();
            const float nyquist=(float)recorded->sampleRate*.5f;
            e.low=frequency((dragBounds.getY()+height-contentBounds.getY())/contentBounds.getHeight(),topHz())/nyquist;
            // Keep the saved start and high frequency (top-left anchor).
            // The final mask resolution is chosen only on mouse release.
        }else{
        e.start+=delta.x/contentBounds.getWidth()*timeSpan();
        if(processor.filePlayerMode){const double limit=juce::jmin(900.,64000000./recorded->sampleRate);e.start=juce::jlimit((recorded->fileSourceLast/recorded->sampleRate)-limit,limit-e.duration,e.start);}
        else e.start=juce::jlimit(recorded->start()-e.duration+1./recorded->sampleRate,recorded->end()-1./recorded->sampleRate,e.start);
        const float height=dragBounds.getHeight(),top=juce::jlimit((float)contentBounds.getY(),juce::jmax((float)contentBounds.getY(),contentBounds.getBottom()-height),dragBounds.getY()+delta.y);
        const float nyquist=(float)recorded->sampleRate*.5f;
        e.high=frequency((top-contentBounds.getY())/contentBounds.getHeight(),topHz())/nyquist;
        e.low=frequency((top+height-contentBounds.getY())/contentBounds.getHeight(),topHz())/nyquist;
        }
        preview=e;previewReleased=false;getProperties().set("previewActive",true);repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if(draggingImage){
            // 退回未打印态的这次手势结束：图片框收尾，编辑器提交同一个撤销步。
            draggingImage=false;imageBox->endExternalDrag();
        }else if(revertGesture&&onRevertEnd){
            // 图片缺失等没能接着拖动的情况：同样要结束这次撤销步。
            onRevertEnd();
        }
        revertGesture=false;
        if(movingPrint&&preview&&onMoveEnd){
            auto e=*preview;
            if(resizingPrint)e.cols=juce::jlimit(4,4096,juce::roundToInt(editBounds(e).getWidth()));
            onMoveEnd(e);
        }
        overviewHandle=OverviewHandle::None;
        dragEdit.reset();movingPrint=false;resizingPrint=false;draggingImage=false;
    }
    void paint(juce::Graphics& g) override {
        ++paintCount;paintedRaster=appliedRaster;lastPaintTime=juce::Time::getMillisecondCounterHiRes();
        if(dirty)render();
        g.fillAll(kSpectrogramBgColour);
        if(waterfall.isValid()){
            juce::Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(contentBounds);
            const auto desired=processor.recording().edits();
            const auto imageArea=imageBox->isVisible()&&imageBox->hasImage()
                ?imageBox->getBounds().translated(contentBounds.getX(),contentBounds.getY()).expanded(2)
                :juce::Rectangle<int>();
            if(waterfallSource&&desired)for(const auto& e:waterfallSource->edits)
                if(std::none_of(desired->begin(),desired->end(),[&](const auto& current){return current.id==e.id;})){
                    const float padding=(float)(e.fftSize/waterfallSource->sampleRate/timeSpan()*contentBounds.getWidth());
                    // FFT window leakage can reach beyond the image's band.
                    // Hide the obsolete time slice, including that leakage.
                    const auto obsolete=editBounds(e).expanded(padding+2,0);
                    // 已打印对象退回未打印态时，这块位置马上由图片本身接替（或者
                    // 图片正在原位盖着它）。这时只挖掉它自己那条频带：把整列都挖空
                    // 会让图片上下的频谱在新栅格到达之前闪黑一下。
                    const bool bandOnly=e.id==imageRevealId
                        ||(imageArea.getWidth()>0&&imageArea.contains(editBounds(e).getSmallestIntegerContainer()));
                    g.excludeClipRegion(bandOnly?obsolete.getSmallestIntegerContainer()
                        :juce::Rectangle<float>(obsolete.getX(),(float)contentBounds.getY(),obsolete.getWidth(),(float)contentBounds.getHeight()).getSmallestIntegerContainer());
                }
            if(waterfallEnd>waterfallStart)g.drawImage(waterfall,juce::Rectangle<float>(timeX(waterfallStart),(float)contentBounds.getY(),timeX(waterfallEnd)-timeX(waterfallStart),(float)contentBounds.getHeight()),juce::RectanglePlacement::stretchToFit);
            else g.drawImageAt(waterfall,contentBounds.getX(),contentBounds.getY());
        }
        if(recorded){
            juce::Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(contentBounds);
            if(preview){
                ensurePreviewImage();
                const auto bounds=editBounds(*preview);
                g.drawImage(previewImage,bounds,juce::RectanglePlacement::stretchToFit);
                if(previewReleased){
                    const float progress=juce::jlimit(0.f,.99f,processor.recording().renderProgress());
                    const auto visible=bounds.getIntersection(contentBounds.toFloat());
                    auto panel=visible.withSizeKeepingCentre(juce::jmin(190.f,visible.getWidth()),juce::jmin(58.f,visible.getHeight()));
                    g.setColour(juce::Colour(0xe51b2023));g.fillRoundedRectangle(panel,5.f);
                    const auto bar=panel.reduced(8.f).removeFromBottom(5.f);g.setColour(juce::Colours::white.withAlpha(.2f));g.fillRect(bar);g.setColour(juce::Colour(0xff80e2b5));g.fillRect(bar.withWidth(bar.getWidth()*progress));
                    const float phase=(float)(juce::Time::getMillisecondCounter()%1200)/1200*juce::MathConstants<float>::twoPi;
                    const auto centre=panel.getCentre().translated(0,-10);juce::Path arc;arc.addCentredArc(centre.x,centre.y,6,6,0,phase,phase+4.6f,true);g.strokePath(arc,juce::PathStrokeType(2.f));
                    g.setColour(juce::Colours::white);g.setFont(chinese?spectrumtag::ui::chineseFont(12.f):juce::Font(12.f));const auto stage=processor.recording().renderStage();juce::String message;
                    if(stage==spectrumtag::RecordingEngine::RenderStage::queued)message=juce::String::fromUTF8(chinese?"等待渲染…":"Queued…");
                    else if(stage==spectrumtag::RecordingEngine::RenderStage::preparing)message=juce::String::fromUTF8(chinese?"准备音频与频谱…":"Preparing audio…");
                    else message=juce::String::fromUTF8(progress>=.85f?(chinese?"更新频谱 ":"Spectrum "):(chinese?"渲染 ":"Rendering "))+(progress<.01f?juce::String("<1"):juce::String((int)(progress*100)))+"%";
                    g.drawText(message,panel.toNearestInt().withTrimmedTop(25).withTrimmedBottom(11),juce::Justification::centred);
                    getProperties().set("renderProgress",progress);
                }
            }
            if(auto edit=preview&&preview->id==selected?preview:selectedPrint()){
                const auto box=editBounds(*edit);g.setColour(juce::Colours::white);g.drawRect(box,2.0f);
                const auto handle=resizeHandle(box);
                g.setColour(juce::Colour(0xff20282b));g.fillRect(handle);
                g.setColour(juce::Colours::white);g.drawRect(handle,1.f);
                g.drawLine(handle.getRight()-4,handle.getY()+4,handle.getX()+4,handle.getBottom()-4,1.5f);
                g.setFont(chinese?spectrumtag::ui::chineseFont(12.0f):juce::Font(12.0f));g.drawText(juce::String::fromUTF8(chinese?"Delete 键删除":"Press Delete to Remove"),box.withHeight(18).toNearestInt(),juce::Justification::centred);
                getProperties().set("selectedX",box.getX());getProperties().set("selectedY",box.getY());getProperties().set("selectedW",box.getWidth());getProperties().set("selectedH",box.getHeight());
            }
            paintCursor(g,contentBounds,timeX(processor.audition().position()),false);
        }
        paintOverview(g);
        g.setColour(kAxisBgColour);g.fillRect(axisBounds);
        const float nyquist=topHz();
        g.setFont(juce::Font(11.0f));
        if(scaleMode>=spectrumtag::frequency::insight){
            constexpr float ticks[]={20,100,200,300,500,700,1000,1200,1500,2000,2500,3000,3500,4000,4500,5000,6000,7000,8000,9000,10000,12000,15000,20000,22000,22050,24000,30000,40000,48000};
            constexpr float perceptualTicks[]={0,100,300,500,1000,1500,2000,3000,5000,7000,10000,15000,20000,24000,30000,40000,48000};
            constexpr float extendedTicks[]={10,20,30,50,70,100,200,300,500,1000,2000,3000,5000,10000,20000,30000,48000};
            const float* begin=scaleMode==6?std::begin(extendedTicks):scaleMode>=4?std::begin(perceptualTicks):std::begin(ticks);
            const float* end=scaleMode==6?std::end(extendedTicks):scaleMode>=4?std::end(perceptualTicks):std::end(ticks);
            float lastY=(float)contentBounds.getBottom()+20;
            for(auto tick=begin;tick!=end;++tick){const float hz=*tick;
                if(hz>nyquist)break;
                const float y=contentBounds.getY()+(spectrumtag::frequency::toY(hz,nyquist,scaleMode)-view.top)/view.span*(contentBounds.getHeight()-1);
                if(y<contentBounds.getY()-1||y>=lastY-12)continue;
                lastY=y;
                const juce::String text=hz>=1000?juce::String(hz/1000,hz==22050?2:(std::fmod(hz,1000.f)==0?0:1))+"k":juce::String((int)hz);
                g.setColour(kAxisColour);g.drawText(text,axisBounds.getX()+2,juce::jlimit(contentBounds.getY(),contentBounds.getBottom()-12,juce::roundToInt(y)-6),axisBounds.getWidth()-5,12,juce::Justification::centredRight);
            }
        }else for(int i=0;i<=6;++i){
            const float y=(float)i/6;const float hz=frequency(view.top+y*view.span,nyquist);
            const juce::String text=hz>=1000?juce::String(hz/1000,1)+"k":juce::String((int)hz);
            g.setColour(kAxisColour);g.drawText(text,axisBounds.getX()+2,contentBounds.getY()+juce::roundToInt(y*(contentBounds.getHeight()-12)),axisBounds.getWidth()-5,12,juce::Justification::centredRight);
        }
        if(scaleMode==1){
            juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(axisBounds.withWidth(26));
            g.setColour(kPianoWhiteKey);g.fillRect(axisBounds.withWidth(26));
            for(int midi=12;midi<132;++midi){
                const float hz=440.0f*std::pow(2.0f,(midi-69)/12.0f);
                const float y=(frequencyToYNorm_Log(hz,nyquist)-view.top)/view.span;
                if(y<0||y>1)continue;
                const int pc=midi%12;const bool black=pc==1||pc==3||pc==6||pc==8||pc==10;
                g.setColour(black?kPianoBlackKey:juce::Colours::grey);
                g.fillRect(axisBounds.getX(),contentBounds.getY()+juce::roundToInt(y*contentBounds.getHeight()),black?17:26,black?3:1);
            }
        }
        g.setColour(kBgColour);g.fillRect(statusBounds);g.setColour(kTextSub);g.setFont(chinese?spectrumtag::ui::chineseFont(11.0f):juce::Font(11.0f));
        const double span=baseSpan()/view.zoomX;
        const auto label=(SPECTRUMTAG_ARA?juce::String::fromUTF8(chinese?"ARA 已加载  ":"ARA Loaded  "):processor.filePlayerMode?juce::String::fromUTF8(chinese?"已加载  ":"Loaded  "):recordMode?juce::String::fromUTF8(processor.recording().isArmed()?(chinese?"录音中  ":"Recording  "):(chinese?"已录制  ":"Recorded  ")):juce::String::fromUTF8(view.follow?(chinese?"实时  ":"Live  "):(chinese?"历史  ":"History  ")))+juce::String(view.endTime-span,1)+" - "+juce::String(view.endTime,1)+" s  |  "+juce::String::fromUTF8(chinese?"右键显示 / 隐藏原图":"Right-click: show / hide image")+(preview?juce::String::fromUTF8(previewReleased?(chinese?"  |  模拟预览 — 正在渲染…":"  |  Preview — rendering…"):(chinese?"  |  模拟预览 — 松手后渲染":"  |  Preview — render on release")):juce::String())+(processor.recording().isBusy()?juce::String::fromUTF8(chinese?"  |  正在渲染…":"  |  Rendering…"):juce::String());
        const char* errors[]={"","Recording stopped: capture queue full","Recording stopped: 15 min / 64 M sample limit","Recording stopped: sample rate / channel layout changed","Recording cache could not be saved or loaded","Audio import failed / exceeds 15 min or 64 M samples"};
#if SPECTRUMTAG_ARA
        if(auto status=processor.araStatus();status.isNotEmpty()){
            g.setColour(juce::Colour(0xee151a1d));g.fillRect(contentBounds.withHeight(45));g.setColour(juce::Colours::lightgrey);g.setFont(chinese?spectrumtag::ui::chineseFont(12.0f):juce::Font(12.0f));g.drawFittedText(status,contentBounds.withHeight(45).reduced(12,4),juce::Justification::centredLeft,2);
        }
#endif
        // Draw the audio guidance above any ARA status banner: the guide is the
        // actionable next step when nothing is loaded.
        paintAudioBanner(g);
        const int error=juce::jlimit(0,5,processor.recording().errorCode());
        const char* errorsZh[]={"","录音已停止：采集队列已满","录音已停止：达到 15 分钟 / 6400 万采样限制","录音已停止：采样率或声道布局改变","无法保存或读取录音缓存","无法导入音频"};
        g.drawText(error?juce::String::fromUTF8(chinese?errorsZh[error]:errors[error]):label,statusBounds.reduced(4,0),juce::Justification::centredLeft);
    }
private:
    enum class OverviewHandle { None, Scrub, ResizeLeft, ResizeRight };
    // 画布上能看到什么，完全由图片对象的三态决定：未加载音频什么都不显示；
    // 只有未打印/聚焦的对象显示图片框（空对象框就是投放引导）。
    void refreshCanvasState(){
        // 清空进行中（音频即将消失）时什么都不显示，避免引导框闪一下。
        const bool audio=canAcceptImage()&&!processor.isClearPending();
        // 没有音频就没有图片对象：只在“音频曾经可用、随后被清空”时销毁旧图，
        // 避免启动时音频还没加载完就把刚恢复的图片清掉。
        if(audio)audioAvailableSeen=true;
        else if(audioAvailableSeen&&imageBox->hasImage()){imageBox->clearImage();if(selected.isNotEmpty()){selected={};}}
        const auto stage=imageStage();
        // 聚焦态只画打印范围和 Delete 提示（由 SpectrumView 绘制），不再显示图片框。
        const bool showBox=audio&&(stage==ImageStage::Empty||stage==ImageStage::Placing);
        imageBox->setVisible(showBox);
        printImageButton.setVisible(showBox&&stage==ImageStage::Placing&&!processor.isPrintRunning());
        if(printImageButton.isVisible())layoutPrintImageButton();
    }
    void layoutPrintImageButton(){
        const int w=78,h=36,gap=8;
        const auto b=imageBox->getBounds().translated(contentBounds.getX(),contentBounds.getY());
        int x=b.getCentreX()-w/2,y=b.getBottom()+gap;
        // 下方放不下就放到图片上方，始终不压住图片。
        if(y+h>contentBounds.getBottom())y=b.getY()-gap-h;
        x=juce::jlimit(contentBounds.getX()+2,contentBounds.getRight()-w-2,x);
        y=juce::jlimit(contentBounds.getY()+2,contentBounds.getBottom()-h-2,y);
        printImageButton.setBounds(x,y,w,h);
        printImageButton.toFront(false);
    }
    // 频谱视图坐标 → 图片框父组件（图片裁剪层）坐标。
    juce::Point<int> imageBoxPoint(juce::Point<float> point) const {
        return (point-juce::Point<float>((float)contentBounds.getX(),(float)contentBounds.getY())).roundToInt();
    }
    juce::Rectangle<int> audioBannerBounds() const {
        if(!processor.isLoadMode()||processor.hasLoadedAudio()||contentBounds.isEmpty())return {};
        const int w=juce::jlimit(1,juce::jmax(1,contentBounds.getWidth()-40),560);
        const int h=juce::jmin(52,juce::jmax(34,contentBounds.getHeight()/7));
        // 提示条放在频谱显示区正中间，而不是贴着顶部。
        return {contentBounds.getCentreX()-w/2,contentBounds.getCentreY()-h/2,w,h};
    }
    void paintAudioBanner(juce::Graphics& g){
        const auto area=audioBannerBounds();
        if(area.isEmpty())return;
        const auto nowMs=juce::Time::getMillisecondCounter();
        const bool flash=audioHintFlashUntil>nowMs;
        auto r=area.toFloat();
        g.setColour(juce::Colour(0xe61b2023));g.fillRoundedRectangle(r,8.f);
        g.setColour(flash?kImgBoxYellow.withAlpha(.55f+.35f*std::sin((float)nowMs*.02f))
                         :juce::Colours::white.withAlpha(.45f));
        g.drawRoundedRectangle(r.reduced(1.f),8.f,flash?2.f:1.f);

        juce::Font baseFont=(typeface!=nullptr)?juce::Font(typeface):juce::Font();
        const auto title=chinese?juce::String::fromUTF8("尚未加载音频"):juce::String("No audio loaded");
        const auto detail=chinese?(processor.filePlayerMode?juce::String::fromUTF8("拖入音频文件，或点击此处加载")
                                                                   :juce::String::fromUTF8("点击此处加载音频"))
                                 :(processor.filePlayerMode?juce::String("Drop an audio file, or click here to load")
                                                          :juce::String("Click here to load audio"));
        g.setColour(kTextWhite);
        g.setFont(chinese?spectrumtag::ui::chineseFont(15.f):baseFont.withHeight(15.f));
        if(area.getHeight()>=44){
            g.drawFittedText(title,area.withTrimmedBottom(area.getHeight()/2).reduced(10,0),juce::Justification::centred,1,.9f);
            g.setColour(kTextSub);
            g.setFont(chinese?spectrumtag::ui::chineseFont(12.5f):baseFont.withHeight(12.5f));
            g.drawFittedText(detail,area.withTrimmedTop(area.getHeight()/2).reduced(10,0),juce::Justification::centred,1,.9f);
        }else{
            g.drawFittedText(title+"  ·  "+detail,area.reduced(10,0),juce::Justification::centred,1,.85f);
        }
    }
    juce::Rectangle<float> overviewViewRect() const {
        if(!recorded||overviewBounds.isEmpty())return {};
        const double start=timelineStart(),end=recorded->end();
        if(end<=start)return {};
        const float x0=(float)overviewBounds.getX()+(float)((view.endTime-timeSpan()-start)/(end-start))*overviewBounds.getWidth();
        const float x1=(float)overviewBounds.getX()+(float)((view.endTime-start)/(end-start))*overviewBounds.getWidth();
        const float left=juce::jmin(x0,x1),right=juce::jmax(x0,x1);
        const float clampedLeft=juce::jmax((float)overviewBounds.getX(),left);
        const float clampedRight=juce::jmin((float)overviewBounds.getRight(),right);
        if(clampedRight-clampedLeft<1.f)return {};
        return {clampedLeft,(float)overviewBounds.getY(),clampedRight-clampedLeft,(float)overviewBounds.getHeight()};
    }
    OverviewHandle overviewHitTest(juce::Point<float> point) const {
        if(!recorded||recorded->empty()||!overviewBounds.toFloat().contains(point))return OverviewHandle::None;
        const auto rect=overviewViewRect();
        if(!rect.isEmpty()){
            // A fixed edge zone lets the view window be stretched with the same
            // effect as Ctrl-wheel (visible span == Speed / view zoom).
            const float edge=juce::jlimit(5.f,9.f,rect.getWidth()*.12f);
            if(std::abs(point.x-rect.getX())<=edge)return OverviewHandle::ResizeLeft;
            if(std::abs(point.x-rect.getRight())<=edge)return OverviewHandle::ResizeRight;
        }
        return OverviewHandle::Scrub;
    }
    double overviewTimeAt(float x) const {
        const double start=timelineStart(),end=recorded?recorded->end():start;
        if(end<=start)return start;
        return start+juce::jlimit(0.f,1.f,(x-(float)overviewBounds.getX())/juce::jmax(1,overviewBounds.getWidth()))*(end-start);
    }
    void overviewScrubTo(float x){
        if(!recorded)return;
        const double t=overviewTimeAt(x);
        processor.auditionAt(t);view.follow=false;fit=false;
        view.endTime=t+baseSpan()/view.zoomX*.5;
        updateImageBounds();dirty=true;repaint();
    }
    void overviewResizeTo(float x){
        if(!recorded||overviewBounds.isEmpty())return;
        const double totalStart=timelineStart(),totalEnd=recorded->end();
        const double totalSpan=juce::jmax(.05,totalEnd-totalStart);
        const double pointerTime=overviewTimeAt(x);
        const double viewSpan=timeSpan(),viewStart=view.endTime-viewSpan;
        // The box represents the loaded range: keep the fixed edge inside it so
        // the dragged width maps to the real visible span.
        const double fixed=overviewHandle==OverviewHandle::ResizeLeft?juce::jmin(view.endTime,totalEnd)
                                                                   :juce::jmax(viewStart,totalStart);
        double span=overviewHandle==OverviewHandle::ResizeLeft?fixed-pointerTime:pointerTime-fixed;
        const double plotWidth=juce::jmax(1,contentBounds.getWidth());
        const double zoom=selectedPrint()?view.zoomX:1.0;
        // Match the Speed range used by the Ctrl-wheel path: 0.1x–4x.
        const double minSpan=plotWidth/(spectrumtag::timing::pixelsPerSecond*4.0*zoom);
        const double maxSpan=juce::jmax(minSpan,juce::jmin(totalSpan,
            plotWidth/(spectrumtag::timing::pixelsPerSecond*0.1*zoom)));
        span=juce::jlimit(minSpan,maxSpan,span);
        const float speed=(float)juce::jlimit(0.1,4.0,plotWidth/(spectrumtag::timing::pixelsPerSecond*span*zoom));
        if(onWheelSpeed)onWheelSpeed(speed);
        view.zoomX=zoom;lastSpeed=speed;fit=false;view.follow=false;panActive=false;
        const double newSpan=timeSpan();
        view.endTime=overviewHandle==OverviewHandle::ResizeLeft?fixed:fixed+newSpan;
        view.endTime=juce::jlimit(totalStart+newSpan,totalEnd,view.endTime);
        updateImageBounds();dirty=true;repaint();
    }
    void ensurePreviewImage(){
        if(previewImage.isValid()||!preview||!recorded)return;
        previewImage=spectrumtag::printPreview(*recorded,*preview,palette);
    }
    void paintCursor(juce::Graphics& g,juce::Rectangle<int> bounds,float x,bool overview){
        const double time=processor.audition().position();const int side=x<bounds.getX()?-1:x>bounds.getRight()?1:0;
        getProperties().set(overview?"overviewCursorSide":"spectrumCursorSide",side);getProperties().set("cursorTime",time);
        g.setColour(juce::Colour(0xfff5e7a2));
        if(side==0){g.drawVerticalLine(juce::roundToInt(x),(float)bounds.getY(),(float)bounds.getBottom());return;}
        // The arrow is an offscreen indicator, never a clamped playback position.
        const float y=(float)bounds.getY()+14,xEdge=side<0?(float)bounds.getX()+4:(float)bounds.getRight()-4;
        juce::Path arrow;arrow.addTriangle(xEdge,y,xEdge-side*7,y-5,xEdge-side*7,y+5);g.fillPath(arrow);
        const auto label=juce::Rectangle<int>(side<0?bounds.getX()+15:bounds.getRight()-135,bounds.getY()+4,120,20);
        g.setColour(juce::Colour(0xe51b2023));g.fillRoundedRectangle(label.toFloat(),3);
        g.setColour(juce::Colour(0xfff5e7a2));g.setFont(12.f);g.drawText(juce::String(time,2)+" s",label.reduced(4,0),side<0?juce::Justification::centredLeft:juce::Justification::centredRight);
    }
    double timelineStart() const {return recorded?recorded->start():0.;}
    double sourceRate() const {return recorded?recorded->sampleRate:juce::jmax(1.0,processor.getSampleRate()>0?processor.getSampleRate():44100.0);}
    float getImageBoxX() const {return (float)imageBox->getX();}
    double now() const{return recorded?juce::jmax(processor.recording().isArmed()&&processor.isTransportAdvancing()?juce::jmax(recorded->end(),processor.audition().hostPosition()):recorded->end(),timelineStart()+baseSpan()/view.zoomX):(double)processor.getVisualisedSamples()/juce::jmax(1.0,processor.getSampleRate());}
    float timeX(double t) const{return contentBounds.getX()+(float)((t-(view.endTime-baseSpan()/view.zoomX))/(baseSpan()/view.zoomX))*contentBounds.getWidth();}
    static juce::Rectangle<float> resizeHandle(juce::Rectangle<float> bounds) {
        const float side=juce::jmin(14.f,bounds.getWidth(),bounds.getHeight());
        return {bounds.getRight()-side,bounds.getBottom()-side,side,side};
    }
    juce::Rectangle<float> editBounds(const spectrumtag::PrintEdit& edit) const {
        const float nyquist=topHz();
        const float sourceNyquist=recorded?(float)recorded->sampleRate*.5f:nyquist;
        const auto y=[&](float norm){const float hz=norm*sourceNyquist;const float yn=spectrumtag::frequency::toY(hz,nyquist,scaleMode);return contentBounds.getY()+(yn-view.top)/view.span*contentBounds.getHeight();};
        return {timeX(edit.start),y(edit.high),timeX(edit.start+edit.duration)-timeX(edit.start),y(edit.low)-y(edit.high)};
    }
    void paintOverview(juce::Graphics& g){
        if(overviewBounds.isEmpty())return;
        g.setColour(kPanelColour);g.fillRect(overviewBounds);
        if(!recorded||recorded->empty()){g.setColour(kTextSub);g.setFont(chinese?spectrumtag::ui::chineseFont(12.0f):juce::Font(12.0f));g.drawText(juce::String::fromUTF8(SPECTRUMTAG_ARA?(chinese?"选择 DAW 音频事件，然后点击加载":"Select DAW audio events, then click Load"):(chinese?"录音就绪 — 播放 DAW 开始采集":"Record armed - play the DAW to capture audio")),overviewBounds,juce::Justification::centred);return;}
        const double length=juce::jmax(.001,recorded->end()-timelineStart());
        const auto x=[&](double t){return overviewBounds.getX()+(float)((t-timelineStart())/length)*overviewBounds.getWidth();};
        {juce::Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(overviewBounds);
        const float centre=(float)overviewBounds.getCentreY(),height=overviewBounds.getHeight()*.42f;
        if(overviewSource!=recorded||overviewPalette!=palette||overviewImage.getWidth()!=overviewBounds.getWidth()||overviewImage.getHeight()!=overviewBounds.getHeight()){
        overviewImage=juce::Image(juce::Image::ARGB,overviewBounds.getWidth(),overviewBounds.getHeight(),true);juce::Graphics peaks(overviewImage);
        peaks.setColour(juce::Colour(spectrumtag::palettes[(size_t)palette].swatch));
        for(int pixel=0;pixel<overviewBounds.getWidth();++pixel){
            const double a=(timelineStart()+length*pixel/overviewBounds.getWidth())*recorded->sampleRate-recorded->origin;
            const double b=(timelineStart()+length*(pixel+1)/overviewBounds.getWidth())*recorded->sampleRate-recorded->origin;
            float low=0,high=0;for(int64_t i=(int64_t)a/128;i<=(int64_t)b/128;++i)if(i>=0&&i<(int64_t)recorded->peaks.size()){low=juce::jmin(low,recorded->peaks[(size_t)i].first);high=juce::jmax(high,recorded->peaks[(size_t)i].second);}
            peaks.drawVerticalLine(pixel,centre-overviewBounds.getY()-juce::jlimit(0.0f,1.0f,high)*height,centre-overviewBounds.getY()-juce::jlimit(-1.0f,0.0f,low)*height+.5f);
        }
        overviewSource=recorded;overviewPalette=palette;}
        g.drawImageAt(overviewImage,overviewBounds.getX(),overviewBounds.getY());
        const auto viewRect=overviewViewRect();
        if(!viewRect.isEmpty()){
            g.setColour(juce::Colours::white.withAlpha(.09f));g.fillRect(viewRect);
            g.setColour(juce::Colours::white.withAlpha(.72f));g.drawRect(viewRect.reduced(0.f,1.f),1.f);
            if(viewRect.getWidth()>=18.f){
                // Edge grips: drag them to resize the visible span (Speed).
                const float gripW=2.5f,gripTop=viewRect.getY()+5.f,gripH=juce::jmax(6.f,viewRect.getHeight()-10.f);
                g.setColour(juce::Colours::white.withAlpha(overviewHandle==OverviewHandle::ResizeLeft?.95f:.55f));
                g.fillRect(viewRect.getX()+1.f,gripTop,gripW,gripH);
                g.setColour(juce::Colours::white.withAlpha(overviewHandle==OverviewHandle::ResizeRight?.95f:.55f));
                g.fillRect(viewRect.getRight()-1.f-gripW,gripTop,gripW,gripH);
            }
        }
        for(const auto& edit:recorded->edits){g.setColour(edit.id==selected?juce::Colours::white:juce::Colours::white.withAlpha(.4f));g.fillRect(x(edit.start),(float)overviewBounds.getBottom()-5,juce::jmax(1.0f,x(edit.start+edit.duration)-x(edit.start)),3.0f);}
        paintCursor(g,overviewBounds,x(processor.audition().position()),true);}
    }
    double baseSpan() const{return juce::jmax(.1,(double)contentBounds.getWidth()/(spectrumtag::timing::pixelsPerSecond*juce::jmax(.01f,processor.getAPVTS().getParameter(ParameterIDs::speed)->convertFrom0to1(processor.getAPVTS().getParameter(ParameterIDs::speed)->getValue()))));}
    float frequency(float y,float nyquist) const{return spectrumtag::frequency::fromY(y,nyquist,scaleMode);}
    void updateImageBounds(){imageBox->setContentBounds({0,0,contentBounds.getWidth(),contentBounds.getHeight()});}
    void render(){
        dirty=false;const int width=contentBounds.getWidth(),height=contentBounds.getHeight();if(width<1||height<1)return;
        // Wait for the snapshot without removed edits, rather than requeueing
        // the old Print pixels after invalidation. Snapshot arrival marks dirty.
        if(recorded){const auto desired=processor.recording().edits();if(desired&&std::any_of(recorded->edits.begin(),recorded->edits.end(),[&](const auto& e){return std::none_of(desired->begin(),desired->end(),[&](const auto& current){return current.id==e.id;});}))return;}
        spectrumtag::ViewRenderer::Request request{recorded,width,height,palette,scaleMode,topHz(),view.endTime-timeSpan(),view.endTime,{}};
        if(!recorded){
            const auto& frames=history->frames;
            auto first=std::lower_bound(frames.begin(),frames.end(),request.start,[](const auto& f,double t){return f->time<t;});
            if(first!=frames.begin())--first;
            auto last=std::upper_bound(first,frames.end(),request.end,[](double t,const auto& f){return t<f->time;});
            if(last!=frames.end())++last;
            request.live.assign(first,last);
        }
        requestedRaster=renderer.request(std::move(request));
    }
    SpectrumTagAudioProcessor& processor;
    juce::Typeface::Ptr typeface;
    std::shared_ptr<spectrumtag::SpectrumHistory> history;
    std::vector<spectrumtag::SpectrumFrame> incoming;
    spectrumtag::SpectrumViewport view;
    juce::Component imageClip;
    FitSpectrumButton fitButton;
    RoundPrintButton printImageButton {"Print"};   // 未打印图片对象的打印按钮，贴在图片下方居中
    std::unique_ptr<ImageBoxComponent> imageBox;
    juce::Rectangle<int> contentBounds,axisBounds,statusBounds,overviewBounds;
    std::shared_ptr<const spectrumtag::RecordingSnapshot> recorded;
    std::shared_ptr<const std::vector<spectrumtag::PrintEdit>> observedEdits;
    juce::String selected;
    juce::String imageRevealId;  // 正被图片接替的打印：绘制时只挖它的频带
    std::optional<spectrumtag::PrintEdit> preview,dragEdit;bool previewReleased=false,movingPrint=false,resizingPrint=false;
    bool draggingImage=false;   // 已退回未打印态的图片正跟随当前拖动
    bool revertGesture=false;   // 本次手势由“撤掉打印”开启，松手时必须收尾
    juce::Image previewImage;juce::Point<float> dragPoint;juce::Rectangle<float> dragBounds;
    bool recordMode=false,chinese=true,fit=false,restored=false,wasPlaying=false,wasArmed=false,fitAfterCapture=false;
    bool lastHasImage=false,lastAudioLoaded=false,audioAvailableSeen=false;
    juce::uint32 audioHintFlashUntil=0;
    OverviewHandle overviewHandle=OverviewHandle::None;
    float lastTop=0;
    spectrumtag::ViewRenderer renderer;
    uint64_t requestedRaster=0,appliedRaster=0,rasterEpoch=0,lastClearSerial=0;
    uint64_t paintCount=0,paintedRaster=0;double lastPaintTime=0;
    bool panActive=false;double panTarget=0,panTick=0;
    juce::Image overviewImage;std::shared_ptr<const spectrumtag::RecordingSnapshot> overviewSource;int overviewPalette=-1;
    double lastCursor=-1;
    juce::Image waterfall;std::shared_ptr<const spectrumtag::RecordingSnapshot> waterfallSource;double waterfallStart=0,waterfallEnd=0;
    std::array<juce::Colour,256> lut;
    int palette=0,scaleMode=0;float lastSpeed=-1;bool dirty=true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumView)
};

class PaletteButton final : public juce::Button {
public:
    explicit PaletteButton(int index):juce::Button(spectrumtag::palettes[(size_t)index].name),colour(spectrumtag::palettes[(size_t)index].swatch){setTooltip(getName());}
    void paintButton(juce::Graphics& g,bool over,bool down) override {
        const auto bounds=getLocalBounds().toFloat().reduced(5);
        g.setColour((over||down)?colour.brighter(.15f):colour);g.fillEllipse(bounds);
        if(getToggleState()){g.setColour(kTextWhite);g.drawEllipse(bounds.expanded(3),1.5f);}
    }
private:juce::Colour colour;
};

// ============================================================================
//  Editor
// ============================================================================
SpectrumTagAudioProcessorEditor::SpectrumTagAudioProcessorEditor (SpectrumTagAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
#if SPECTRUMTAG_ARA
    ,juce::AudioProcessorEditorARAExtension(&p)
#endif
    , processor (p)
{
    // 通知 Processor：有 UI 在场。
    //  Processor 会据此决定 automationPrintRequest 由谁消费：
    //    - 有 Editor 且实时模式：由 Editor timer（消息线程）消费 → onPrintClicked；
    //    - 无 Editor 或离线导出：由音频线程消费 → triggerPrintFromAutomationInternal。
    processor.notifyEditorAttached();

    basementTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BasementGrotesqueBlack_v1_202_otf,
        BinaryData::BasementGrotesqueBlack_v1_202_otfSize);

    setLookAndFeel (&lookAndFeel);
    setComponentID("editor");
    tooltipLookAndFeel.setDefaultSansSerifTypefaceName(spectrumtag::ui::chineseFontName());
    tooltipWindow.setLookAndFeel(&tooltipLookAndFeel);
    // 右上角三个工具入口（诊断 / 音频设备 / 语言）统一为只有描边的按钮。
    tooltipLanguageButton.getProperties().set("spectrumTagStyle","outline");
    chineseTips=processor.getEditorState().chinese;
    tooltipLanguageButton.setComponentID("tooltipLanguage");
    tooltipLanguageButton.onClick=[this]{beginChange();chineseTips=!chineseTips;spectrumtag::Preferences::remember(nullptr,nullptr,&chineseTips);refreshLanguage();changed();finishChange(true);};
    addAndMakeVisible(tooltipLanguageButton);
    diagnosticsButton.getProperties().set("spectrumTagStyle","outline");
    diagnosticsButton.setComponentID("exportDiagnostics");
    diagnosticsButton.onClick=[this]{exportDiagnostics();};addAndMakeVisible(diagnosticsButton);

    // ---- 顶部 ----
    titleLabel.setText (SPECTRUMTAG_ARA?"SpectrumTag ARA":"SpectrumTag", juce::dontSendNotification);
    styleHeaderLabel (titleLabel, SPECTRUMTAG_ARA?25.0f:32.0f, kTextWhite);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (juce::String ("v") + (processor.filePlayerMode&&processor.filePlayerVersion.isNotEmpty()?processor.filePlayerVersion:juce::String(JucePlugin_VersionString)), juce::dontSendNotification);
    styleHeaderLabel (versionLabel, 18.0f, kTextWhite);
    addAndMakeVisible (versionLabel);

    websiteLabel = std::make_unique<HyperlinkLabel> ("iisaacbeats.cn",
                                                     juce::URL ("https://iisaacbeats.cn"));
    websiteLabel->setTypeface (basementTypeface);
    websiteLabel->setFontHeight (18.0f);
    addAndMakeVisible (*websiteLabel);

    // ---- 频谱视图（内含图片框）----
    spectrumView = std::make_unique<SpectrumView> (processor, basementTypeface);
    addAndMakeVisible (*spectrumView);

    // 图片框回调：选图/拖动后写回 Processor
    spectrumView->getImageBox().onChanged = [this] {
        // 拖动只改位置/大小；聚焦对象是否退回未打印态由松手（onGestureEnd）决定。
        if(!suppressChanges)changed(false);
        persistEditorStateToProcessor();
    };
    spectrumView->getImageBox().onGestureStart=[this]{
        beginChange();gestureActive=true;
    };
    // Ctrl+拖动：原对象留在原位打印，副本继续被这次拖动带走。
    spectrumView->getImageBox().onDuplicateDrag=[this]{duplicateImageObject();};
    spectrumView->getImageBox().onGestureEnd=[this]{
        gestureActive=false;
        // 聚焦态下拖动/缩放图片属于“需要重新渲染”的修改：撤掉已打印的图案，
        // 对象退回未打印态，图片停在新位置等待重新打印。
        if(spectrumView->selectedPrint())unprintFocused();
        finishChange(true);
    };
    spectrumView->getImageBox().onFileLoadStart=[this]{beginChange();};
    spectrumView->onSelected=[this](const juce::String& id){selectPrinted(id);};
    // 未加载音频时，点击画布上的引导条等同按"加载音频 / Load audio"。
    spectrumView->onRequestAudio=[this]{if(!processor.isClearPending())recordButton.onClick();};
    spectrumView->onImagePrint=[this]{onPrintClicked();};
    spectrumView->onDelete=[this]{removeSelected();};
    spectrumView->onNavigation=[this]{finishChange(true);changed();};
    spectrumView->onWheelSpeed=[this](float value){
        wheelChange=true;auto* p=processor.getAPVTS().getParameter(ParameterIDs::speed);
        p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();wheelChange=false;changed();
    };
    spectrumView->onMoveBegin=[this]{beginChange();gestureActive=true;};
    // 直接拖动已打印对象（状态2/状态3）：撤掉这次打印、图片按打印范围摆回，
    // 剩下的位移由同一手势继续作用在图片上；松手时整体提交，算一个撤销步。
    spectrumView->onRevertFocused=[this]{finishChange(true);beginChange();gestureActive=true;unprintFocused();};
    spectrumView->onRevertEnd=[this]{gestureActive=false;finishChange(true);};
    spectrumView->onMoveEnd=[this](const spectrumtag::PrintEdit&){
        // 聚焦对象被拖动/缩放：撤掉打印、退回未打印态。
        spectrumView->setPreview({});
        unprintFocused();
        finishChange(true);
    };
    spectrumView->getImageBox().onImageRequested = [this] (const juce::File& file) { imagePicked(file); };
    for(int i=0;i<8;++i){
        auto button=std::make_unique<PaletteButton>(i);button->setComponentID("palette"+juce::String(i));
        button->onClick=[this,i]{beginChange();selectPalette(i,true);changed();finishChange(true);};addAndMakeVisible(*button);paletteButtons[(size_t)i]=std::move(button);
    }

    // ---- 控件标签 ----
    styleControlLabel (fftSizeLabel);
    styleControlLabel (fftScaleLabel);
    styleControlLabel (speedLabel);
    styleControlLabel (amplitudeLabel);
    styleControlLabel (invertLabel);
    addAndMakeVisible (fftSizeLabel);
    addAndMakeVisible (fftScaleLabel);
    addAndMakeVisible (speedLabel);
    timeSpanLabel.setComponentID("timeSpan");timeSpanLabel.setJustificationType(juce::Justification::centredRight);
    timeSpanLabel.setColour(juce::Label::textColourId,juce::Colours::lightgrey);addAndMakeVisible(timeSpanLabel);
    addAndMakeVisible (amplitudeLabel);
    addAndMakeVisible (invertLabel);

    // ---- 控件 ----
    fftSizeCombo.setComponentID("fftSize");
    fftSizeCombo.addItemList ({ "1024", "2048", "4096" }, 1);
    fftScaleCombo.addItem("Linear",1);fftScaleCombo.addItem("Mel (RX)",5);fftScaleCombo.addItem("Bark",6);
    fftScaleCombo.addItem("Log",2);fftScaleCombo.addItem("Extended Log",7);
    fftScaleCombo.addItem("insight",3);
    addAndMakeVisible (fftSizeCombo);
    addAndMakeVisible (fftScaleCombo);

    speedSlider.setComponentID("speed");fftScaleCombo.setComponentID("fftScale");imageModeCombo.setComponentID("imageMode");invertToggle.setComponentID("invert");
    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible (speedSlider);

    amplitudeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amplitudeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    amplitudeSlider.setComponentID("amplitudeRatio");
    amplitudeSlider.setTooltip("Gain in the affected image area: 0-33%. The image preview follows this value and Invert.");
    addAndMakeVisible (amplitudeSlider);

    invertToggle.setButtonText ({});
    addAndMakeVisible (invertToggle);

    noiseFillSlider.setEnabled(false);
    noiseFillSlider.setComponentID("noiseFill");noiseFillSlider.setSliderStyle(juce::Slider::LinearHorizontal);noiseFillSlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,56,22);
    styleControlLabel(noiseFillLabel);addAndMakeVisible(noiseFillLabel);addAndMakeVisible(noiseFillSlider);
    printButton.setComponentID("printButton");
    printButton.setTypeface (basementTypeface);
    recordButton.setComponentID("recordButton");removePrintButton.setComponentID("removePrint");clearRecordButton.setComponentID("clearRecord");
    copyImageButton.setComponentID("copyImage");copyImageButton.onClick=[this]{if(copyButtonPaste)pasteImageObject();else copyImageObject();refreshCopyButton();};
    previousPrintButton.setComponentID("previousPrint");nextPrintButton.setComponentID("nextPrint");
    recordButton.onClick=[this]{
        if(processor.filePlayerMode){finishChange(true);if(processor.onLoadAudioFile)processor.onLoadAudioFile();return;}
#if SPECTRUMTAG_ARA
        finishChange(true);processor.loadARA();return;
#endif
        if(processor.isClearPending())return;
        finishChange(true);persistEditorStateToProcessor();
        if(!processor.recording().isArmed()){recordBefore=processor.captureUserState();processor.recording().setArmed(true);}
        else processor.recording().setArmed(false);
        spectrumView->resized();stableState=processor.captureUserState();
    };
    clearRecordButton.onClick=[this]{
        if(processor.filePlayerMode&&processor.onFileSeek)processor.onFileSeek(0);
        finishChange(true);persistEditorStateToProcessor();recordBefore.reset();
#if SPECTRUMTAG_ARA
        processor.clearARA();
#else
        processor.clearRecordedAudio();
#endif
        // 清空 = 音频 + 全部图片对象一起清除；源图与缓存的图片引用都不保留。
        spectrumView->getImageBox().clearImage();
        spectrumView->selectEdit({});placementId={};spectrumView->clearDisplay();spectrumView->refreshCanvas();
        const juce::String noImage;
        spectrumtag::Preferences::remember(&noImage,nullptr);
        persistEditorStateToProcessor();stableState=processor.captureUserState();
    };
    removePrintButton.onClick=[this]{removeSelected();};previousPrintButton.onClick=[this]{navigatePrint(false);};nextPrintButton.onClick=[this]{navigatePrint(true);};
    for(auto* b:std::initializer_list<juce::Button*>{&recordButton,&removePrintButton,&copyImageButton,&clearRecordButton,&previousPrintButton,&nextPrintButton})addAndMakeVisible(b);
    if(processor.isLoadMode()){recordButton.setComponentID("loadButton");clearRecordButton.setComponentID("clearLoad");}
    frequencyRangeLabel.setComponentID("frequencyRangeLabel");fftSizeLabel.setComponentID("fftSizeLabel");frequencyRangeCombo.setComponentID("frequencyRange");frequencyRangeCombo.addItemList({"20 kHz","24 kHz","22.05 kHz","48 kHz"},1);
    addAndMakeVisible(frequencyRangeCombo);addAndMakeVisible(frequencyRangeLabel);
    printButton.onClick = [this] { onPrintClicked(); };
    addAndMakeVisible (printButton);
    printButton.setVisible(false); // 打印按钮不再显示：图片放置模式下点击频谱自动打印。

    styleControlLabel(imageModeLabel); styleControlLabel(imageAmountLabel);
    addAndMakeVisible(imageModeLabel); addAndMakeVisible(imageAmountLabel);
    imageModeCombo.addItemList({"Auto (Otsu)","Detail (Adaptive)","Manual","Alpha silhouette"},1);
    addAndMakeVisible(imageModeCombo);
    imageAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    imageAmountSlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,48,20);
    imageAmountSlider.setDoubleClickReturnValue(true,0.5,juce::ModifierKeys::altModifier);
    imageAmountSlider.setComponentID("imageAmount");
    imageAmountSlider.setTooltip("Auto/Detail: higher values make more pixels black. Manual: brightness threshold. Alpha: opacity threshold. Alt + left-click or double-click resets to 0.5.");
    addAndMakeVisible(imageAmountSlider);

    // ---- APVTS Attachments ----
    auto& vts = processor.getAPVTS();
    fftSizeAttachment=std::make_unique<juce::ParameterAttachment>(*vts.getParameter(ParameterIDs::fftSize),[this](float value){const int index=juce::jmin(2,(int)value);fftSizeCombo.setSelectedId(index+1,juce::dontSendNotification);processor.recording().setDisplayFft(1024<<index);},nullptr);
    fftSizeAttachment->sendInitialUpdate();
    fftSizeCombo.onChange=[this]{fftSizeAttachment->setValueAsCompleteGesture((float)fftSizeCombo.getSelectedItemIndex());changed(true);};
    auto scaleChanged=[this](float){if(!changingScaleControl)syncFrequencyScale();};
    fftScaleAttachment=std::make_unique<juce::ParameterAttachment>(*vts.getParameter(ParameterIDs::fftScale),scaleChanged,nullptr);
    presetScaleAttachment=std::make_unique<juce::ParameterAttachment>(*vts.getParameter(ParameterIDs::frequencyPreset),scaleChanged,nullptr);
    fftScaleAttachment->sendInitialUpdate();
    speedAttachment     = std::make_unique<SliderAttachment> (vts, ParameterIDs::speed,          speedSlider);
    amplitudeAttachment = std::make_unique<SliderAttachment> (vts, ParameterIDs::amplitudeRatio, amplitudeSlider);
    invertAttachment    = std::make_unique<ButtonAttachment> (vts, ParameterIDs::invert,         invertToggle);

    imageModeAttachment=std::make_unique<ComboAttachment>(vts,ParameterIDs::imageMode,imageModeCombo);
    imageAmountAttachment=std::make_unique<SliderAttachment>(vts,ParameterIDs::imageAmount,imageAmountSlider);
    noiseFillAttachment=std::make_unique<SliderAttachment>(vts,ParameterIDs::noiseFill,noiseFillSlider);
    frequencyRangeAttachment=std::make_unique<ComboAttachment>(vts,ParameterIDs::frequencyRange,frequencyRangeCombo);
    for(auto* slider:{&speedSlider,&amplitudeSlider,&imageAmountSlider,&noiseFillSlider}){
        slider->onDragStart=[this]{beginChange();gestureActive=true;};
        slider->onDragEnd=[this]{gestureActive=false;changedAt=juce::Time::getMillisecondCounter();};
        slider->onValueChange=[this,slider]{
            if(suppressChanges||processor.isRestoringUserState())return;
            if(slider==&speedSlider&&wheelChange){changed(true);return;}
            if(slider==&speedSlider)spectrumView->useAbsoluteSpeed((float)speedSlider.getValue());
            syncImagePreview();changed(true);
        };
    }
    invertToggle.onClick=[this]{syncImagePreview();changed(true);};
    imageModeCombo.onChange=[this]{syncImagePreview();changed(true);};
    fftScaleCombo.onChange=[this]{
        const double imageAnchor=spectrumView->imageStart();
        const int mode=(fftScaleCombo.getSelectedId()-1);changingScaleControl=true;
        if(mode<2)fftScaleAttachment->setValueAsCompleteGesture((float)mode);
        presetScaleAttachment->setValueAsCompleteGesture((float)juce::jmax(0,mode-1));
        if(mode==spectrumtag::frequency::insight){
            // An explicit preset choice calibrates timing. Host automation and
            // selecting an existing Print only recall their stored settings.
            const bool previous=suppressChanges;suppressChanges=true;
            auto setValue=[this](const juce::String& id,float value){auto* p=processor.getAPVTS().getParameter(id);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();};
            setValue(ParameterIDs::speed,(float)spectrumtag::timing::presetSpeed(mode,spectrumView->getContentBounds().getHeight()));
            spectrumView->setScaleMode(mode);
            spectrumView->matchPresetTime(imageAnchor);
            suppressChanges=previous;
        }
        changingScaleControl=false;syncFrequencyScale();processor.rememberARAFrequencyChoice();
    };
    frequencyRangeCombo.onChange=[this]{changed(true);};
    spectrumView->setScaleMode((fftScaleCombo.getSelectedId()-1));

    // All controls live in a fixed logical canvas. Transforming it scales text,
    // hit targets and plots together, without changing Print duration or DSP.
    #if !SPECTRUMTAG_ARA
    if(!processor.filePlayerMode){
    uiScaleCombo.setComponentID("uiScale");
    for(int percent=75;percent<=250;percent+=25)uiScaleCombo.addItem(juce::String(percent)+"%",percent);
    uiScaleCombo.onChange=[this]{const int percent=uiScaleCombo.getSelectedId();if(percent>=75&&percent<=250){beginChange();
#if SPECTRUMTAG_ARA
        requestEditorScale(percent);
#else
        setSize(juce::roundToInt(kDefaultWidth*percent/100.0),juce::roundToInt(kDefaultHeight*percent/100.0));
#endif
        changed();}};
    addAndMakeVisible(uiScaleCombo);
    }
    #endif
    std::vector<juce::Component*> controls;for(auto* child:getChildren())controls.push_back(child);
    for(auto* child:controls)scaledContent.addAndMakeVisible(child);
    addAndMakeVisible(scaledContent);scaleReady=true;
    // 「显示上限」暂时屏蔽显示（addAndMakeVisible 会重新显示子组件，必须放在重挂之后）：
    // 参数、附件与重渲染逻辑全部保留，默认 22.05 kHz。
    frequencyRangeCombo.setVisible(false);frequencyRangeLabel.setVisible(false);
#if SPECTRUMTAG_ARA
    clearRecordButton.setVisible(true);
#endif
    // ---- 等比缩放 ----
#if SPECTRUMTAG_ARA
    // The host owns the ARA panel. A second corner grip would resize only its child.
    setResizable(true,false);
    resizeConstrainer.setSizeLimits(320,180,16384,16384);
#else
    setResizable (true, !processor.filePlayerMode);
    const double aspect = (double) kDefaultWidth / (double) kDefaultHeight;
    resizeConstrainer.setFixedAspectRatio (aspect);
    resizeConstrainer.setSizeLimits (930,510,3100,1700);
#endif
    setConstrainer (&resizeConstrainer);
    const float rememberedScale=processor.isLoadMode()?1.f:processor.getEditorState().uiScale;
    setSize(juce::roundToInt(kDefaultWidth*rememberedScale),juce::roundToInt(kDefaultHeight*rememberedScale));

    // ---- 恢复保存的状态（图片路径 + 框位置）----
    restoreEditorStateFromProcessor();
    syncImagePreview();
    refreshLanguage();
    spectrumView->tick();suppressChanges=false;persistEditorStateToProcessor();stableState=processor.captureUserState();
    scaleNeedsSave=false; // Opening another instance is not a new size preference.
    lastVisualisedSamples=processor.getVisualisedSamples();

    startTimerHz (60);

#if SPECTRUMTAG_ENABLE_NETWORK
    telemetrySession = std::make_unique<iisaac::telemetry::Session> (
        iisaac::telemetry::forPlugin ("spectrumtag", JucePlugin_VersionString,
                                     JucePlugin_VersionString, processor.wrapperType));
#endif
}

SpectrumTagAudioProcessorEditor::~SpectrumTagAudioProcessorEditor()
{
#if SPECTRUMTAG_ARA
    if(auto* view=processor.getEditorView<spectrumtag::ara::View>())view->beforeSelection={};
#endif
    finishChange(true);persistEditorStateToProcessor();
    if(scaleNeedsSave)spectrumtag::Preferences::remember(nullptr,nullptr,nullptr,&uiScale);
    tooltipWindow.setLookAndFeel(nullptr);tooltipLanguageButton.setLookAndFeel(nullptr);
    telemetrySession.reset();
    setConstrainer (nullptr);
    stopTimer();
    setLookAndFeel (nullptr);
    processor.notifyEditorDetached();
}

// ============================================================================
//  文件拖入：用户可以从外部文件夹直接把图片拖到插件窗口
// ============================================================================
bool SpectrumTagAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if(processor.filePlayerMode&&juce::StringArray{".wav",".wave",".aif",".aiff",".flac",".ogg",".mp3",".m4a",".aac",".wma",".opus",".mp4",".m4v",".mov",".mkv",".webm",".avi"}.contains(ext))return true;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".bmp" || ext == ".gif")
            return true;
    }
    return false;
}

void SpectrumTagAudioProcessorEditor::filesDropped (const juce::StringArray& files,
                                                    int /*x*/, int /*y*/)
{
    if (spectrumView == nullptr) return;

    for (auto& f : files)
    {
        juce::File file (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (! file.existsAsFile()) continue;
        if(processor.filePlayerMode&&juce::StringArray{".wav",".wave",".aif",".aiff",".flac",".ogg",".mp3",".m4a",".aac",".wma",".opus",".mp4",".m4v",".mov",".mkv",".webm",".avi"}.contains(ext)){if(processor.onDropAudioFile)processor.onDropAudioFile(file);break;}
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg"
            && ext != ".bmp" && ext != ".gif") continue;

        // imagePicked 负责音频门禁、先打印上一张未打印图片、载入与状态更新。
        imagePicked(file);
        break;   // 只取第一张有效图片
    }
}

void SpectrumTagAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int) {}
void SpectrumTagAudioProcessorEditor::fileDragExit  (const juce::StringArray&)         {}

void SpectrumTagAudioProcessorEditor::styleHeaderLabel (juce::Label& l, float h, juce::Colour c)
{
    l.setColour (juce::Label::textColourId, c);
    l.setFont (juce::Font (basementTypeface).withHeight (h));
    l.setJustificationType (juce::Justification::centredLeft);
}

void SpectrumTagAudioProcessorEditor::styleControlLabel (juce::Label& l)
{
    l.setColour (juce::Label::textColourId, kTextWhite);
    l.setFont (juce::Font (basementTypeface).withHeight (18.0f));
    l.setJustificationType (juce::Justification::centredLeft);
}

void SpectrumTagAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBgColour);
}

void SpectrumTagAudioProcessorEditor::resized()
{
    if(!scaleReady)return;
    const bool followsOuterFrame=processor.isLoadMode();
    uiScale=followsOuterFrame?juce::jlimit(.1f,2.5f,juce::jmin((float)getWidth()/kDefaultWidth,(float)getHeight()/kDefaultHeight))
                            :juce::jlimit(.75f,2.5f,(float)getHeight()/kDefaultHeight);
    const int canvasWidth=followsOuterFrame?juce::roundToInt(getWidth()/uiScale):kDefaultWidth;
    const int canvasHeight=followsOuterFrame?juce::roundToInt(getHeight()/uiScale):kDefaultHeight;
    scaledContent.setBounds(0,0,canvasWidth,canvasHeight);
    scaledContent.setTransform(juce::AffineTransform::scale(uiScale));
    const int percent=juce::roundToInt(uiScale*100);
    if(percent%25==0)uiScaleCombo.setSelectedId(percent,juce::dontSendNotification);
    else uiScaleCombo.setText(juce::String(percent)+"%",juce::dontSendNotification);
#if SPECTRUMTAG_ARA
    if(!hostLayoutPass)
#endif
    {scaleNeedsSave=true;scaleChangedAt=juce::Time::getMillisecondCounter();if(!suppressChanges)changed();}
    const float scale = 1.0f;
    auto px = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto r = juce::Rectangle<int>(0,0,canvasWidth,canvasHeight);

    // ===== 顶部 =====
    tooltipLanguageButton.setBounds(canvasWidth-px(100),px(14),px(80),px(26));
    // 独立版需要在语言与诊断按钮之间放音频设备按钮，诊断按钮左移腾位。
    diagnosticsButton.setBounds(canvasWidth-px(processor.isLoadMode()?348:232),px(14),px(124),px(26));
    uiScaleCombo.setBounds(20,52,90,28);
    const int headerH = px (90);
    auto header = r.removeFromTop (headerH).reduced (px (20), px (8)).withHeight(px(38));
    const int toolbarOffset=processor.isLoadMode()?105:0;
    for(int i=0;i<8;++i)if(paletteButtons[(size_t)i])paletteButtons[(size_t)i]->setBounds(px(125-toolbarOffset+i*35),px(52),px(30),px(30));
    const int recordW=processor.isLoadMode()?104:84;
    const int recordX=459-toolbarOffset;
    recordButton.setBounds(recordX,52,recordW,28);
    removePrintButton.setBounds(recordX+recordW+8,52,75,28);
    // 复制/粘贴按钮紧贴删除按钮右侧，导航箭头顺势右移。
    copyImageButton.setBounds(recordX+recordW+91,52,68,28);
    previousPrintButton.setBounds(recordX+recordW+167,52,70,28);
    nextPrintButton.setBounds(recordX+recordW+243,52,70,28);
    clearRecordButton.setBounds(canvasWidth-300-46,52,34,28);
    frequencyRangeLabel.setBounds(canvasWidth-300,52,142,28);frequencyRangeCombo.setBounds(canvasWidth-158,52,138,28);

    titleLabel.setBounds (header.removeFromLeft (px (250)));
    header.removeFromLeft (px (8));
    versionLabel.setBounds (header.removeFromLeft (px (80)));
    header.removeFromLeft (px (4));
    websiteLabel->setBounds (header.removeFromLeft (px (200)));

    titleLabel.setFont   (juce::Font (basementTypeface).withHeight ((SPECTRUMTAG_ARA?25.0f:32.0f) * scale));
    versionLabel.setFont (juce::Font (basementTypeface).withHeight (18.0f * scale));
    websiteLabel->setFontHeight (18.0f * scale);

    // ===== 主体 =====
    auto body = r.reduced (px (20), 0).withTrimmedBottom (px (20));
    const int rightPanelW = px (280);
    auto rightPanel = body.removeFromRight (rightPanelW);
    body.removeFromRight (px (20));

    if (spectrumView != nullptr)
        spectrumView->setBounds (body);

    // ===== 右侧控件 =====
    const int labelH   = px (22);
    const int comboH   = px (28);
    const int sliderH  = px (18);
    const int spacing  = px (10);
    const int rowGap   = px (16);

    auto controlFont = chineseTips?spectrumtag::ui::chineseFont(18.0f):juce::Font(basementTypeface).withHeight(18.0f);
    fftSizeLabel.setFont (controlFont);
    fftScaleLabel.setFont (controlFont);
    speedLabel.setFont (controlFont);
    amplitudeLabel.setFont (controlFont);
    invertLabel.setFont (controlFont);

    auto layoutSameLine = [&] (juce::Rectangle<int>& panel, juce::Label& lbl, juce::ComboBox& combo)
    {
        auto row = panel.removeFromTop (juce::jmax (labelH, comboH));
        auto comboW = px (138);
        combo.setBounds (row.removeFromRight (comboW).withSizeKeepingCentre (comboW, comboH));
        lbl.setBounds (row);
        panel.removeFromTop (rowGap);
    };

    layoutSameLine (rightPanel, fftSizeLabel,  fftSizeCombo);
    layoutSameLine (rightPanel, fftScaleLabel, fftScaleCombo);

    auto speedRow=rightPanel.removeFromTop(labelH);
    speedLabel.setBounds(speedRow.removeFromLeft(85));timeSpanLabel.setBounds(speedRow);
    timeSpanLabel.setFont(spectrumtag::ui::chineseFont(14.f));
    rightPanel.removeFromTop (spacing);
    speedSlider.setBounds (rightPanel.removeFromTop (sliderH));
    rightPanel.removeFromTop (rowGap);

    amplitudeLabel.setBounds (rightPanel.removeFromTop (labelH));
    rightPanel.removeFromTop (spacing);
    amplitudeSlider.setBounds (rightPanel.removeFromTop (sliderH));
    rightPanel.removeFromTop (rowGap);

    imageModeLabel.setFont(controlFont); imageAmountLabel.setFont(controlFont);
    imageModeLabel.setBounds(rightPanel.removeFromTop(labelH));
    imageModeCombo.setBounds(rightPanel.removeFromTop(comboH));
    rightPanel.removeFromTop(px(10));
    imageAmountLabel.setBounds(rightPanel.removeFromTop(labelH));
    imageAmountSlider.setBounds(rightPanel.removeFromTop(px(24)));
    rightPanel.removeFromTop(px(10));

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

    rightPanel.removeFromTop(px(4));noiseFillLabel.setBounds(rightPanel.removeFromTop(labelH));
    noiseFillSlider.setBounds(rightPanel.removeFromTop(px(24)));
}

// ----------------------------------------------------------------------------
//  独立版覆盖控件的落点（编辑器本地坐标）。与 resized() 使用同一套画布换算，
//  由独立版把播放/导出/音频设备/状态标签叠放到编辑器上方。
// ----------------------------------------------------------------------------
SpectrumTagAudioProcessorEditor::StandaloneSlots SpectrumTagAudioProcessorEditor::standaloneSlots() const
{
    StandaloneSlots slots;
    const bool followsOuterFrame=processor.isLoadMode();
    const float s=followsOuterFrame?juce::jlimit(.1f,2.5f,juce::jmin((float)getWidth()/kDefaultWidth,(float)getHeight()/kDefaultHeight))
                                   :juce::jlimit(.75f,2.5f,(float)getHeight()/kDefaultHeight);
    const int canvasWidth=followsOuterFrame?juce::roundToInt(getWidth()/s):kDefaultWidth;
    const int canvasHeight=followsOuterFrame?juce::roundToInt(getHeight()/s):kDefaultHeight;
    const auto map=[&](juce::Rectangle<int> canvasRect){
        return juce::Rectangle<int>(juce::roundToInt(canvasRect.getX()*s),juce::roundToInt(canvasRect.getY()*s),
                                    juce::roundToInt(canvasRect.getWidth()*s),juce::roundToInt(canvasRect.getHeight()*s));
    };
    // 播放：频谱下方波形预览左侧的频率刻度列，与预览条垂直居中。
    // 频谱视图位于画布 (20, 90, W-40, H-110)，波形预览条画布纵向为 [H-122, H-40]。
    slots.play=map({29,canvasHeight-107,52,52});
    // 导出：占据原 Print 按钮的右下角位置（Print 已改为点击频谱自动触发）。
    slots.exportButton=map({canvasWidth-148,canvasHeight-148,128,128});
    // 音频设备：切换语言与导出诊断按钮之间（诊断按钮在加载模式左移至 348）。
    slots.device=map({canvasWidth-216,14,108,26});
    // 文件名 / 导出进度：标题行网站链接右侧。
    slots.fileName=map({574,8,juce::jmax(80,canvasWidth-938),38});
    return slots;
}

// ----------------------------------------------------------------------------
void SpectrumTagAudioProcessorEditor::syncFrequencyScale() {
    auto value=[this](const juce::String& id){auto* p=processor.getAPVTS().getParameter(id);return (int)juce::roundToInt(p->convertFrom0to1(p->getValue()));};
    const int preset=value(ParameterIDs::frequencyPreset);
    const int mode=preset==2?1:preset>0?preset+1:value(ParameterIDs::fftScale);
    fftScaleCombo.setSelectedId(mode+1,juce::dontSendNotification);spectrumView->setScaleMode(mode);
    changed(true);
}
void SpectrumTagAudioProcessorEditor::syncImagePreview()
{
    if(spectrumView==nullptr) return;
    auto& state=processor.getAPVTS();
    spectrumView->getImageBox().setConversion((int)*state.getRawParameterValue(ParameterIDs::imageMode),
        *state.getRawParameterValue(ParameterIDs::imageAmount), *state.getRawParameterValue(ParameterIDs::invert)>0.5f,
        *state.getRawParameterValue(ParameterIDs::amplitudeRatio));
}
#if SPECTRUMTAG_ARA
void SpectrumTagAudioProcessorEditor::parentHierarchyChanged()
{
    juce::AudioProcessorEditor::parentHierarchyChanged();
    if(!hostLayoutPass)hostLayoutTicks=30;
}
void SpectrumTagAudioProcessorEditor::visibilityChanged()
{
    juce::AudioProcessorEditor::visibilityChanged();
    if(isShowing()&&!hostLayoutPass)hostLayoutTicks=30;
}
void SpectrumTagAudioProcessorEditor::setScaleFactor(float scale)
{
    const juce::ScopedValueSetter<bool> guard(hostLayoutPass,true);
    juce::AudioProcessorEditor::setScaleFactor(scale);
    resized();repaint();hostLayoutTicks=30;
}
void SpectrumTagAudioProcessorEditor::parentSizeChanged()
{
    juce::AudioProcessorEditor::parentSizeChanged();
    if(!hostLayoutPass){hostResizeDeadline=0;fitToHostBounds();}
}
void SpectrumTagAudioProcessorEditor::requestEditorScale(int percent)
{
    hostLayoutTicks=30;hostResizeDeadline=juce::Time::getMillisecondCounter()+120;
    // JUCE forwards the editor size request through IPlugFrame::resizeView.
    // Keep the host's acknowledgement authoritative if it rejects or clamps it.
    setSize(juce::roundToInt(kDefaultWidth*percent/100.0),juce::roundToInt(kDefaultHeight*percent/100.0));
}
void SpectrumTagAudioProcessorEditor::fitToHostBounds()
{
    if(!scaleReady||hostLayoutPass)return;
    auto* parent=getParentComponent();if(!parent||parent->getWidth()<=0||parent->getHeight()<=0)return;
    const auto actual=getLocalArea(parent,parent->getLocalBounds()).withZeroOrigin();
    if(actual.isEmpty()||getBounds()==actual)return;
    const juce::ScopedValueSetter<bool> guard(hostLayoutPass,true);
    setBounds(actual);repaint();
}
void SpectrumTagAudioProcessorEditor::syncHostLayout()
{
    if(hostLayoutTicks<=0)return;
    const int tick=--hostLayoutTicks;
    if(tick%4!=0||(hostResizeDeadline!=0&&(int)(juce::Time::getMillisecondCounter()-hostResizeDeadline)<0))return;
    fitToHostBounds();
}
#endif
void SpectrumTagAudioProcessorEditor::timerCallback()
{
#if SPECTRUMTAG_ARA
    syncHostLayout();
    if(auto* view=processor.getEditorView<spectrumtag::ara::View>()){
        view->beforeSelection=[this]{finishChange(true);};
        if(!view->loaded&&!view->manualLoadOnly)processor.loadARA();
    }
    processor.pollARA();
#endif
    processor.pollTransportTimeout();
    if(processor.getStateRevision()!=lastStateRevision){pendingChange=pendingRevert=false;restoreEditorStateFromProcessor();stableState=processor.captureUserState();}
    syncImagePreview();
    spectrumView->getImageBox().refreshConversion();
    spectrumView->tick();
    if(juce::Time::getMillisecondCounter()-lastDiagnosticTick>=500){lastDiagnosticTick=juce::Time::getMillisecondCounter();diagnosticHistory.add(diagnosticState());}
    const auto timingPrefix=spectrumView->isFitView()
        ?juce::String::fromUTF8(chineseTips?"全览 ":"Fit ")+juce::String(spectrumView->fitSpeed(),3)+juce::String::fromUTF8("× · ")
        :juce::String::fromUTF8(chineseTips?"视窗 ":"Window ");
    timeSpanLabel.setText(timingPrefix+juce::String(spectrumView->visibleSeconds(),2)+juce::String::fromUTF8(chineseTips?" 秒":" s"),juce::dontSendNotification);
    if(!processor.filePlayerMode&&scaleNeedsSave&&juce::Time::getMillisecondCounter()-scaleChangedAt>500){spectrumtag::Preferences::remember(nullptr,nullptr,nullptr,&uiScale);scaleNeedsSave=false;}
    finishChange();
    if(recordBefore&&!processor.recording().isArmed()&&!processor.recording().isBusy()){
        persistEditorStateToProcessor();auto after=processor.captureUserState();auto before=after;
        before.document=recordBefore->document;before.armed=recordBefore->armed;
        processor.commitUserChange(std::move(before),after,true);recordBefore.reset();stableState=after;
    }
    processor.finishPendingClear();
    if(auto* trig=processor.getPrintTriggerParam()){
        if(!processor.recording().isEnabled()&&!processor.isTransportAdvancing()){
            processor.cancelPrint();if(trig->get())trig->setValueNotifyingHost(0);lastSeenTrigger=false;
        }
        if(processor.consumeAutomationPrintRequest())onPrintClicked();
        const bool on=trig->get();if(on&&!processor.isPrintRunning()&&lastSeenTrigger)trig->setValueNotifyingHost(0);lastSeenTrigger=on;
    }
    printButton.setEnabled((!processor.filePlayerMode||processor.recording().isEnabled())&&!processor.isPrintRunning()&&(!SPECTRUMTAG_ARA||(processor.araBound()&&processor.recording().snapshot()!=nullptr)));
    processor.flushPrintMathLogToFile();
    recordButton.setButtonText(juce::String::fromUTF8(processor.isLoadMode()?(chineseTips?"加载音频":"Load audio"):processor.recording().isArmed()?(chineseTips?"停止录音":"Stop Rec"):(chineseTips?"录音":"Record")));
    recordButton.setColour(juce::TextButton::buttonColourId,processor.recording().isArmed()?juce::Colour(0xff983e56):kButtonBg);
    // 图片对象存在（未打印或聚焦）即可删除；清空按钮同时清除音频与全部图片。
    const bool objectSelected=spectrumView->getImageBox().hasImage()||spectrumView->selectedPrint().has_value();
    removePrintButton.setEnabled(objectSelected);
    // 复制按钮与删除按钮同样只在有对象时亮起；已经复制、等待粘贴时保持可用，
    // 否则复制后一旦取消聚焦就按不到粘贴。
    copyImageButton.setEnabled(objectSelected||(copyButtonPaste&&clipboardImagePath.isNotEmpty()));
    clearRecordButton.setEnabled(processor.recording().isEnabled()&&!processor.isClearPending());recordButton.setEnabled(!processor.isClearPending());
    previousPrintButton.setEnabled(!processor.isClearPending()&&adjacentPrint(false).isNotEmpty());nextPrintButton.setEnabled(!processor.isClearPending()&&adjacentPrint(true).isNotEmpty());
#if SPECTRUMTAG_ARA
    recordButton.setEnabled(processor.araBound());clearRecordButton.setEnabled(processor.araBound()&&processor.getEditorView<spectrumtag::ara::View>()->loaded);
#endif
    auto recording=processor.recording().snapshot();noiseFillSlider.setEnabled(processor.recording().isEnabled()&&recording&&!recording->empty());noiseFillLabel.setAlpha(noiseFillSlider.isEnabled()?1.f:.4f);
    if(!pendingChange&&!gestureActive){persistEditorStateToProcessor();stableState=processor.captureUserState();}
}

juce::var SpectrumTagAudioProcessorEditor::diagnosticState() const {
    using namespace spectrumtag::diagnostics;auto o=object();
    put(o,"elapsedMs",juce::Time::getMillisecondCounterHiRes()-diagnosticStarted);
    put(o,"engine",engine(processor.recording()));put(o,"view",spectrumView->diagnosticState());
    put(o,"transportPlaying",processor.isTransportAdvancing());put(o,"hostPosition",processor.audition().hostPosition());put(o,"auditionPosition",processor.audition().position());
    put(o,"localAudition",processor.audition().isLocal());put(o,"realtimePrintRunning",processor.isPrintRunning());
    return o;
}
juce::var SpectrumTagAudioProcessorEditor::diagnosticReport(){
    using namespace spectrumtag::diagnostics;auto report=object();
    put(report,"schema","SpectrumTag-diagnostics-1");put(report,"capturedUtc",juce::Time::getCurrentTime().toISO8601(true));
    put(report,"privacy","Aggregate technical data only. No media, images, screenshots, project state, file paths, credentials or automatic upload.");
    put(report,"environment",environment());put(report,"wrapper",juce::AudioProcessor::getWrapperTypeDescription(processor.wrapperType));
    put(report,"araBuild",processor.isARABuild());put(report,"araBound",processor.araBound());put(report,"standalone",processor.filePlayerMode);
    put(report,"processorSampleRate",processor.getSampleRate());put(report,"blockSize",processor.getBlockSize());
    put(report,"inputChannels",processor.getTotalNumInputChannels());put(report,"outputChannels",processor.getTotalNumOutputChannels());
    put(report,"editorBounds",rectangle(getBounds()));put(report,"uiScale",uiScale);
    if(auto* peer=getPeer())put(report,"nativeScale",peer->getPlatformScaleFactor());
    put(report,"current",diagnosticState());put(report,"viewProbe",spectrumView->diagnosticState(true));
    put(report,"prints",prints(processor.recording()));put(report,"recentStates",diagnosticHistory.json());
    auto parameters=object();for(auto* p:processor.getParameters())if(auto* v=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
        if(auto* raw=processor.getAPVTS().getRawParameterValue(v->paramID))parameters.getDynamicObject()->setProperty(v->paramID,raw->load());
    put(report,"parameters",parameters);
    return report;
}
void SpectrumTagAudioProcessorEditor::exportDiagnostics(){
    // Capture before opening a native dialog: opening/closing a dialog may itself
    // trigger the repaint we are investigating. Do not tick/render or toggle view.
    const auto report=diagnosticReport();const bool chinese=chineseTips;
    const auto destination=juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getNonexistentChildFile(
        "SpectrumTag-" JucePlugin_VersionString "-Diagnostics-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"),".json");
    diagnosticChooser=std::make_unique<juce::FileChooser>(juce::String::fromUTF8(chinese?"导出诊断（不含音乐、图片或工程）":"Export diagnostics (no media or project files)"),destination,"*.json");
    diagnosticsButton.setEnabled(false);
    diagnosticChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
        [safe=juce::Component::SafePointer<SpectrumTagAudioProcessorEditor>(this),report,chinese](const juce::FileChooser& chooser){
            if(!safe)return;safe->diagnosticsButton.setEnabled(true);const auto file=chooser.getResult();if(file==juce::File{})return;
            const bool saved=spectrumtag::diagnostics::write(file,report);
            juce::AlertWindow::showMessageBoxAsync(saved?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,
                juce::String::fromUTF8(chinese?"SpectrumTag 诊断":"SpectrumTag diagnostics"),
                saved?juce::String::fromUTF8(chinese?"已保存。请将这个 JSON 文件手动发给开发者。\n":"Saved. Please send this JSON file to the developer manually.\n")+file.getFullPathName()
                     :juce::String::fromUTF8(chinese?"无法保存，请选择可写入的位置重试。":"Could not save. Please choose a writable location and try again."));
        });
}

void SpectrumTagAudioProcessorEditor::beginChange(){
    if(suppressChanges)return;finishChange(true);persistEditorStateToProcessor();changeBefore=processor.captureUserState();pendingChange=true;changedAt=juce::Time::getMillisecondCounter();
}
void SpectrumTagAudioProcessorEditor::changed(bool parameters){
    if(suppressChanges||processor.isRestoringUserState())return;
    if(!pendingChange){changeBefore=stableState;pendingChange=true;}
    // 聚焦态下任何影响渲染的修改（参数、速度、频率刻度…）都会让已打印图案失效：
    // 手势结束后撤掉打印，对象退回未打印态。
    if(parameters&&spectrumView->selectedPrint())pendingRevert=true;
    changedAt=juce::Time::getMillisecondCounter();
    persistEditorStateToProcessor();
}
void SpectrumTagAudioProcessorEditor::finishChange(bool force){
    if(suppressChanges||!pendingChange)return;
    if(!force&&(gestureActive||juce::Time::getMillisecondCounter()-changedAt<180||!spectrumView->getImageBox().conversionReady()))return;
    if(pendingRevert){pendingRevert=false;unprintFocused();}
    persistEditorStateToProcessor();auto after=processor.captureUserState();
    processor.commitUserChange(std::move(changeBefore),after);stableState=std::move(after);pendingChange=false;
}
void SpectrumTagAudioProcessorEditor::undo(bool redo){
    finishChange(true);gestureActive=false;recordBefore.reset();
    if(processor.undoUserChange(redo)){restoreEditorStateFromProcessor();stableState=processor.captureUserState();}
}
bool SpectrumTagAudioProcessorEditor::keyPressed(const juce::KeyPress& key){
    const auto mods=key.getModifiers();
    if(mods.isCtrlDown()&&(key.getKeyCode()=='z'||key.getKeyCode()=='Z')){undo(mods.isShiftDown());return true;}
    if(mods.isCtrlDown()&&(key.getKeyCode()=='c'||key.getKeyCode()=='C')){copyImageObject();return true;}
    if(mods.isCtrlDown()&&(key.getKeyCode()=='v'||key.getKeyCode()=='V')){pasteImageObject();return true;}
    if(key.getKeyCode()==juce::KeyPress::deleteKey&&(spectrumView->selectedPrint()||spectrumView->getImageBox().hasImage())){removeSelected();return true;}return false;
}
bool SpectrumTagAudioProcessorEditor::audioReady() const {
    return spectrumView!=nullptr&&spectrumView->canAcceptImage();
}
// 把“未打印”对象渲染进频谱；成功后源图销毁，对象只剩频谱里的挖空（已打印态）。
bool SpectrumTagAudioProcessorEditor::printPlacingObject(){
    if(!spectrumView||spectrumView->imageStage()!=SpectrumView::ImageStage::Placing)return false;
    if(!processor.recording().isEnabled())return false;
    auto source=processor.recording().snapshot();if(!source||source->empty())return false;
    auto e=makeEdit();
    if(!processor.filePlayerMode&&(e.start>=source->end()||e.start+e.duration<=source->start()))return false;
    if(processor.filePlayerMode&&juce::jmax(source->end(),e.start+e.duration)-juce::jmin(source->start(),e.start)>juce::jmin(900.,64000000./source->sampleRate))return false;
    processor.recording().add(e);
    spectrumView->setPreview(e,true);
    spectrumView->getImageBox().clearImage();
    spectrumView->selectEdit({});
    placementId={};
    spectrumView->refreshCanvas();
    return true;
}
// 聚焦态被修改：撤掉这次打印（频谱恢复原样），把对象自己的图片按打印范围
// 摆回来，变成“未打印”的状态1，等待重新打印。
void SpectrumTagAudioProcessorEditor::unprintFocused(){
    const auto id=spectrumView->selectedEdit();
    const auto edit=spectrumView->selectedPrint();
    if(id.isEmpty()||!edit)return;
    auto& box=spectrumView->getImageBox();
    if(!box.hasImage()&&!edit->imagePath.isEmpty()&&juce::File(edit->imagePath).existsAsFile())box.loadImageFromFile(juce::File(edit->imagePath));
    spectrumView->alignImageToPrint(*edit);
    // 绘制层：这次打印正被退回的图片接替，不要在图片上下的频谱里挖出黑柱。
    spectrumView->markPrintRevealed(id);
    processor.recording().remove(id);
    spectrumView->setPreview({});
    spectrumView->selectEdit({});
    spectrumView->refreshDeletedPrint();
    placementId={};
    spectrumView->refreshCanvas();
    changed();
}
void SpectrumTagAudioProcessorEditor::removeSelected(){
    finishChange(true);
    const auto focusedId=spectrumView->selectedEdit();
    const bool printed=focusedId.isNotEmpty()&&spectrumView->selectedPrint().has_value();
    if(!printed&&!spectrumView->getImageBox().hasImage())return;
    beginChange();
    if(printed){
        processor.recording().remove(focusedId);
        spectrumView->setPreview({});
        spectrumView->selectEdit({});
        spectrumView->refreshDeletedPrint();
    }
    // 删除的是“图片对象”本身：原图和它的打印一起消失，不会以任何方式回来。
    spectrumView->getImageBox().clearImage();
    placementId={};
    spectrumView->refreshCanvas();
    changed();finishChange(true);
}
juce::String SpectrumTagAudioProcessorEditor::adjacentPrint(bool next)const{
    auto edits=processor.recording().edits();if(!edits||edits->empty())return {};
    std::vector<const spectrumtag::PrintEdit*> ordered;for(const auto& e:*edits)ordered.push_back(&e);
    std::stable_sort(ordered.begin(),ordered.end(),[](auto* a,auto* b){return a->start<b->start;});
    const auto id=spectrumView->selectedEdit();auto it=std::find_if(ordered.begin(),ordered.end(),[&](auto* e){return e->id==id;});
    if(it==ordered.end())return (next?ordered.front():ordered.back())->id;
    if(next){if(++it!=ordered.end())return (*it)->id;}else if(it!=ordered.begin())return (*std::prev(it))->id;
    return {};
}
void SpectrumTagAudioProcessorEditor::navigatePrint(bool next){
    finishChange(true);const auto id=adjacentPrint(next);if(id.isNotEmpty())selectPrinted(id);
}
void SpectrumTagAudioProcessorEditor::selectPrinted(const juce::String& id){
    finishChange(true);
    if(id.isEmpty()){
        // 取消聚焦：对象回到“已打印未聚焦”，只保留频谱里的挖空。
        if(!spectrumView->selectedPrint())return;
        beginChange();
        spectrumView->selectEdit({});placementId={};
        spectrumView->getImageBox().clearImage();
        spectrumView->refreshCanvas();
        changed();finishChange(true);
        return;
    }
    std::optional<spectrumtag::PrintEdit> target;
    if(const auto edits=processor.recording().edits())for(const auto& e:*edits)if(e.id==id){target=e;break;}
    if(!target)return;
    beginChange();
    // 只允许一个未打印对象：聚焦别的对象前，先把当前未打印的图片打印进去。
    if(spectrumView->imageStage()==SpectrumView::ImageStage::Placing)printPlacingObject();
    spectrumView->selectEdit(id);placementId=id;
    if(auto edit=spectrumView->selectedPrint()){
        suppressChanges=true;changingScaleControl=true;
        auto assign=[this](const juce::String& name,float value){auto* p=processor.getAPVTS().getParameter(name);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();};
        assign(ParameterIDs::speed,edit->speed);assign(ParameterIDs::imageMode,(float)edit->imageMode);assign(ParameterIDs::imageAmount,edit->amount);
        assign(ParameterIDs::amplitudeRatio,edit->ratio);assign(ParameterIDs::invert,edit->invert?1.f:0.f);assign(ParameterIDs::noiseFill,edit->compensation);
        assign(ParameterIDs::fftSize,edit->fftSize==1024?0.f:edit->fftSize==2048?1.f:2.f);assign(ParameterIDs::fftScale,edit->scaleMode==1?1.f:0.f);assign(ParameterIDs::frequencyPreset,(float)juce::jmax(0,edit->scaleMode-1));
        const float tops[]={20000,24000,22050,48000};int range=0;for(int i=1;i<4;++i)if(std::abs(tops[i]-edit->displayTop)<std::abs(tops[range]-edit->displayTop))range=i;
        assign(ParameterIDs::frequencyRange,(float)range);changingScaleControl=false;syncFrequencyScale();
        // 状态3 只画打印范围和 Delete 提示，不显示源图；等用户拖动或改参数
        // 退回状态1 时，才按 imagePath 把图片重新摆到打印范围上。
        spectrumView->getImageBox().clearImage();
        syncImagePreview();spectrumView->restorePrintView(*edit);processor.rememberARAFrequencyChoice();suppressChanges=false;
    }
    spectrumView->refreshCanvas();
    changed();finishChange(true);
}
spectrumtag::PrintEdit SpectrumTagAudioProcessorEditor::makeEdit(){
    auto& box=spectrumView->getImageBox();auto& v=processor.getAPVTS();auto value=[&](const juce::String& id){return v.getRawParameterValue(id)->load();};
    spectrumtag::PrintEdit e;e.imagePath=box.getImagePath();e.imageAspect=box.aspect();
    e.start=spectrumView->imageStart();e.duration=(double)box.getWidth()/juce::jmax(1,spectrumView->getContentBounds().getWidth())*spectrumView->timeSpan();
    e.speed=value(ParameterIDs::speed);e.viewZoom=spectrumView->zoom();e.scaleMode=processor.getFrequencyScale();
    const float tops[]={20000,24000,22050,48000};e.displayTop=tops[juce::jlimit(0,3,(int)value(ParameterIDs::frequencyRange))];
    auto source=processor.recording().snapshot();const float nyquist=(float)(source?source->sampleRate:juce::jmax(44100.,processor.getSampleRate()))*.5f;
    auto freq=[&](float y){return spectrumtag::frequency::fromY(y,spectrumView->topHz(),e.scaleMode);};
    auto n=box.getNormalisedBounds();e.high=freq(n.getY())/nyquist;e.low=freq(n.getBottom())/nyquist;
    e.fftSize=1024<<juce::jlimit(0,2,(int)value(ParameterIDs::fftSize));e.rows=e.fftSize/2+1;e.cols=juce::jlimit(4,4096,box.getWidth());
    e.ratio=value(ParameterIDs::amplitudeRatio);e.compensation=value(ParameterIDs::noiseFill);e.invert=value(ParameterIDs::invert)>.5f;
    e.imageMode=(int)value(ParameterIDs::imageMode);e.amount=value(ParameterIDs::imageAmount);
    e.mask=std::make_shared<const std::vector<float>>(box.generateMask(e.rows,e.cols,e.scaleMode,e.low*nyquist,e.high*nyquist));return e;
}
void SpectrumTagAudioProcessorEditor::onPrintClicked(){
    processor.pollTransportTimeout();
    // Loading modes must begin with real audio. The standalone keeps a silent
    // "generated" canvas for the empty workspace; printing into it is refused
    // and the on-canvas guide asks the user to load audio first.
    if(processor.isLoadMode()&&!processor.hasLoadedAudio()){
        finishChange(true);
        processor.cancelPrint();
        if(auto* trigger=processor.getPrintTriggerParam())if(trigger->get())trigger->setValueNotifyingHost(0);
        lastSeenTrigger=false;
        spectrumView->flashAudioHint();
        return;
    }
    if(!processor.recording().isEnabled()&&!processor.isTransportAdvancing()){
        processor.cancelPrint();
        if(auto* trigger=processor.getPrintTriggerParam())if(trigger->get())trigger->setValueNotifyingHost(0);
        lastSeenTrigger=false;printButton.setEnabled(true);return;
    }
    // 只有“未打印”的图片对象可以打印；已打印对象要修改必须退回未打印态。
    if(processor.isPrintRunning()||spectrumView->imageStage()!=SpectrumView::ImageStage::Placing)return;
    finishChange(true);beginChange();syncImagePreview();
    if(processor.recording().isEnabled()){
        // 渲染完成后对象只剩频谱里的挖空：源图随打印一起收起。
        printPlacingObject();changed();finishChange(true);return;
    }
    auto e=makeEdit();if(auto* trig=processor.getPrintTriggerParam())if(!trig->get())trig->setValueNotifyingHost(1);
    processor.markPrintClickAndBeginMathLog();processor.startPrint(std::vector<float>(*e.mask),e.rows,e.cols,e.low,e.high,e.duration);
    spectrumView->getImageBox().clearImage();spectrumView->selectEdit({});placementId={};printButton.setEnabled(false);finishChange(true);
}

void SpectrumTagAudioProcessorEditor::persistEditorStateToProcessor(){
    if(!spectrumView)return;auto& box=spectrumView->getImageBox();auto s=processor.getEditorState();
    // imagePath 只保存“未打印/聚焦”对象：已打印未聚焦时源图已销毁，状态里不该再有它。
    s.palette=selectedPalette;s.imageVisible=box.hasImage();s.chinese=chineseTips;s.uiScale=uiScale;s.placementId=placementId;
    s.imagePath=box.getImagePath();auto n=box.getNormalisedBounds();s.imgRectXNorm=n.getX();s.imgRectYNorm=n.getY();s.imgRectWNorm=n.getWidth();s.imgRectHNorm=n.getHeight();s.imgBoxWidthPx=(float)box.getWidth();s.spectrumHeightPx=spectrumView->getContentBounds().getHeight();
    s.hasValidValues=true;spectrumView->saveView(s);processor.setEditorState(s);
}
void SpectrumTagAudioProcessorEditor::restoreEditorStateFromProcessor(){
    if(!spectrumView)return;spectrumView->setPreview({});auto s=processor.getEditorState();bool previous=suppressChanges;suppressChanges=true;lastStateRevision=processor.getStateRevision();
    selectPalette(s.palette,false);chineseTips=s.chinese;placementId=s.placementId;
#if !SPECTRUMTAG_ARA
        if(!processor.filePlayerMode)setSize(juce::roundToInt(kDefaultWidth*s.uiScale),juce::roundToInt(kDefaultHeight*s.uiScale));
#endif
    // 状态里没有 imagePath 就代表没有“未打印/聚焦”对象：已打印对象只存在于
    // 录音的 Print 列表里，所以这里必须清空源图，否则删除过的图片会复活。
    // 旧存档把“隐藏的源图”记成 imageVisible=0：新模型里对象只有未打印（可见）
    // 与聚焦（可见）两种带图状态，这种隐藏图没有对象归属，直接丢弃。
    auto& box=spectrumView->getImageBox();
    const bool legacyHidden=!s.imageVisible&&s.selectedPrint.isEmpty();
    const auto restoredPath=legacyHidden?juce::String():s.imagePath;
    if(box.getImagePath()!=restoredPath){box.clearImage();if(juce::File(restoredPath).existsAsFile())box.loadImageFromFile(juce::File(restoredPath));else box.setImagePathForRecall(restoredPath);}
    if(s.hasValidValues&&box.hasImage())box.setNormalisedBounds({s.imgRectXNorm,s.imgRectYNorm,s.imgRectWNorm,s.imgRectHNorm});
    spectrumView->setScaleMode(processor.getFrequencyScale());spectrumView->restoreView(s);
    syncImagePreview();refreshLanguage();suppressChanges=previous;pendingChange=pendingRevert=false;
}
void SpectrumTagAudioProcessorEditor::selectPalette(int index,bool remember){
    selectedPalette=juce::jlimit(0,7,index);spectrumView->setPalette(selectedPalette);
    for(int i=0;i<8;++i)if(paletteButtons[i])paletteButtons[i]->setToggleState(i==selectedPalette,juce::dontSendNotification);
    if(remember)spectrumtag::Preferences::remember(nullptr,&selectedPalette);persistEditorStateToProcessor();
}
bool SpectrumTagAudioProcessorEditor::loadImageObject(const juce::File& file){
    // 没有音频就没有可打印的画布：拒绝导入，只提示先加载音频。
    if(!audioReady()){spectrumView->flashAudioHint();return false;}
    // 同一时间只允许一个未打印对象：已有未打印图片时，先把上一张打印进频谱。
    if(spectrumView->imageStage()==SpectrumView::ImageStage::Placing)printPlacingObject();
    auto& box=spectrumView->getImageBox();
    if(!box.loadImageFromFile(file))return false;
    // 缓存一份副本，删除原文件后对象仍能回填。
    const auto folder=spectrumtag::Preferences::file().getParentDirectory().getChildFile("Images");
    const auto cached=folder.getChildFile(juce::Uuid().toString()+file.getFileExtension());
    if(folder.createDirectory().wasOk()&&file.copyFileTo(cached))box.setImagePathForRecall(cached.getFullPathName());
    spectrumView->selectEdit({});placementId={};
    syncImagePreview();spectrumView->refreshCanvas();
    return true;
}
void SpectrumTagAudioProcessorEditor::imagePicked(const juce::File& file){
    finishChange(true);beginChange();
    if(!loadImageObject(file)){finishChange(true);return;}
    changed();finishChange(true);
}
// Ctrl+拖动图片：原对象留在原位变成 Print，拖走的是同样的副本。
// 撤销步由这次手势松手时的 onGestureEnd 一起提交。
void SpectrumTagAudioProcessorEditor::duplicateImageObject(){
    auto& box=spectrumView->getImageBox();
    if(!box.hasImage()||spectrumView->imageStage()!=SpectrumView::ImageStage::Placing)return;
    const juce::File file(box.getImagePath());
    if(!file.existsAsFile())return;
    // 打印不成立（没有可打印的音频）时保持原样，退化成普通拖动。
    if(!printPlacingObject())return;
    box.loadImageFromFile(file);
    spectrumView->selectEdit({});placementId={};
    syncImagePreview();spectrumView->refreshCanvas();changed();
}
// Ctrl+C：记录当前对象的图片。聚焦的 Print 用它记录的原图，未打印对象用图片框里的图。
bool SpectrumTagAudioProcessorEditor::copyImageObject(){
    juce::String path;
    if(const auto edit=spectrumView->selectedPrint())path=edit->imagePath;
    else if(spectrumView->getImageBox().hasImage())path=spectrumView->getImageBox().getImagePath();
    if(path.isEmpty()||!juce::File(path).existsAsFile())return false;
    clipboardImagePath=path;
    copyButtonPaste=true;refreshCopyButton();
    return true;
}
// Ctrl+V：以当前播放位置为左边缘新建一份副本。正在聚焦的对象先取消聚焦，
// 它本身已经打印在频谱里（等于自动打印），图片框腾出来放新对象；
// 若还有未打印的图片对象，loadImageObject 会先把它打印掉。
bool SpectrumTagAudioProcessorEditor::pasteImageObject(){
    if(clipboardImagePath.isEmpty())return false;
    const juce::File file(clipboardImagePath);
    if(!file.existsAsFile())return false;
    const double at=processor.audition().position();
    finishChange(true);beginChange();
    if(spectrumView->selectedPrint()){
        // 与“点空白处取消聚焦”一致：打印留在频谱里，图片框腾空准备接新对象。
        spectrumView->selectEdit({});placementId={};
        spectrumView->getImageBox().clearImage();
        spectrumView->refreshCanvas();
    }
    if(!loadImageObject(file)){finishChange(true);refreshCopyButton();return false;}
    spectrumView->placeImageStartAt(at);
    copyButtonPaste=false;
    changed();finishChange(true);
    refreshCopyButton();
    return true;
}
// 复制按钮：平时是「复制」（等同 Ctrl+C），记录好之后变成「粘贴」（等同 Ctrl+V）。
void SpectrumTagAudioProcessorEditor::refreshCopyButton(){
    const bool paste=copyButtonPaste&&clipboardImagePath.isNotEmpty();
    copyImageButton.setButtonText(juce::String::fromUTF8(paste?(chineseTips?"粘贴":"Paste"):(chineseTips?"复制":"Copy")));
    copyImageButton.setTooltip(juce::String::fromUTF8(paste
        ?(chineseTips?"粘贴：结束当前聚焦，并在播放位置新建一份副本。快捷键 Ctrl+V。"
                     :"Paste: end the current focus and create a copy at the playhead. Shortcut: Ctrl+V.")
        :(chineseTips?"复制当前图片对象，随后可在播放位置粘贴副本。快捷键 Ctrl+C。"
                     :"Copy the current image object, then paste a copy at the playhead. Shortcut: Ctrl+C.")));
}
void SpectrumTagAudioProcessorEditor::refreshLanguage(){
    auto tr=[this](const char* zh,const char* en){return juce::String::fromUTF8(chineseTips?zh:en);};lookAndFeel.setChinese(chineseTips);
    diagnosticsButton.setButtonText(tr("导出诊断","Diagnostics"));
    diagnosticsButton.setTooltip(tr("复现问题后保存诊断 JSON：仅包含系统、参数和显示状态，不包含音乐、图片、工程或截图，不会自动上传。","After reproducing a problem, save diagnostic JSON containing system, parameter and display status only. No audio, images, projects or screenshots; never uploaded automatically."));
    for(auto* l:{&fftSizeLabel,&fftScaleLabel,&speedLabel,&amplitudeLabel,&invertLabel,&imageModeLabel,&imageAmountLabel,&noiseFillLabel,&frequencyRangeLabel})l->setFont(chineseTips?spectrumtag::ui::chineseFont(18.f):juce::Font(basementTypeface).withHeight(18));
    fftSizeLabel.setText(tr("FFT 大小：","FFT Size:"),juce::dontSendNotification);fftScaleLabel.setText(tr("频率刻度：","FFT scale:"),juce::dontSendNotification);
    speedLabel.setText(tr("速度：","Speed:"),juce::dontSendNotification);amplitudeLabel.setText(tr("保留音量：","Amplitude Ratio:"),juce::dontSendNotification);
    imageModeLabel.setText(tr("图像黑白转换：","Image B/W:"),juce::dontSendNotification);imageAmountLabel.setText(tr("黑白强度：","B/W amount:"),juce::dontSendNotification);
    invertLabel.setText(tr("反转：","Invert:"),juce::dontSendNotification);noiseFillLabel.setText(tr("噪声补偿：","Noise Fill:"),juce::dontSendNotification);frequencyRangeLabel.setText(tr("显示上限：","Range:"),juce::dontSendNotification);
    imageModeCombo.changeItemText(1,tr("自动阈值","Auto (Otsu)"));imageModeCombo.changeItemText(2,tr("保留细节","Detail (Adaptive)"));imageModeCombo.changeItemText(3,tr("手动阈值","Manual"));imageModeCombo.changeItemText(4,tr("透明度轮廓","Alpha silhouette"));
    fftScaleCombo.changeItemText(1,tr("线性","Linear"));fftScaleCombo.changeItemText(2,tr("对数","Log"));fftScaleCombo.changeItemText(7,tr("扩展对数","Extended Log"));
    imageModeCombo.setSelectedId(1+(int)*processor.getAPVTS().getRawParameterValue(ParameterIDs::imageMode),juce::dontSendNotification);
    fftScaleCombo.setSelectedId(1+processor.getFrequencyScale(),juce::dontSendNotification);
    imageModeCombo.sendLookAndFeelChange();fftScaleCombo.sendLookAndFeelChange();
    removePrintButton.setButtonText(tr("删除","Delete"));previousPrintButton.setButtonText(tr("上一个打印","Previous Print"));nextPrintButton.setButtonText(tr("下一个打印","Next Print"));
    refreshCopyButton();
    recordButton.setButtonText(processor.isLoadMode()?tr("加载音频","Load audio"):tr("录音","Record"));printButton.setButtonText(tr("打印","Print"));printButton.setTypeface(chineseTips?spectrumtag::ui::chineseTypeface():basementTypeface);printButton.setTypeScale(chineseTips?spectrumtag::ui::chineseScale:1.0f);
    spectrumView->setChinese(chineseTips);tooltipLanguageButton.setButtonText(juce::String::fromUTF8(chineseTips?"中文":"EN"));updateTooltips();resized();repaint();
}
void SpectrumTagAudioProcessorEditor::updateTooltips(){
    auto tr=[this](const char* zh,const char* en){return juce::String::fromUTF8(chineseTips?zh:en);};
    tooltipLanguageButton.setTooltip(tr("切换界面和悬停说明的语言。","Switch the interface and tooltip language."));
#if SPECTRUMTAG_ARA
    uiScaleCombo.setTooltip(tr("请求宿主整体缩放 75–250%。ARA 面板边框由 DAW 管理，内部自动匹配宿主实际尺寸。", "Request a host resize from 75–250%. The DAW owns the ARA panel frame; the content follows the size it grants."));
#else
    uiScaleCombo.setTooltip(tr("整体界面缩放 75–250%；右下角可等比例拖动。","Scale the whole UI from 75–250%; drag the lower-right corner for a custom size."));
#endif
    frequencyRangeCombo.setTooltip(tr("频率显示上限，默认 22.05 kHz。选中 Print 时重新渲染；未选中时只改变视图。最高不超过采样率的一半。","Display ceiling, default 22.05 kHz, capped at half the sample rate. Rerenders the selected Print; otherwise only changes the view."));
    recordButton.setTooltip(tr("按 DAW 时间位置录制原始音频。再次点击停止。未录到的位置播放原轨输入。","Capture original audio at DAW timeline positions. Click again to stop. Unrecorded positions play the original input."));
    clearRecordButton.setTooltip(tr("清除插件录音、打印和补偿噪声，回到未录制模式。可撤销。","Clear the plugin recording, Prints and added noise; return to live mode. Undo restores them."));
    removePrintButton.setTooltip(tr("删除选中打印及其补偿噪声，保留其他打印。快捷键 Delete。","Delete the selected Print and its noise, retaining other Prints. Shortcut: Delete."));
    previousPrintButton.setTooltip(tr("选择时间位置上的上一个打印，不移动播放指针。撤销：Ctrl+Z。","Select the previous Print in time without moving the playhead. Undo: Ctrl+Z."));nextPrintButton.setTooltip(tr("选择时间位置上的下一个打印，不移动播放指针。重做：Ctrl+Shift+Z。","Select the next Print in time without moving the playhead. Redo: Ctrl+Shift+Z."));
    fftSizeCombo.setTooltip(tr("较大 FFT 提高频率分辨率，但会拉宽时间细节。","Larger FFT sizes improve frequency resolution but spread time detail."));
    fftScaleCombo.setTooltip(tr("线性、Mel、Bark、对数、扩展对数，以及 insight。insight 保持当前显示上限，按参考截图匹配频率和时间（约 18 秒视窗），Speed 使用统一单位，自动填入匹配值后可手动调整。通用刻度只改频率、不改当前时间速度；扩展对数下限为 10 Hz。选中 Print 时重新渲染；未选中时只改变视图。","Linear, Mel, Bark, Log, Extended Log or insight. insight keeps the current Range and fills in a matched Speed value using the same time unit as every other mode; adjust Speed afterwards. Reference insight window: about 18 s from the screenshot. Generic scales retain the current time speed; Extended Log starts at 10 Hz. Rerenders a selected Print; otherwise only changes the view."));
    speedSlider.setTooltip(tr("统一时间缩放，所有频率刻度下相同数值代表相同速度。2x 覆盖的时长为 1x 的一半；右侧显示当前视窗覆盖的秒数。选中打印后会调整其时长；拖动结束后合并渲染最终结果。图片始终保持原比例。","Unified time scale: the same value means the same speed in every frequency mode. 2x covers half as much time as 1x. Window shows the actual visible duration in seconds. A selected Print changes duration; final rendering follows after dragging ends. Image preview keeps its original aspect ratio."));
    amplitudeSlider.setTooltip(tr("打印区域保留音量 0–33%。数值越大，图案越淡。拖动结束后更新渲染。","Remaining gain, 0–33%. Higher values soften the image. Final rendering follows after dragging ends."));
    imageModeCombo.setTooltip(tr("自动、局部细节、手动阈值，或透明度轮廓。透明度轮廓适合透明背景图标。","Automatic, adaptive detail, manual threshold, or alpha silhouette for transparent artwork."));
    imageAmountSlider.setTooltip(tr("黑白强度，默认 0.5。Alt+左键或双击恢复默认。","B/W amount; default 0.5. Alt-left-click or double-click resets."));
    invertToggle.setTooltip(tr("同步反转预览和打印。","Invert both preview and Print."));
    noiseFillSlider.setTooltip(tr("仅录音模式可用。只在图片范围内能量不足的亮区补充白噪声。删除打印时一起删除。","Recorded mode only. Add white noise to weak bright areas inside the image. Deleting the Print removes its noise."));
    const auto fineTip=tr(" 按住 Shift 拖动或滚轮可微调（1/10 速度）。"," Hold Shift while dragging or scrolling for fine adjustment (1/10 speed).");
    for(auto* slider:{&speedSlider,&amplitudeSlider,&imageAmountSlider,&noiseFillSlider})slider->setTooltip(slider->getTooltip()+fineTip);
    printButton.setTooltip(tr("未录音时仅在播放期间实时试听；停止时点击不执行，也不排队。录音后隐藏原图，在图片位置显示模拟图形和渲染进度，完成后显示真实频谱。同一位置重复打印不会叠加。","Before recording, Print auditions only while playing; stopped clicks do nothing and are not queued. Recorded mode hides the source image and shows a simulation with render progress at its position, then the actual spectrum. Repeating Print replaces without stacking."));
    auto navigation=tr("滚轮左右浏览；Shift 或 Ctrl+滚轮只缩放时间视图。单击已打印的图形会聚焦它并显示原图与边框；点击非打印区域定位并取消聚焦。图片下方按钮把图片打印进频谱；聚焦后任何拖动或参数修改都会撤掉这次打印、回到未打印状态，需要重新打印。Ctrl+拖动图片＝原位留一份打印、拖走同样的副本；Ctrl+C 记录当前对象，Ctrl+V 随时可在播放位置新建副本，正在聚焦的对象会保留为打印并取消聚焦。Ctrl+Z 撤销。下方波形可按住拖动连续定位；拖动白框左右两端可缩放时间视图（等同 Speed / Ctrl+滚轮）。","Wheel pans time; Shift or Ctrl-wheel scales the time view only. Click a Print to focus it and reveal the source image with its frame; clicking elsewhere seeks and unfocuses. The button under the image prints it into the spectrum; once focused, any drag or parameter change removes that Print and returns to the unprinted state, so print again afterwards. Ctrl-drag the image leaves a Print in place and drags a copy; Ctrl+C copies the current object, Ctrl+V creates a copy at the playhead at any time, keeping a focused Print printed and unfocusing it. Ctrl+Z undoes. Drag the overview waveform to scrub; drag either edge of its white view box to zoom the time view (same as Speed / Ctrl-wheel).");
    spectrumView->setTooltip(navigation);spectrumView->getImageBox().setTooltip(tr("拖动移动，右下角等比缩放，Ctrl+拖动留下一份副本，单击或双击换图。","Drag to move; lower-right corner resizes proportionally; Ctrl-drag leaves a copy behind; click or double-click to load an image.")+"\n"+navigation);
    spectrumView->setPrintButtonTooltip(tr("把这张图片打印进频谱。打印后图片隐藏，点击频谱里的位置可以重新聚焦并查看边框。","Print this picture into the spectrum. The image is hidden afterwards; click its position in the spectrum to focus it again."));
    if(processor.filePlayerMode){
        recordButton.setTooltip(tr("选择本地音频或视频音轨；也可直接拖入文件。", "Choose local audio or a video soundtrack, or drag a file into the window."));
        clearRecordButton.setTooltip(tr("清空加载的音频及打印；Ctrl+Z 可撤销。", "Clear loaded audio and Prints; Ctrl+Z restores them."));
        printButton.setTooltip(tr("将图片渲染到音频内或前后空白处；空白处由噪声补偿生成声音，导出时包含延长的部分，视频相应补黑帧。删除打印会撤回对应延长。最多 15 分钟 / 6400 万采样，使用导出按原格式保存。", "Print inside audio or empty time before/after it. Noise Fill supplies sound in silence; Export includes the extension and pads video with black frames. Deleting the Print removes its extension. Limit: 15 minutes / 64M samples. Export keeps the source format."));
    }
    if(processor.isLoadMode())noiseFillSlider.setTooltip(tr("仅加载音频后可用；只补偿图片范围内的弱频段，删除打印时一起移除。", "Available after loading audio. Fill only weak frequencies inside the image; removed with that Print."));
    if(processor.filePlayerMode)noiseFillSlider.setTooltip(tr("在空白或已加载的音频中，只补偿图片范围内的弱频段。空白处需要大于 0 才能生成声音；删除打印时一起移除。", "Fill weak frequencies only inside the image, with or without a loaded file. A value above zero generates sound in blank areas. Removed with that Print."));
    const char* names[]={"经典橙","深海青","电光蓝","薰衣草紫","玫瑰粉","薄荷绿","奶油金","冰川白"};for(int i=0;i<8;++i)paletteButtons[i]->setTooltip(chineseTips?juce::String::fromUTF8(names[i]):juce::String(spectrumtag::palettes[i].name));
#if SPECTRUMTAG_ARA
    clearRecordButton.setTooltip(tr("清空当前加载的音频视图及其全部打印和补偿噪声。DAW 素材保留；点击加载重新开始。可撤销。", "Clear the loaded view and all its Prints/Noise Fill. DAW events remain. Click Load to start again. Undo restores the edits."));
    recordButton.setTooltip(tr("加载所选事件所在轨道的全部 ARA 音频，保留工程位置与空隙。同轨切换复用缓存；重新加载保留打印。","Load all ARA events on the selected track at their DAW positions, preserving gaps. Switching events on that track reuses the cache; reload preserves Prints."));
    printButton.setTooltip(tr("将图片打印到已加载的音频，显示模拟预览与进度，完成后显示真实频谱。Delete 删除，Ctrl+Z 撤销。","Print into loaded audio. A simulated preview and progress are replaced by the rendered spectrum. Delete removes; Ctrl+Z undoes."));
#endif
}
