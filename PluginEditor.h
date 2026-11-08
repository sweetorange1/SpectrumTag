#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "shared/IisaacTelemetry.h"
#include "shared/PreviewWorker.h"
#include "shared/FineSlider.h"
#include "shared/ViewRenderer.h"
#include "shared/Diagnostics.h"

// ============================================================================
//  SpectrumTagAudioProcessorEditor
// ----------------------------------------------------------------------------
//  布局（参考 UI.png）：
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │ SpectrumTag   v1.1.0  iisaacbeats.cn                                  │
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
    void setChinese(bool value){chinese=value;}

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
    bool chinese=false;
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
class ImageBoxComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    ImageBoxComponent();

    // 设置当前图片（从文件加载并预处理）
    bool loadImageFromFile (const juce::File& file);
    bool hasImage() const { return sourceImage.isValid(); }
    void clearImage(){sourceImage={};binaryMask={};binaryPreview={};imagePath={};requestedConversion=converter.request({},conversionMode,conversionAmount);appliedConversion=0;repaint();}
    juce::String getImagePath() const { return imagePath; }
    void setImagePathForRecall(const juce::String& path){imagePath=path;}

    // 把当前图片渲染成 mask（rows = numFreqBins, cols 根据 contentBounds 宽度）
    // 1.0 = 形状内, 0.0 = 形状外
    std::vector<float> generateMask (int numFreqBins, int numCols, int scale=0, float low=0, float high=1, bool completedPreview=false) const;
    void setConversion(int mode, float amount, bool inverted, float ratio=0.0f);
    juce::Image getPreviewImage() const {return spectrumtag::image::preview(conversionReady()?binaryMask:spectrumtag::image::binarize(sourceImage,{conversionMode,conversionAmount}),previewInverted,previewRatio);}
    float aspect() const {return sourceImage.isValid()?(float)sourceImage.getWidth()/sourceImage.getHeight():1.0f;}
    bool conversionReady() const {return requestedConversion==appliedConversion;}
    bool refreshConversion();
    void setChinese(bool value){chinese=value;repaint();}

    // 当前框在父组件（频谱区）内的归一化矩形（x/y/w/h 都 ∈ [0,1]）
    juce::Rectangle<float> getNormalisedBounds() const { return normRect; }
    void setNormalisedBounds (juce::Rectangle<float> n) { normRect = n; fitNormRectToImageAspect(); resizedFromNorm(); }
    // A recalled Print may extend past the viewport. Clipping is the parent's
    // job; fitting it again would shrink the source independently of the audio.
    void setPrintBounds (juce::Rectangle<float> n) { normRect = n; resizedFromNorm(); }

    // 父组件传入：内容区（不含左侧刻度）的实际像素矩形
    void setContentBounds (juce::Rectangle<int> contentRect);

    // 字体注入
    void setTypeface (juce::Typeface::Ptr tf) { typeface = std::move (tf); repaint(); }

    // 选中/释放回调（让父组件保存到 Processor 状态）
    std::function<void()> onChanged,onGestureStart,onGestureEnd,onFileLoadStart;
    std::function<void(const juce::File&)> onImagePicked;

    void paint (juce::Graphics&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

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

    spectrumtag::PreviewWorker converter;
    uint64_t requestedConversion=0,appliedConversion=0;
    bool chinese=false;
    juce::Image  sourceImage;           // 原图（彩色）
    juce::Image binaryMask;
    int conversionMode=0;
    float conversionAmount=0.5f;
    bool previewInverted=false;
    float previewRatio=0;
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
#if SPECTRUMTAG_ARA
                                        , private juce::AudioProcessorEditorARAExtension
#endif
{
public:
    explicit SpectrumTagAudioProcessorEditor (SpectrumTagAudioProcessor&);
    ~SpectrumTagAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::var diagnosticReport();
#if SPECTRUMTAG_ARA
    juce::AudioProcessorEditorARAExtension* getARAClientExtensions()override{return this;}
    void parentHierarchyChanged() override;
    void parentSizeChanged() override;
    void visibilityChanged() override;
    void setScaleFactor(float) override;
#endif

    // ---- 外部文件拖入支持 ----
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;

private:
    void timerCallback() override;
#if SPECTRUMTAG_ARA
    void syncHostLayout();
    void fitToHostBounds();
    void requestEditorScale(int percent);
    juce::uint32 hostResizeDeadline=0;
    int hostLayoutTicks=0;
    bool hostLayoutPass=false;
#endif
    void onPrintClicked();
    void persistEditorStateToProcessor();
    void restoreEditorStateFromProcessor();

    SpectrumTagAudioProcessor& processor;

    juce::Component scaledContent;
    juce::ComboBox uiScaleCombo;
    float uiScale=1.0f;
    bool scaleReady=false,scaleNeedsSave=false;
    juce::uint32 scaleChangedAt=0;
    // ---- 全局字体与外观 ----
    juce::Typeface::Ptr     basementTypeface;
    SpectrumTagLookAndFeel  lookAndFeel;

    // ---- 顶部 ----
    juce::Label                       titleLabel;       // "SpectrumTag"
    juce::Label                       versionLabel;     // "v1.1.0"
    std::unique_ptr<HyperlinkLabel>   websiteLabel;     // iisaacbeats.cn

    // ---- 频谱视图（占位，Phase 3 实现）----
    class SpectrumView;
    std::unique_ptr<SpectrumView> spectrumView;

    // ---- 右侧控制面板 ----
    juce::Label    fftSizeLabel    { {}, "FFT Size:" };
    juce::Label    fftScaleLabel   { {}, "FFT scale:" };
    juce::Label    speedLabel      { {}, "Speed:" };
    juce::Label timeSpanLabel;
    juce::Label    amplitudeLabel  { {}, "Amplitude Ratio:" };
    juce::Label    invertLabel     { {}, "Invert:" };

    juce::Label imageModeLabel {{}, "Image B/W:"};
    juce::Label imageAmountLabel {{}, "B/W amount:"};
    juce::ComboBox imageModeCombo;
    spectrumtag::FineSlider imageAmountSlider;
    spectrumtag::FineSlider noiseFillSlider;
    juce::Label noiseFillLabel {{},"Noise Fill:"};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseFillAttachment;
    class PrintNavigationButton final : public juce::Button {
    public:
        explicit PrintNavigationButton(bool next):juce::Button(next?"Next Print":"Previous Print"),pointsRight(next){}
        void paintButton(juce::Graphics& g,bool over,bool down) override {
            auto background=juce::Colour(0xff273136);
            if(isEnabled()&&over)background=background.brighter(down?.08f:.18f);
            g.setColour(background.withAlpha(isEnabled()?1.f:.55f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(),4.f);
            auto bounds=getLocalBounds().toFloat().withSizeKeepingCentre(22.f,22.f);
            juce::Path arrow;
            arrow.startNewSubPath(6.f,5.f);arrow.lineTo(6.f,19.f);
            arrow.startNewSubPath(18.f,5.f);arrow.lineTo(9.f,12.f);arrow.lineTo(18.f,19.f);
            if(pointsRight)arrow.applyTransform(juce::AffineTransform::scale(-1.f,1.f).translated(24.f,0.f));
            arrow.applyTransform(juce::AffineTransform::scale(bounds.getWidth()/24.f).translated(bounds.getX(),bounds.getY()));
            g.setColour(juce::Colours::white.withAlpha(isEnabled()? (down?.8f: .95f):.25f));
            g.strokePath(arrow,juce::PathStrokeType(1.8f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        }
    private:
        const bool pointsRight;
    };
    juce::TextButton recordButton {"Record"},removePrintButton {"Delete"},clearRecordButton {"Clear Record"};
    PrintNavigationButton previousPrintButton {false},nextPrintButton {true};
    juce::ComboBox frequencyRangeCombo;
    juce::Label frequencyRangeLabel {{},"Range:"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> frequencyRangeAttachment;
    bool suppressChanges=true,gestureActive=false,pendingChange=false,pendingSelected=false,pendingDuration=false,pendingFrequency=false,pendingPresetTiming=false,wheelChange=false;
    juce::uint32 changedAt=0;
    SpectrumTagAudioProcessor::UserState stableState,changeBefore;
    std::optional<SpectrumTagAudioProcessor::UserState> recordBefore;
    juce::String placementId;
    void beginChange();
    void changed(bool selectedParameters=false);
    void finishChange(bool force=false);
    void removeSelected();
    void selectPrinted(const juce::String&);
    juce::String adjacentPrint(bool next)const;
    void navigatePrint(bool next);
    spectrumtag::PrintEdit editedSelection(bool rebuildMask);
    void updateSelectedPrint();
    spectrumtag::PrintEdit makeEdit();
    void undo(bool redo);
    void refreshLanguage();
    void syncImagePreview();
    void imagePicked(const juce::File&);
    void updateTooltips();
    juce::LookAndFeel_V4 tooltipLookAndFeel;
    juce::TooltipWindow tooltipWindow {this,600};
    juce::TextButton tooltipLanguageButton {"中文"};
    juce::TextButton diagnosticsButton {"Diagnostics"};
    std::unique_ptr<juce::FileChooser> diagnosticChooser;
    spectrumtag::diagnostics::History diagnosticHistory;
    juce::uint32 lastDiagnosticTick=0;
    const double diagnosticStarted=juce::Time::getMillisecondCounterHiRes();
    juce::var diagnosticState() const;
    void exportDiagnostics();
    bool chineseTips=true;
    void selectPalette(int index, bool remember);
    std::array<std::unique_ptr<juce::Button>,8> paletteButtons;
    class EyeButton : public juce::Button {
    public:
        EyeButton():juce::Button("Image"){}
        void paintButton(juce::Graphics& g,bool over,bool) override {
            auto r=getLocalBounds().toFloat().reduced(7,8);juce::Path eye;
            eye.startNewSubPath(r.getX(),r.getCentreY());
            eye.quadraticTo(r.getCentreX(),r.getY()-5,r.getRight(),r.getCentreY());
            eye.quadraticTo(r.getCentreX(),r.getBottom()+5,r.getX(),r.getCentreY());
            g.setColour(over?juce::Colours::white:juce::Colours::lightgrey);g.strokePath(eye,juce::PathStrokeType(1.7f));
            g.fillEllipse(r.getCentreX()-3,r.getCentreY()-3,6,6);
            if(!getToggleState())g.drawLine(r.getX(),r.getBottom()+3,r.getRight(),r.getY()-3,2);
        }
    } imageVisibilityButton;
    int selectedPalette=0,lastStateRevision=0;
    bool imageVisible=true;
    uint64_t lastVisualisedSamples=0;
    double scrollPixelRemainder=0;
    juce::ComboBox fftSizeCombo;
    juce::ComboBox fftScaleCombo;
    spectrumtag::FineSlider   speedSlider;
    spectrumtag::FineSlider   amplitudeSlider;
    juce::ToggleButton invertToggle;
    RoundPrintButton   printButton { "Print" };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<juce::ParameterAttachment> fftSizeAttachment;
    std::unique_ptr<juce::ParameterAttachment> fftScaleAttachment,presetScaleAttachment;
    bool changingScaleControl=false;
    void syncFrequencyScale();
    std::unique_ptr<SliderAttachment> speedAttachment;
    std::unique_ptr<SliderAttachment> amplitudeAttachment;
    std::unique_ptr<ButtonAttachment> invertAttachment;
    std::unique_ptr<ComboAttachment> imageModeAttachment;
    std::unique_ptr<SliderAttachment> imageAmountAttachment;

    // ---- 等比缩放约束 ----
    juce::ComponentBoundsConstrainer resizeConstrainer;

    // ---- Print Trigger 自动化 ↔ UI 同步 ----
    // 用于在 timer 中检测自动化曲线对 printTrigger 的写入：当 trigger 已经被 host
    // 写为 true，但 print 流程结束后我们要负责自动复位回 false（避免"卡 true"）。
    bool lastSeenTrigger = false;
    bool missingImageNotified = false;   // 工程加载时图片文件丢失的非阻塞提示节流

    // ---- 工具：给 Label 应用 BasementGrotesque 字体 ----
    void styleHeaderLabel (juce::Label& l, float height, juce::Colour colour);
    void styleControlLabel (juce::Label& l);

    std::unique_ptr<iisaac::telemetry::Session> telemetrySession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumTagAudioProcessorEditor)
};
