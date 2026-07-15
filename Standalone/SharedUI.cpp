#include "SharedUI.h"
#include <JuceHeader.h>
#include <cmath>

using namespace SharedColours;

// ============================================================================
//  StandaloneLookAndFeel
// ============================================================================
StandaloneLookAndFeel::StandaloneLookAndFeel()
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

juce::Typeface::Ptr StandaloneLookAndFeel::getTypefaceForFont (const juce::Font&)
{
    return typeface;
}

void StandaloneLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
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

void StandaloneLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
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

juce::Font StandaloneLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (typeface).withHeight (juce::jmin (15.0f, box.getHeight() * 0.7f));
}

void StandaloneLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId));
}

void StandaloneLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
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
    fitNormRectToImageAspect();
    resizedFromNorm();
    repaint();
    if (onChanged) onChanged();
    return true;
}

void ImageBoxComponent::fitNormRectToImageAspect()
{
    if (! sourceImage.isValid() || sourceImage.getHeight() <= 0) return;
    if (contentBounds.isEmpty()) return;

    const float aspect    = (float) sourceImage.getWidth() / (float) sourceImage.getHeight();
    const float pixAspect = (float) contentBounds.getWidth()
                          / juce::jmax (1, contentBounds.getHeight());

    const float cx = normRect.getCentreX();
    const float cy = normRect.getCentreY();

    float newH = juce::jlimit (0.05f, 0.95f, normRect.getHeight());
    float newW = newH * aspect / pixAspect;

    if (newW < 0.05f || newW > 0.95f)
    {
        newW = juce::jlimit (0.05f, 0.95f, normRect.getWidth());
        newH = newW * pixAspect / aspect;
        newH = juce::jlimit (0.05f, 0.95f, newH);
        newW = newH * aspect / pixAspect;
    }

    juce::Rectangle<float> r (cx - newW * 0.5f, cy - newH * 0.5f, newW, newH);
    normRect = clampNormRect (r);
}

void ImageBoxComponent::preprocessImage()
{
    if (! sourceImage.isValid()) { binaryPreview = juce::Image(); return; }

    const int maxDim = 2048;
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

    const bool useAlphaMode  = (countTransparent > totalPx / 20);
    const float meanBrightness = (float) (sumBrightness / juce::jmax (1, totalPx));
    binaryThreshold = useAlphaMode ? 0.5f : meanBrightness;

    binaryPreview = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData dst (binaryPreview, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto px = src.getPixelColour (x, y);
            const bool inShape = useAlphaMode
                ? (px.getAlpha() >= 128)
                : (px.getBrightness() < meanBrightness);
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
        float aspect = 1.0f;
        if (sourceImage.isValid() && sourceImage.getHeight() > 0)
            aspect = (float) sourceImage.getWidth() / (float) sourceImage.getHeight();

        const float pixAspect = (float) contentBounds.getWidth() / juce::jmax (1, contentBounds.getHeight());

        auto r = dragStartNormRect;
        float newW = juce::jlimit (0.05f, 0.95f, r.getWidth() + dxNorm);
        float newH = newW * pixAspect / aspect;
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

    g.setColour (kImgBoxBg.withAlpha (0.55f));
    g.fillRect (r);

    // 黑黄交错虚线边框
    {
        juce::Path p;
        p.addRectangle (r.reduced (1.0f));
        const float yellowDashes[] = { 10.0f, 0.0f };
        g.setColour (kImgBoxYellow);
        juce::PathStrokeType (2.0f).createDashedStroke (p, p, yellowDashes, 2);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        g.setColour (kImgBoxBlack);
        const float blackDashes[] = { 6.0f, 6.0f };
        juce::Path p2;
        p2.addRectangle (r.reduced (1.0f));
        juce::PathStrokeType (2.0f).createDashedStroke (p2, p2, blackDashes, 2);
        g.strokePath (p2, juce::PathStrokeType (2.0f));
    }

    if (hasImage() && binaryPreview.isValid())
    {
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
        juce::Font f = (typeface != nullptr) ? juce::Font (typeface) : juce::Font();
        f = f.withHeight (juce::jlimit (14.0f, 28.0f, r.getHeight() * 0.18f));
        g.setFont (f);
        g.setColour (kImgBoxBlack);
        g.drawText ("Choose picture", getLocalBounds(), juce::Justification::centred);
    }

    auto h = getResizeHandle().toFloat();
    g.setColour (kImgBoxBlack.withAlpha (0.6f));
    for (int i = 0; i < 3; ++i)
    {
        const float off = 4.0f + i * 4.0f;
        g.drawLine (h.getX() + off, h.getBottom() - 2.0f,
                    h.getRight() - 2.0f, h.getY() + off, 1.2f);
    }
}
