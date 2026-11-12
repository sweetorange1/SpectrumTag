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
    // 供独立版弹窗等外部组件复用同一套字体。
    juce::Font getUiFont (float height) const;

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

    // TextButton：扁平圆角块，primary 为白底黑字，ghost 为无底色
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height,
                          double progress, const juce::String& textToShow) override;

private:
    bool chinese=false;
    juce::Typeface::Ptr typeface;
    juce::Typeface::Ptr chineseTypeface;
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
    void setTypeScale (float scale) { typeScale = scale; repaint(); }
private:
    juce::Typeface::Ptr typeface;
    float typeScale = 1.0f;
};

// ----------------------------------------------------------------------------
//  TransportButton：独立版顶栏的播放/暂停图标按钮（自绘三角与双竖条）
// ----------------------------------------------------------------------------
class TransportButton : public juce::Button
{
public:
    TransportButton();
    void setPlaying (bool value) { if (playing != value) { playing = value; repaint(); } }
    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;
private:
    bool playing = false;
};

// ----------------------------------------------------------------------------
//  TrashButton：清空按钮的垃圾桶图标
// ----------------------------------------------------------------------------
class TrashButton : public juce::Button
{
public:
    TrashButton();
    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;
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
    // Ctrl+拖动：把原对象留在原位打印，编辑器载入一份同样的副本继续被拖动。
    std::function<void()> onDuplicateDrag;
    // 用户选好/拖入图片后先问编辑器：是否允许导入、是否需要先打印上一张。
    std::function<void(const juce::File&)> onImageRequested;

    // 已打印对象在拖动时退回“未打印”态，频谱视图把这次手势直接交给图片框：
    // parentPoint 是图片框父组件（图片裁剪层）坐标中的指针位置，松手时必须
    // 调用 endExternalDrag()，否则撤销步不会被提交。
    void beginExternalDrag (juce::Point<int> parentPoint, bool resizeAtHandle);
    void continueExternalDrag (juce::Point<int> parentPoint);
    void endExternalDrag();

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
    void dragToParentPoint (juce::Point<int> parentPoint);
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
    bool                   externalDrag = false;  // 手势由 SpectrumView 接手
    bool                   duplicatePending = false;  // Ctrl 按下：开始拖动时先复制一份

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
    // 独立版覆盖控件（播放/导出/音频设备/已加载文件名）在编辑器本地坐标中的落点。
    struct StandaloneSlots
    {
        juce::Rectangle<int> play;         // 频谱下方波形预览左侧
        juce::Rectangle<int> exportButton; // Print 圆按钮左侧
        juce::Rectangle<int> device;       // 切换语言与导出诊断按钮之间
        juce::Rectangle<int> fileName;     // 标题行网站链接右侧
    };
    StandaloneSlots standaloneSlots() const;
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
    juce::TextButton recordButton {"Record"},removePrintButton {"Delete"};
    TrashButton clearRecordButton;
    PrintNavigationButton previousPrintButton {false},nextPrintButton {true};
    juce::ComboBox frequencyRangeCombo;
    juce::Label frequencyRangeLabel {{},"Range:"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> frequencyRangeAttachment;
    // 三态图片对象：未打印（可编辑）/ 已打印未聚焦 / 已打印聚焦。
    bool suppressChanges=true,gestureActive=false,pendingChange=false,pendingRevert=false,wheelChange=false;
    juce::uint32 changedAt=0;
    SpectrumTagAudioProcessor::UserState stableState,changeBefore;
    std::optional<SpectrumTagAudioProcessor::UserState> recordBefore;
    juce::String placementId;      // 当前聚焦对象对应的打印 id（仅在聚焦时有意义）
    bool audioReady() const;
    bool printPlacingObject();     // 把“未打印”对象渲染进频谱，成功后对象进入已打印态
    void unprintFocused();         // 已打印聚焦态被修改：撤掉打印，退回未打印态
    void beginChange();
    void changed(bool selectedParameters=false);
    void finishChange(bool force=false);
    void removeSelected();
    void selectPrinted(const juce::String&);
    juce::String adjacentPrint(bool next)const;
    void navigatePrint(bool next);
    spectrumtag::PrintEdit makeEdit();
    void undo(bool redo);
    void refreshLanguage();
    void syncImagePreview();
    void imagePicked(const juce::File&);
    // 图片对象的新建/复制：loadImageObject 只负责载入并落位，撤销步由调用者提交。
    bool loadImageObject(const juce::File& file);
    void duplicateImageObject();   // Ctrl+拖动：原位留一份 Print，拖走副本
    void copyImageObject();        // Ctrl+C：记录当前对象的图片
    void pasteImageObject();       // Ctrl+V：在播放位置新建一份副本
    juce::String clipboardImagePath;
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
    int selectedPalette=0,lastStateRevision=0;
    uint64_t lastVisualisedSamples=0;
    double scrollPixelRemainder=0;
    juce::ComboBox fftSizeCombo;
    juce::ComboBox fftScaleCombo;
    spectrumtag::FineSlider   speedSlider;
    spectrumtag::FineSlider   amplitudeSlider;
    juce::ToggleButton invertToggle;
    // 打印已改为“图片放置模式下点击频谱自动触发”。按钮保持隐藏，
    // 仅作为内部触发入口（自动化、诊断与回归测试仍经由此路径）。
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
