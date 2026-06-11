#include <JuceHeader.h>
#include "PluginEditor.h"
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

    constexpr int kFreqAxisWidth = 70;   // 左侧频率刻度宽度（mel 模式包含钢琴键）
    constexpr int kPianoWidth    = 28;   // mel 模式下钢琴键宽度

    // 显示频率范围固定为 20Hz ~ 22000Hz（同时受 Nyquist 上限约束）
    constexpr float kMinDisplayHz = 20.0f;
    constexpr float kMaxDisplayHz = 22000.0f;

    inline float getDisplayTopHz (float nyquistHz)
    {
        return juce::jmax (kMinDisplayHz + 1.0f, juce::jmin (kMaxDisplayHz, nyquistHz));
    }

    inline float frequencyToYNorm_Linear (float hz, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float t = (juce::jlimit (kMinDisplayHz, topHz, hz) - kMinDisplayHz) / (topHz - kMinDisplayHz);
        return juce::jlimit (0.0f, 1.0f, 1.0f - t);  // 高频在上、低频在下
    }
    inline float yNormToFrequency_Linear (float yNorm, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f - yNorm);
        return kMinDisplayHz + t * (topHz - kMinDisplayHz);
    }

    inline float frequencyToYNorm_Log (float hz, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        if (hz <= kMinDisplayHz) return 1.0f;
        if (hz >= topHz) return 0.0f;
        const float a = std::log (kMinDisplayHz);
        const float b = std::log (topHz);
        const float t = (std::log (hz) - a) / (b - a);
        return juce::jlimit (0.0f, 1.0f, 1.0f - t);  // 高频在上、低频在下
    }
    inline float yNormToFrequency_Log (float yNorm, float nyquistHz)
    {
        const float topHz = getDisplayTopHz (nyquistHz);
        const float a = std::log (kMinDisplayHz);
        const float b = std::log (topHz);
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f - yNorm);
        return std::exp (a + t * (b - a));
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
}

juce::Typeface::Ptr SpectrumTagLookAndFeel::getTypefaceForFont (const juce::Font&)
{
    return typeface;
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
    return juce::Font (typeface).withHeight (juce::jmin (15.0f, box.getHeight() * 0.7f));
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

// ============================================================================
//  RoundPrintButton
// ============================================================================
RoundPrintButton::RoundPrintButton (const juce::String& name) : juce::Button (name) {}

void RoundPrintButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto r = getLocalBounds().toFloat();
    const float diameter = juce::jmin (r.getWidth(), r.getHeight());
    juce::Rectangle<float> circle (r.getCentreX() - diameter * 0.5f,
                                   r.getCentreY() - diameter * 0.5f,
                                   diameter, diameter);

    juce::Colour bg = kPrintBg;
    if (! isEnabled())  bg = juce::Colour (0xff7a7a7a);
    else if (isDown)    bg = juce::Colour (0xffd0d0d0);
    else if (isOver)    bg = juce::Colours::white;

    g.setColour (bg);
    g.fillEllipse (circle);

    juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
    f = f.withHeight (diameter * 0.32f);
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

    const float textWidth = f.getStringWidthFloat (text);
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

juce::Rectangle<float> ImageBoxComponent::clampNormRect (juce::Rectangle<float> r)
{
    r.setWidth  (juce::jlimit (0.05f, 0.95f, r.getWidth()));
    r.setHeight (juce::jlimit (0.05f, 0.95f, r.getHeight()));
    r.setX (juce::jlimit (0.0f, 1.0f - r.getWidth(),  r.getX()));
    r.setY (juce::jlimit (0.0f, 1.0f - r.getHeight(), r.getY()));
    return r;
}

void ImageBoxComponent::setContentBounds (juce::Rectangle<int> r)
{
    contentBounds = r;
    resizedFromNorm();
}

void ImageBoxComponent::resizedFromNorm()
{
    if (contentBounds.isEmpty()) return;
    const int x = contentBounds.getX() + juce::roundToInt (normRect.getX() * contentBounds.getWidth());
    const int y = contentBounds.getY() + juce::roundToInt (normRect.getY() * contentBounds.getHeight());
    const int w = juce::roundToInt (normRect.getWidth()  * contentBounds.getWidth());
    const int h = juce::roundToInt (normRect.getHeight() * contentBounds.getHeight());
    setBounds (x, y, juce::jmax (8, w), juce::jmax (8, h));
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
void ImageBoxComponent::fitNormRectToImageAspect()
{
    if (! sourceImage.isValid() || sourceImage.getHeight() <= 0) return;
    if (contentBounds.isEmpty()) return;

    const float aspect    = (float) sourceImage.getWidth() / (float) sourceImage.getHeight();
    const float pixAspect = (float) contentBounds.getWidth()
                          / juce::jmax (1, contentBounds.getHeight());

    // 在归一化空间下："视觉等比"等价于  newW/newH = aspect / pixAspect
    // 以原 normRect 的对角面积近似不变，且优先保留 height
    const float cx = normRect.getCentreX();
    const float cy = normRect.getCentreY();

    float newH = juce::jlimit (0.05f, 0.95f, normRect.getHeight());
    float newW = newH * aspect / pixAspect;

    // 如果 newW 超出范围，则反过来：先固定 W
    if (newW < 0.05f || newW > 0.95f)
    {
        newW = juce::jlimit (0.05f, 0.95f, normRect.getWidth());
        newH = newW * pixAspect / aspect;
        newH = juce::jlimit (0.05f, 0.95f, newH);
        newW = newH * aspect / pixAspect;  // 再反推一次，确保两者都在范围内
    }

    juce::Rectangle<float> r (cx - newW * 0.5f, cy - newH * 0.5f, newW, newH);
    normRect = clampNormRect (r);
}

void ImageBoxComponent::preprocessImage()
{
    if (! sourceImage.isValid()) { binaryPreview = juce::Image(); return; }

    // 1) 缩放到合理尺寸（限制最大 256x256，避免 mask 过大）
    const int maxDim = 256;
    int w = sourceImage.getWidth();
    int h = sourceImage.getHeight();
    if (juce::jmax (w, h) > maxDim)
    {
        const float scale = (float) maxDim / (float) juce::jmax (w, h);
        w = juce::jmax (1, juce::roundToInt (w * scale));
        h = juce::jmax (1, juce::roundToInt (h * scale));
    }
    juce::Image scaled (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (scaled);
        g.drawImage (sourceImage, 0.0f, 0.0f, (float) w, (float) h,
                     0, 0, sourceImage.getWidth(), sourceImage.getHeight());
    }

    // 2) 判定 logo 形状：先扫一遍像素，决定使用 "Alpha 通道" 还是 "亮度通道"
    //    - 透明 PNG（如 logo.png）：背景 alpha=0；logo 本体 alpha=255
    //      → logo 内部 = 不透明像素 = "形状内"
    //    - 不透明 JPG / 完全不透明 PNG：没有 alpha 信息，退回亮度判定
    //      → 暗像素 = "形状内"（适合黑色 logo on 白底）
    juce::Image::BitmapData src (scaled, juce::Image::BitmapData::readOnly);

    int  countTransparent = 0;
    int  countOpaque      = 0;
    double sumBrightness  = 0.0;
    const int totalPx     = w * h;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto px = src.getPixelColour (x, y);
            if (px.getAlpha() < 128)        ++countTransparent;
            else                             ++countOpaque;
            sumBrightness += px.getBrightness();
        }
    }

    // 至少 5% 像素是透明 → 认为是带透明通道的 logo 图（以 alpha 判定）
    const bool useAlphaMode  = (countTransparent > totalPx / 20);
    const float meanBrightness = (float) (sumBrightness / juce::jmax (1, totalPx));
    binaryThreshold = useAlphaMode ? 0.5f : meanBrightness;

    // 3) 生成二值化预览（黑形：形状内 = 黑色不透明 / 形状外 = 透明）
    binaryPreview = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData dst (binaryPreview, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto px = src.getPixelColour (x, y);
            const bool inShape = useAlphaMode
                ? (px.getAlpha() >= 128)                 // 不透明 = logo 本体
                : (px.getBrightness() < meanBrightness); // 暗像素 = logo（黑形 on 白底）
            dst.setPixelColour (x, y, inShape ? juce::Colour (0xff111111)
                                              : juce::Colour (0x00000000));
        }
    }
}

std::vector<float> ImageBoxComponent::generateMask (int numFreqBins, int numCols) const
{
    std::vector<float> mask ((size_t) (numFreqBins * numCols), 0.0f);
    if (! binaryPreview.isValid() || numCols <= 0 || numFreqBins <= 0) return mask;

    const int w = binaryPreview.getWidth();
    const int h = binaryPreview.getHeight();
    juce::Image::BitmapData bmp (binaryPreview, juce::Image::BitmapData::readOnly);

    // mask 行 = 频率 bin（0 = 低频，bin 越大频率越高）
    // 由于显示时低频在底部 / 高频在顶部，图片 y=0 在顶部 → 高频；y=h-1 在底部 → 低频
    // → row -> imgY: imgY = (numFreqBins - 1 - row) / (numFreqBins-1) * (h-1)
    for (int row = 0; row < numFreqBins; ++row)
    {
        const float ny = numFreqBins > 1 ? (float) (numFreqBins - 1 - row) / (float) (numFreqBins - 1) : 0.0f;
        const int   imgY = juce::jlimit (0, h - 1, juce::roundToInt (ny * (h - 1)));

        for (int col = 0; col < numCols; ++col)
        {
            const float nx = numCols > 1 ? (float) col / (float) (numCols - 1) : 0.0f;
            const int   imgX = juce::jlimit (0, w - 1, juce::roundToInt (nx * (w - 1)));

            const auto px = bmp.getPixelColour (imgX, imgY);
            mask[(size_t) (row * numCols + col)] = (px.getAlpha() > 128) ? 1.0f : 0.0f;
        }
    }
    return mask;
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

void ImageBoxComponent::mouseDown (const juce::MouseEvent& e)
{
    dragMode = hitTest (e.getPosition());
    dragStartPos = e.getPosition() + getPosition();
    dragStartNormRect = normRect;

    if (! hasImage())
    {
        // 没图：单击直接弹出 chooser
        openFileChooser();
        dragMode = DragMode::None;
    }
}

void ImageBoxComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::None || contentBounds.isEmpty()) return;

    const auto cur = e.getPosition() + getPosition();
    const float dxNorm = (float) (cur.getX() - dragStartPos.getX()) / (float) contentBounds.getWidth();
    const float dyNorm = (float) (cur.getY() - dragStartPos.getY()) / (float) contentBounds.getHeight();

    if (dragMode == DragMode::Move)
    {
        auto r = dragStartNormRect;
        r.setPosition (r.getX() + dxNorm, r.getY() + dyNorm);
        normRect = clampNormRect (r);
    }
    else if (dragMode == DragMode::ResizeBR)
    {
        // 锁定长宽比：以原始图片像素比为基准
        float aspect = 1.0f;
        if (sourceImage.isValid() && sourceImage.getHeight() > 0)
            aspect = (float) sourceImage.getWidth() / (float) sourceImage.getHeight();

        // 在归一化空间下，"视觉等比"需要把 contentBounds 的实际宽高比纳入
        const float pixAspect = (float) contentBounds.getWidth() / juce::jmax (1, contentBounds.getHeight());

        auto r = dragStartNormRect;
        // 以 "对角线" 为参考缩放：用 dxNorm 推算宽度，按图片实际比例反推高度
        float newW = juce::jlimit (0.05f, 0.95f, r.getWidth() + dxNorm);
        float newH = newW * pixAspect / aspect;
        // 如果上面让 newH 超出可用范围，则按 dyNorm 反推
        if (newH > 0.95f || newH < 0.05f)
        {
            newH = juce::jlimit (0.05f, 0.95f, r.getHeight() + dyNorm);
            newW = newH * aspect / pixAspect;
        }
        r.setWidth (newW);
        r.setHeight (newH);
        normRect = clampNormRect (r);
    }

    resizedFromNorm();
    if (onChanged) onChanged();
}

void ImageBoxComponent::mouseUp (const juce::MouseEvent& e)
{
    // 区分"轻点"与"拖动"：轻点（dragDist < 4）当作点击中央区域 → 弹出 FileChooser
    if (dragMode == DragMode::Move && hasImage())
    {
        const auto delta = (e.getPosition() + getPosition()) - dragStartPos;
        if (delta.toFloat().getDistanceFromOrigin() < 4.0f)
            openFileChooser();
    }
    dragMode = DragMode::None;
}

void ImageBoxComponent::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose picture",
        juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                if (loadImageFromFile (file))
                {
                    if (onImagePicked) onImagePicked (file);
                }
            }
        });
}

void ImageBoxComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // 半透明灰底
    g.setColour (kImgBoxBg.withAlpha (0.55f));
    g.fillRect (r);

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

    if (hasImage() && binaryPreview.isValid())
    {
        // 居中等比绘制二值化预览
        auto innerR = r.reduced (6.0f);
        const float imgAspect = (float) binaryPreview.getWidth() / juce::jmax (1, binaryPreview.getHeight());
        const float boxAspect = innerR.getWidth() / juce::jmax (1.0f, innerR.getHeight());
        juce::Rectangle<float> drawRect = innerR;
        if (imgAspect > boxAspect)
        {
            const float h2 = innerR.getWidth() / imgAspect;
            drawRect = innerR.withSizeKeepingCentre (innerR.getWidth(), h2);
        }
        else
        {
            const float w2 = innerR.getHeight() * imgAspect;
            drawRect = innerR.withSizeKeepingCentre (w2, innerR.getHeight());
        }
        g.drawImage (binaryPreview, drawRect, juce::RectanglePlacement::centred, false);
    }
    else
    {
        // "Choose picture" 占位
        juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
        f = f.withHeight (juce::jlimit (14.0f, 28.0f, r.getHeight() * 0.18f));
        g.setFont (f);
        g.setColour (kImgBoxBlack);
        g.drawText ("Choose picture", getLocalBounds(), juce::Justification::centred);
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
class SpectrumTagAudioProcessorEditor::SpectrumView : public juce::Component
{
public:
    SpectrumView (SpectrumTagAudioProcessor& p,
                  juce::Typeface::Ptr tf)
        : processor (p), typeface (std::move (tf))
    {
        imageBox = std::make_unique<ImageBoxComponent>();
        imageBox->setTypeface (typeface);
        addAndMakeVisible (*imageBox);
    }

    ImageBoxComponent& getImageBox() { return *imageBox; }
    juce::Rectangle<int> getContentBounds() const { return contentBounds; }

    // 0 = linear, 1 = mel
    void setScaleMode (int m) { if (m != scaleMode) { scaleMode = m; repaint(); } }

    void resized() override
    {
        auto r = getLocalBounds();
        // 左侧刻度区
        axisBounds = r.removeFromLeft (kFreqAxisWidth);
        contentBounds = r;

        // 重置 waterfall image（按内容区大小）
        if (! contentBounds.isEmpty()
            && (waterfall.isNull() || waterfall.getWidth() != contentBounds.getWidth()
                                   || waterfall.getHeight() != contentBounds.getHeight()))
        {
            waterfall = juce::Image (juce::Image::RGB, contentBounds.getWidth(),
                                                       contentBounds.getHeight(), true);
            juce::Graphics gg (waterfall);
            gg.fillAll (kPanelColour);
        }

        if (imageBox != nullptr)
            imageBox->setContentBounds (contentBounds);
    }

    // 由 Editor 定时调用：把最新一帧的幅度谱滚到 waterfall 的最右边
    void pushFrame (const std::vector<float>& mags, float sampleRate, int speedPxPerTick)
    {
        if (waterfall.isNull() || mags.empty() || speedPxPerTick <= 0) return;

        const int W = waterfall.getWidth();
        const int H = waterfall.getHeight();

        speedPxPerTick = juce::jmin (speedPxPerTick, W);

        // ---- 过期检测：display FFT 未产生新数据时保持上一帧，避免黑线 ----
        const int currentCounter = processor.getDisplayUpdateCounter();
        if (currentCounter == lastDisplayCounter)
        {
            repaint();
            return;
        }
        lastDisplayCounter = currentCounter;

        // ---- 幅度映射：固定参考，不做硬门限清屏 ----
        const float maxHz   = sampleRate * 0.5f;
        const int   numBins = (int) mags.size();

        // 1) 整体向左滚动 speedPxPerTick 个像素
        if (speedPxPerTick > 0)
            waterfall.moveImageSection (0, 0, speedPxPerTick, 0, W - speedPxPerTick, H);

        juce::Graphics g (waterfall);

        // 2) 在最右侧 speedPxPerTick 列内绘制最新帧
        // 固定 dB 参考：0 dBFS，显示范围 96 dB
        //  黑 = ≤ -96 dB（接近静音），黄 = ≥ 0 dB（满幅）
        // 注意：不再做“整帧全黑”硬门限，避免白噪声等宽带低峰值信号被误清空。
        const float dbFloor = -96.0f;

        for (int y = 0; y < H; ++y)
        {
            const float yNorm = (float) y / juce::jmax (1, H - 1);   // 0=top,1=bottom
            float hz;
            if (scaleMode == 1) hz = yNormToFrequency_Log (yNorm, maxHz);
            else                hz = yNormToFrequency_Linear (yNorm, maxHz);

            // freq -> bin：hz ∈ [0, maxHz] → binF ∈ [0, numBins-1]
            const float binF = (hz / juce::jmax (1.0f, maxHz)) * (float) (numBins - 1);
            const int b0 = juce::jlimit (0, numBins - 1, (int) std::floor (binF));
            const int b1 = juce::jlimit (0, numBins - 1, b0 + 1);
            const float t = juce::jlimit (0.0f, 1.0f, binF - (float) b0);
            const float mag = mags[(size_t) b0] + (mags[(size_t) b1] - mags[(size_t) b0]) * t;

            const float db = juce::Decibels::gainToDecibels (juce::jmax (1e-9f, mag));
            const float dbNorm = juce::jlimit (0.0f, 1.0f, (db - dbFloor) / (-dbFloor));

            const juce::Colour c = mapHeatmap (dbNorm);
            g.setColour (c);
            g.fillRect (W - speedPxPerTick, y, speedPxPerTick, 1);
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // 频率刻度
        drawFrequencyAxis (g);

        // 主面板
        g.setColour (kPanelColour);
        g.fillRect (contentBounds);

        if (! waterfall.isNull())
            g.drawImageAt (waterfall, contentBounds.getX(), contentBounds.getY());

        // 边框
        g.setColour (juce::Colour (0xff2a2d30));
        g.drawRect (contentBounds, 1);
    }

private:
    static juce::Colour mapHeatmap (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        // 4 段插值：黑 → 深紫 → 橙红 → 黄
        const juce::Colour c0 (0xff000000);
        const juce::Colour c1 (0xff2a0a55);
        const juce::Colour c2 (0xffd13a1a);
        const juce::Colour c3 (0xfff8e83a);
        if (t < 0.33f)      return c0.interpolatedWith (c1, t / 0.33f);
        else if (t < 0.66f) return c1.interpolatedWith (c2, (t - 0.33f) / 0.33f);
        else                return c2.interpolatedWith (c3, (t - 0.66f) / 0.34f);
    }

    void drawFrequencyAxis (juce::Graphics& g)
    {
        g.setColour (kAxisBgColour);
        g.fillRect (axisBounds);

        const float maxHz = (float) (processor.getSampleRate() > 0
                                     ? processor.getSampleRate() : 44100.0) * 0.5f;
        juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
        f = f.withHeight (10.0f);
        g.setFont (f);
        g.setColour (kAxisColour);

        if (scaleMode == 1)
        {
            // mel：钢琴键 + 频率
            drawPianoKeys (g, maxHz);

            // 频率标签（右侧文本区）
            const std::vector<float> hzMarks { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000 };
            const int textX = axisBounds.getRight() - 32;
            for (auto hz : hzMarks)
            {
                if (hz > maxHz) continue;
                const float yNorm = frequencyToYNorm_Log (hz, maxHz);
                const int y = contentBounds.getY() + juce::roundToInt (yNorm * (contentBounds.getHeight() - 1));
                juce::String label = (hz >= 1000.0f) ? (juce::String (hz / 1000.0f, 0) + "k")
                                                     : juce::String ((int) hz);
                g.drawText (label, textX, y - 6, 30, 12, juce::Justification::centredRight);
            }
        }
        else
        {
            // linear：等距频率（20Hz ~ min(22000Hz, Nyquist)）
            const int N = 10;
            for (int i = 0; i <= N; ++i)
            {
                const float yNorm = (float) i / (float) N; // 顶部 0，底部 1
                const int y = contentBounds.getY() + juce::roundToInt (yNorm * (contentBounds.getHeight() - 1));
                const float hz = yNormToFrequency_Linear (yNorm, maxHz);
                juce::String label = (hz >= 1000.0f) ? (juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k")
                                                     : juce::String ((int) std::round (hz));
                g.drawText (label, axisBounds.getX() + 4, y - 6,
                            axisBounds.getWidth() - 8, 12, juce::Justification::centredRight);
            }
        }
    }

    void drawPianoKeys (juce::Graphics& g, float maxHz)
    {
        // 钢琴键区在 axisBounds 左侧
        const int kx = axisBounds.getX();
        const int kw = kPianoWidth;
        const int kytop = contentBounds.getY();
        const int kyh   = contentBounds.getHeight();

        // 8 个八度 C1(32.7Hz) ~ C9(8372Hz)，每键代表一个半音
        // 由于 mel 模式 y 是 log，钢琴键的位置都按 log 排列
        auto noteToHz = [] (int midi) { return 440.0f * std::pow (2.0f, (midi - 69) / 12.0f); };
        const int midiStart = 24;   // C1
        const int midiEnd   = 120;  // C10

        // 白键先画背景，再画黑键
        g.setColour (kPianoWhiteKey);
        g.fillRect (kx, kytop, kw, kyh);

        for (int m = midiStart; m <= midiEnd; ++m)
        {
            const float hz = noteToHz (m);
            if (hz > maxHz) break;
            const float yNorm = frequencyToYNorm_Log (hz, maxHz);
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
                // 白键间分割线
                g.setColour (juce::Colours::darkgrey);
                g.drawLine ((float) kx, (float) y, (float) (kx + kw), (float) y, 0.5f);
            }

            // C 标签
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

        // 边线
        g.setColour (juce::Colour (0xff555555));
        g.drawRect (kx, kytop, kw, kyh, 1);
    }

    SpectrumTagAudioProcessor& processor;
    juce::Typeface::Ptr        typeface;
    std::unique_ptr<ImageBoxComponent> imageBox;
    juce::Rectangle<int>       contentBounds;
    juce::Rectangle<int>       axisBounds;
    juce::Image                waterfall;
    int                        scaleMode = 0;  // 0 linear / 1 mel
    int                        lastDisplayCounter = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};

// ============================================================================
//  Editor
// ============================================================================
SpectrumTagAudioProcessorEditor::SpectrumTagAudioProcessorEditor (SpectrumTagAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    basementTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BasementGrotesqueBlack_v1_202_otf,
        BinaryData::BasementGrotesqueBlack_v1_202_otfSize);

    setLookAndFeel (&lookAndFeel);

    // ---- 顶部 ----
    titleLabel.setText ("SpectrumTag", juce::dontSendNotification);
    styleHeaderLabel (titleLabel, 32.0f, kTextWhite);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (juce::String ("v") + JucePlugin_VersionString, juce::dontSendNotification);
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
    spectrumView->getImageBox().onChanged = [this] { persistEditorStateToProcessor(); };
    spectrumView->getImageBox().onImagePicked = [this] (const juce::File&) { persistEditorStateToProcessor(); };

    // ---- 控件标签 ----
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

    // ---- 控件 ----
    fftSizeCombo.addItemList ({ "1024", "2048", "4096", "8192" }, 1);
    fftScaleCombo.addItemList ({ "linear", "mel" }, 1);
    addAndMakeVisible (fftSizeCombo);
    addAndMakeVisible (fftScaleCombo);

    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (speedSlider);

    amplitudeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amplitudeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (amplitudeSlider);

    invertToggle.setButtonText ({});
    addAndMakeVisible (invertToggle);

    printButton.setTypeface (basementTypeface);
    printButton.onClick = [this] { onPrintClicked(); };
    addAndMakeVisible (printButton);

    // ---- APVTS Attachments ----
    auto& vts = processor.getAPVTS();
    fftSizeAttachment   = std::make_unique<ComboAttachment>  (vts, ParameterIDs::fftSize,        fftSizeCombo);
    fftScaleAttachment  = std::make_unique<ComboAttachment>  (vts, ParameterIDs::fftScale,       fftScaleCombo);
    speedAttachment     = std::make_unique<SliderAttachment> (vts, ParameterIDs::speed,          speedSlider);
    amplitudeAttachment = std::make_unique<SliderAttachment> (vts, ParameterIDs::amplitudeRatio, amplitudeSlider);
    invertAttachment    = std::make_unique<ButtonAttachment> (vts, ParameterIDs::invert,         invertToggle);

    // FFT scale 改变时同步给 SpectrumView
    fftScaleCombo.onChange = [this]
    {
        if (spectrumView != nullptr)
            spectrumView->setScaleMode (fftScaleCombo.getSelectedItemIndex());
    };
    if (spectrumView != nullptr)
        spectrumView->setScaleMode (fftScaleCombo.getSelectedItemIndex());

    // ---- 等比缩放 ----
    setResizable (true, true);
    const double aspect = (double) kDefaultWidth / (double) kDefaultHeight;
    resizeConstrainer.setFixedAspectRatio (aspect);
    resizeConstrainer.setSizeLimits (kMinWidth, kMinHeight, 4096, 4096);
    setConstrainer (&resizeConstrainer);
    setSize (kDefaultWidth, kDefaultHeight);

    // ---- 恢复保存的状态（图片路径 + 框位置）----
    restoreEditorStateFromProcessor();

    startTimerHz (30);
}

SpectrumTagAudioProcessorEditor::~SpectrumTagAudioProcessorEditor()
{
    setConstrainer (nullptr);
    stopTimer();
    setLookAndFeel (nullptr);
}

// ============================================================================
//  文件拖入：用户可以从外部文件夹直接把图片拖到插件窗口
// ============================================================================
bool SpectrumTagAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
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
    auto& imgBox = spectrumView->getImageBox();

    for (auto& f : files)
    {
        juce::File file (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (! file.existsAsFile()) continue;
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg"
            && ext != ".bmp" && ext != ".gif") continue;

        if (imgBox.loadImageFromFile (file))
        {
            persistEditorStateToProcessor();
            break;   // 只取第一张有效图片
        }
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
    const float scale = (float) getHeight() / (float) kDefaultHeight;
    auto px = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto r = getLocalBounds();

    // ===== 顶部 =====
    const int headerH = px (54);
    auto header = r.removeFromTop (headerH).reduced (px (20), px (8));

    titleLabel.setBounds (header.removeFromLeft (px (250)));
    header.removeFromLeft (px (8));
    versionLabel.setBounds (header.removeFromLeft (px (80)));
    header.removeFromLeft (px (4));
    websiteLabel->setBounds (header.removeFromLeft (px (200)));

    titleLabel.setFont   (juce::Font (basementTypeface).withHeight (32.0f * scale));
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

    // Print 圆按钮：右下角
    const int printD = px (160);
    auto printArea = juce::Rectangle<int> (
        getWidth() - px (20) - printD,
        getHeight() - px (20) - printD,
        printD, printD);
    printButton.setBounds (printArea);
}

// ----------------------------------------------------------------------------
void SpectrumTagAudioProcessorEditor::timerCallback()
{
    // 更新 Print 按钮启用状态
    if (! processor.isPrintRunning() && ! printButton.isEnabled())
        printButton.setEnabled (true);

    // 推送一帧到瀑布图
    if (spectrumView != nullptr)
    {
        std::vector<float> mags;
        processor.getLatestMagnitudeSpectrum (mags);

        const float speed = (float) *processor.getAPVTS().getRawParameterValue (ParameterIDs::speed);
        // 30Hz timer * speed * 2 px → 速度 1.0x 时大约 60 px/s
        const int speedPx = juce::jmax (1, juce::roundToInt (speed * 2.0f));
        spectrumView->pushFrame (mags, (float) processor.getSampleRate(), speedPx);
    }
}

// ---- Print 流程（Phase 5）---------------------------------------------------
void SpectrumTagAudioProcessorEditor::onPrintClicked()
{
    if (processor.isPrintRunning() || spectrumView == nullptr) return;
    auto& imgBox = spectrumView->getImageBox();
    if (! imgBox.hasImage()) return;

    // 1) 计算 mask 维度
    //   rows = fft bins = fftSize/2 + 1
    //   cols = 图片框宽度（像素）/ speedPx * 1帧像素？这里用 STFT 帧数表达：
    //         cols = imgBoxWidthPx * (sampleRate / hopSize) / speedPxPerSecond
    //   为简单：让 cols = 图片框宽度 px / speedPxPerTick * (timer 频率)
    //   timer=30Hz, speedPxPerTick = max(1, round(speed*2))
    //   每个 timer tick 滚动 speedPxPerTick px → 持续帧数 = imgBoxW / speedPxPerTick
    //   STFT 帧数 = sampleRate / hopSize 每秒 → 这里我们让 mask 的列数 = STFT 帧数：
    //   持续秒数 = imgBoxWidthPx / (30 * speedPxPerTick)
    //   STFT帧/秒 = sampleRate / (fftSize/2)
    //   cols = 持续秒数 * STFT帧每秒
    const int fftSizeIdx = fftSizeCombo.getSelectedItemIndex();
    const int fftSize    = (fftSizeIdx == 0 ? 1024 : fftSizeIdx == 1 ? 2048
                          : fftSizeIdx == 2 ? 4096 : 8192);
    const int rows = fftSize / 2 + 1;

    const auto contentR = spectrumView->getContentBounds();
    const auto normR = imgBox.getNormalisedBounds();

    // 用归一化宽度 * 内容区宽度推导绘制时长，避免在某些时序下读取到陈旧/极小的组件像素宽度。
    const float boxWidthPx = juce::jmax (1.0f, normR.getWidth() * (float) juce::jmax (1, contentR.getWidth()));

    const double sr = processor.getSampleRate() > 1000.0 ? processor.getSampleRate() : 44100.0;

    const float speed = (float) *processor.getAPVTS().getRawParameterValue (ParameterIDs::speed);
    const int speedPxPerTick = juce::jmax (1, juce::roundToInt (speed * 2.0f));
    const float seconds = boxWidthPx / juce::jmax (1.0f, 30.0f * (float) speedPxPerTick);

    // 关键：Processor 的 STFT hop = fftSize / 4，这里必须与之保持一致。
    // 若误用 fftSize/2，会导致 mask 列数偏少，Print 提前结束（表现为按钮很快恢复可点）。
    const int hopSize = juce::jmax (1, fftSize / 4);
    const float framesPerSec = (float) (sr / (double) hopSize);
    int cols = juce::jmax (4, juce::roundToInt (seconds * framesPerSec));
    cols = juce::jmin (cols, 4096);  // 安全上限

    // 2) 计算图片框对应的归一化频率范围
    // SpectrumView 里 y 顶部 = 高频
    //  linear: y=0 顶部 -> 1.0 (Nyquist)，y=1 底部 -> 0
    //  log:    y=0 顶部 -> Nyquist 的 log，y=1 底部 -> kMinDisplayHz
    const int scaleMode = fftScaleCombo.getSelectedItemIndex();
    const float maxHz = (float) (sr * 0.5);

    auto yNormToFreq = [&] (float yNorm)
    {
        if (scaleMode == 1) return yNormToFrequency_Log (yNorm, maxHz);
        return yNormToFrequency_Linear (yNorm, maxHz);
    };
    const float fHz_top    = yNormToFreq (normR.getY());
    const float fHz_bottom = yNormToFreq (normR.getBottom());
    const float fLow  = juce::jmin (fHz_top, fHz_bottom);
    const float fHigh = juce::jmax (fHz_top, fHz_bottom);
    const float lowNorm  = fLow  / juce::jmax (1.0f, maxHz);
    const float highNorm = fHigh / juce::jmax (1.0f, maxHz);

    // 3) 生成 mask 并启动
    //    传入 durationSeconds：滤波器组方案直接根据时长 + 采样率推进列指针，
    //    不再依赖 STFT 帧节奏，因此即便 cols 是按 STFT 帧数推算的，
    //    "持续秒数"才是真正决定 Print 总长度的量。
    auto mask = imgBox.generateMask (rows, cols);
    printButton.setEnabled (false);
    processor.startPrint (std::move (mask), rows, cols, lowNorm, highNorm, (double) seconds);
}

// ---- 状态持久化 ------------------------------------------------------------
void SpectrumTagAudioProcessorEditor::persistEditorStateToProcessor()
{
    if (spectrumView == nullptr) return;
    auto& imgBox = spectrumView->getImageBox();
    SpectrumTagAudioProcessor::EditorState s;
    s.imagePath = imgBox.getImagePath();
    auto n = imgBox.getNormalisedBounds();
    s.imgRectXNorm = n.getX();
    s.imgRectYNorm = n.getY();
    s.imgRectWNorm = n.getWidth();
    s.imgRectHNorm = n.getHeight();
    s.hasValidValues = true;
    processor.setEditorState (s);
}

void SpectrumTagAudioProcessorEditor::restoreEditorStateFromProcessor()
{
    if (spectrumView == nullptr) return;
    auto s = processor.getEditorState();
    if (! s.hasValidValues) return;

    auto& imgBox = spectrumView->getImageBox();
    juce::Rectangle<float> n (s.imgRectXNorm, s.imgRectYNorm,
                              s.imgRectWNorm, s.imgRectHNorm);
    imgBox.setNormalisedBounds (n);

    if (s.imagePath.isNotEmpty())
    {
        juce::File f (s.imagePath);
        if (f.existsAsFile())
            imgBox.loadImageFromFile (f);
    }
}