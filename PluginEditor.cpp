#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BinaryData.h"
#include <functional>
#include <JuceHeader.h>
#include <cmath>
#include <limits>

// 5 根竖直线默认半音偏移（从左到右），用于初始化与恢复兜底
static constexpr std::array<int, 5> kDefaultDotSemitoneOffsets = {
    -24, -12, 0, +12, +24
};

void PuponvstAudioProcessorEditor::HeaderComboLookAndFeel::drawComboBox(juce::Graphics& g,
                                                                         int width,
                                                                         int height,
                                                                         bool isButtonDown,
                                                                         int buttonX,
                                                                         int buttonY,
                                                                         int buttonW,
                                                                         int buttonH,
                                                                         juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height).reduced(0.5f);
    const float corner = 8.0f;

    const auto bgTop = juce::Colour(0xFF26262C);
    const auto bgBottom = juce::Colour(0xFF18181D);
    juce::ColourGradient bg(bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float)height, false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, corner);

    // Soft outer glow to match the plugin's luminous style.
    g.setColour(juce::Colour(0x22BFD4FF));
    g.drawRoundedRectangle(bounds.expanded(0.4f), corner + 0.6f, 1.0f);

    auto stroke = juce::Colour(0x70B8BAC3);
    const bool hoverActive = box.isMouseOver(true) || box.isMouseButtonDown();
    if (box.hasKeyboardFocus(true) || isButtonDown || hoverActive)
        stroke = juce::Colour(0xA0DDE6FF);
    g.setColour(stroke);
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    // Keep header combos clean: no dropdown triangle glyph.
}

void PuponvstAudioProcessorEditor::HeaderComboLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(box.getLocalBounds().reduced(8, 2));
    label.setFont(juce::Font(12.5f, juce::Font::plain));
    label.setJustificationType(juce::Justification::centred);
}

void PuponvstAudioProcessorEditor::HeaderComboLookAndFeel::drawComboBoxTextWhenNothingSelected(juce::Graphics& g,
                                                                                                 juce::ComboBox& box,
                                                                                                 juce::Label& label)
{
    juce::ignoreUnused(label);
    g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(box.isEnabled() ? 1.0f : 0.45f));
    g.setFont(juce::Font(12.5f, juce::Font::plain));
    g.drawFittedText(box.getTextWhenNothingSelected(),
                     box.getLocalBounds().reduced(8, 2),
                     juce::Justification::centred,
                     1);
}

// 主编辑器实现
PuponvstAudioProcessorEditor::PuponvstAudioProcessorEditor(PuponvstAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setSize(900, 600);

    // ===== 加载子集字体 AtomicMarker-PUPON-subset.otf =====
    atomicMarkerTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::AtomicMarkerPUPONsubset_otf,
        BinaryData::AtomicMarkerPUPONsubset_otfSize);

    auto makeAtomicFont = [this](float size) -> juce::Font
    {
        if (atomicMarkerTypeface != nullptr)
        {
            juce::Font f(atomicMarkerTypeface);
            f.setHeight(size);
            return f;
        }
        return juce::Font(size, juce::Font::plain);
    };

    // 大标题：Pupon（字号 32，不加粗不斜体）
    titleLabel.setText("PUPON", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(makeAtomicFont(48.0f));
    titleLabel.setInterceptsMouseClicks(false, false);  // 允许鼠标事件穿透，以便编辑器统一处理点击

    // 副标题：仅显示版本号（普通无衬线字体，字号更小）
    versionLabel.setText("v1.0.4", juce::dontSendNotification);
    versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.48f));
    versionLabel.setJustificationType(juce::Justification::centredLeft);
    {
        juce::Font subtitleFont(16.0f, juce::Font::plain);
        subtitleFont.setTypefaceName("Microsoft YaHei UI");
        versionLabel.setFont(subtitleFont);
    }
    versionLabel.setInterceptsMouseClicks(false, false);  // 允许鼠标事件穿透，以便编辑器统一处理点击

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(versionLabel);
    addAndMakeVisible(presetPrevButton);
    addAndMakeVisible(presetCombo);
    addAndMakeVisible(presetNextButton);
    addAndMakeVisible(qualityCombo);
    addAndMakeVisible(formantCombo);

    presetCombo.setLookAndFeel(&headerComboLookAndFeel);
    qualityCombo.setLookAndFeel(&headerComboLookAndFeel);
    formantCombo.setLookAndFeel(&headerComboLookAndFeel);

    presetCombo.setTextWhenNothingSelected(currentPresetName);
    presetCombo.setTooltip("Preset actions and preset list from default folder");
    presetCombo.setJustificationType(juce::Justification::centred);
    presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A1D));
    presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x66A4A4A8));
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xFFE0E0E4));
    presetCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFFBFC0C6));
    presetCombo.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xFFBFD4FF));
    presetCombo.setColour(juce::Label::textWhenEditingColourId, juce::Colour(0xFFE0E0E4));
    presetCombo.onChange = [this] { handlePresetComboChange(); };

    for (auto* c : { &qualityCombo, &formantCombo })
    {
        c->setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xFFBFD4FF));
        c->setJustificationType(juce::Justification::centred);
    }

    for (auto* b : { &presetPrevButton, &presetNextButton })
    {
        b->setTooltip("Quick preset switch");
        b->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE0E3EA));
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFF4F7FF));
        b->setTriggeredOnMouseDown(true);
    }
    presetPrevButton.onClick = [this] { switchPresetByStep(-1); };
    presetNextButton.onClick = [this] { switchPresetByStep(+1); };

    qualityCombo.addItem("Fastest", 1);
    qualityCombo.addItem("Mid", 2);
    qualityCombo.addItem("Best", 3);
    qualityCombo.setTooltip("Pitch quality (higher quality uses more CPU)");
    qualityCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A1D));
    qualityCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x66A4A4A8));
    qualityCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xFFE0E0E4));
    qualityCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFFBFC0C6));

    formantCombo.addItem("Complex", 1);
    formantCombo.addItem("Vocal", 2);
    formantCombo.setTooltip("Formant mode");
    formantCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A1D));
    formantCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x66A4A4A8));
    formantCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xFFE0E0E4));
    formantCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFFBFC0C6));

    headerComboLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF1A1A1D));
    headerComboLookAndFeel.setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFE0E0E4));
    headerComboLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFF2B2B30));
    headerComboLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xFFF4F4F7));

    // 动态获取用户文档目录下的puponpresent文件夹
    presetFolder = juce::File(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName() + "/puponpresent");
    refreshPresetList();
    updatePresetComboDisplayText();

    qualityAttachment = std::make_unique<ComboAttachment>(processor.getAPVTS(), ParameterIDs::rbPitchQuality, qualityCombo);
    formantAttachment = std::make_unique<ComboAttachment>(processor.getAPVTS(), ParameterIDs::rbFormantMode, formantCombo);

    // 设置等比放大约束器
    resizeConstrainer.setFixedAspectRatio(1.5f); // 宽高比为 1.5:1 (900:600)
    resizeConstrainer.setMinimumSize(600, 400);
    resizeConstrainer.setMaximumSize(1920, 1280);
    setConstrainer(&resizeConstrainer);
    
    // 初始化射线原点和默认角度
    // 默认蓝线 45°（右上），红线与其左右对称（左上）
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60); // 移除导航栏
    const auto safeArea = getInteractionSafeArea(controlArea);
    bottomCenter = juce::Point<float>((float) safeArea.getCentreX(), (float) safeArea.getBottom());

    blueAngleDeg = 45.0f;  // 默认蓝线45°（右上方向）
    redRayClockwiseDeg = blueAngleDeg;
    isVerticalRay = false;
    
    // ===== 必须在第一次 push 之前先拉取存档！=====
    // 关键顺序：pushDotParamsToProcessor() 内部会把当前成员值镜像到 editorState 并将
    // hasValidValues 置为 true，所以一旦先 push 再 get，就会拿到刚刚被默认值覆盖的"假"
    // 存档。正确做法是：先看 Processor 里有没有上次 setStateInformation 恢复出来的真存档
    // 或上次 Editor 关闭时残留的状态镜像，有就把它"回灌"到 Editor 成员上，然后再做第一次 push。
    {
        const auto restored = processor.getEditorState();
        if (restored.hasValidValues)
        {
            blueAngleDeg        = restored.blueAngleDeg;
            redRayClockwiseDeg  = blueAngleDeg;
            sigma               = juce::jlimit(0.24f, 8.0f, restored.sigma);
            dotOffsetT          = restored.dotOffsetT;
            dotSemitoneOffsets  = restored.dotSemitoneOffsets;
        }
        else
        {
            dotSemitoneOffsets = kDefaultDotSemitoneOffsets;
        }
    }

    // 初始化完成后先把当前的半音偏移推给音频线程（确保首帧音频就使用正确 st）
    for (int i = 0; i < 5; ++i)
        processor.setDotSemitoneOffset(i, dotSemitoneOffsets[(size_t)i]);

    // 再把当前的 gain/pan 推给音频线程（此时已经包含了恢复出的值，如果有的话）
    pushDotParamsToProcessor();

    // ===== 从 APVTS 读取参数值（宿主自动化参数 / 加载工程时）=====
    // APVTS 参数优先级高于 EditorState（参数系统是现代方式）
    if (auto* redRayClockwiseAngleParam = processor.getAPVTS().getRawParameterValue(ParameterIDs::redRayClockwiseAngle))
    {
        float normalizedValue = redRayClockwiseAngleParam->load();
        blueAngleDeg = normalizedValue * 180.0f;
        redRayClockwiseDeg = blueAngleDeg;
    }
    if (auto* sigmaParam = processor.getAPVTS().getRawParameterValue(ParameterIDs::sigma))
    {
        sigma = juce::jlimit(0.24f, 8.0f, sigmaParam->load());
    }
    if (auto* centerParam = processor.getAPVTS().getRawParameterValue(ParameterIDs::filterCenterSt))
    {
        filterCenterSt = juce::jlimit(kMinSemitone, kMaxSemitone, (int) std::lround(centerParam->load()));
    }
    if (auto* widthParam = processor.getAPVTS().getRawParameterValue(ParameterIDs::filterWidthSt))
    {
        filterWidthSt = juce::jlimit(10, 72, (int) std::lround(widthParam->load()));
    }
    for (int i = 0; i < 5; ++i)
    {
        juce::String paramID = "dot" + juce::String(i);
        if (auto* dotParam = processor.getAPVTS().getRawParameterValue(paramID))
        {
            dotOffsetT[i] = juce::jlimit(0.0f, 1.0f, dotParam->load());
        }
    }

    const juce::String semitoneParamIDs[5] = {
        ParameterIDs::dot0Semitone,
        ParameterIDs::dot1Semitone,
        ParameterIDs::dot2Semitone,
        ParameterIDs::dot3Semitone,
        ParameterIDs::dot4Semitone
    };
    for (int i = 0; i < 5; ++i)
    {
        if (auto* stParam = processor.getAPVTS().getRawParameterValue(semitoneParamIDs[i]))
            dotSemitoneOffsets[(size_t)i] = juce::jlimit(kMinSemitone, kMaxSemitone, (int) std::lround(stParam->load()));
    }

    // ===== 标记初始化完成，从此 push 操作可以正式镜像状态到 Processor =====
    // 之前 setSize() 同步触发的 resized() 会调用 pushDotParamsToProcessor()，但因 initialised=false
    // 所以那次 push 没有覆盖 editorState（保留了 setStateInformation 恢复出的真存档）。
    initialised = true;
    // 此时再做一次 push，让 Processor 的 editorState 与 Editor 当前成员值（含恢复值）保持一致。
    pushDotParamsToProcessor();

    // 添加参数监听器，响应宿主自动化
    processor.getAPVTS().addParameterListener(ParameterIDs::redRayClockwiseAngle, this);
    processor.getAPVTS().addParameterListener(ParameterIDs::sigma, this);
    processor.getAPVTS().addParameterListener(ParameterIDs::filterCenterSt, this);
    processor.getAPVTS().addParameterListener(ParameterIDs::filterWidthSt, this);
    for (int i = 0; i < 5; ++i)
    {
        juce::String paramID = "dot" + juce::String(i);
        processor.getAPVTS().addParameterListener(paramID, this);
        processor.getAPVTS().addParameterListener(semitoneParamIDs[i], this);
    }

    // 启动动效计时器：约 40 FPS，既能保证丝滑，又不浪费 CPU
    startTimerHz(40);
}

PuponvstAudioProcessorEditor::~PuponvstAudioProcessorEditor()
{
    stopTimer();
    cancelPendingUpdate(); // 取消挂起的异步更新，防止析构后回调

    presetCombo.setLookAndFeel(nullptr);
    qualityCombo.setLookAndFeel(nullptr);
    formantCombo.setLookAndFeel(nullptr);

    // 移除参数监听器
    processor.getAPVTS().removeParameterListener(ParameterIDs::redRayClockwiseAngle, this);
    processor.getAPVTS().removeParameterListener(ParameterIDs::sigma, this);
    processor.getAPVTS().removeParameterListener(ParameterIDs::filterCenterSt, this);
    processor.getAPVTS().removeParameterListener(ParameterIDs::filterWidthSt, this);
    for (int i = 0; i < 5; ++i)
    {
        juce::String paramID = "dot" + juce::String(i);
        processor.getAPVTS().removeParameterListener(paramID, this);
        const juce::String semitoneParamIDs[5] = {
            ParameterIDs::dot0Semitone,
            ParameterIDs::dot1Semitone,
            ParameterIDs::dot2Semitone,
            ParameterIDs::dot3Semitone,
            ParameterIDs::dot4Semitone
        };
        processor.getAPVTS().removeParameterListener(semitoneParamIDs[i], this);
    }
}

// ===== 动效时钟：每帧采集音频电平 + 推进时间相位 + 触发重绘 =====
void PuponvstAudioProcessorEditor::timerCallback()
{
    // 1) 从 processor 拿一小段示波器快照，近似计算 RMS 与 peak
    juce::Array<float> snapshot;
    processor.getOscilloscopeSnapshot(snapshot);
    
    float rms = 0.0f;
    float peak = 0.0f;
    const int n = snapshot.size();
    if (n > 0)
    {
        // 只取最近的一段（最多 1024 样本）以反映"当下"的响度
        const int takeN = juce::jmin(n, 1024);
        const int startIdx = n - takeN;
        double sumSq = 0.0;
        for (int i = startIdx; i < n; ++i)
        {
            const float s = snapshot.getUnchecked(i);
            sumSq += (double)s * (double)s;
            peak = juce::jmax(peak, std::abs(s));
        }
        rms = (float)std::sqrt(sumSq / (double)takeN);
    }
    
    // 2) 非线性映射：把 RMS / peak 拉到 0~1 的视觉区间（-60dB ~ 0dB 映射到 0~1）
    auto toLogUnit = [](float v) -> float
    {
        if (v < 1.0e-5f) return 0.0f;
        const float db = juce::Decibels::gainToDecibels(v);
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };
    const float targetLevel = toLogUnit(rms);
    const float targetPeak  = toLogUnit(peak);
    
    // 3) 单极低通平滑：attack 快、release 慢，视觉上手感更"自然呼吸"
    const float attack  = 0.45f; // 上升跟得紧
    const float release = 0.08f; // 下降平滑衰减
    {
        const float a = (targetLevel > audioLevel) ? attack : release;
        audioLevel += (targetLevel - audioLevel) * a;
    }
    {
        const float a = (targetPeak > audioPeak) ? 0.75f : 0.12f;
        audioPeak += (targetPeak - audioPeak) * a;
    }
    
    // 4) 推进时间相位（~40Hz → dt=0.025）
    animPhase += 0.025f;
    if (animPhase > 1.0e6f) animPhase = 0.0f; // 保护，避免极端累积
    
    repaint();
}

void PuponvstAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 暗色主题背景
    g.fillAll(juce::Colour(0xFF121214));
    
    // 绘制导航栏
    auto navBar = getLocalBounds().removeFromTop(60);
    g.setColour(juce::Colour(0xFF1C1C1F));
    g.fillRect(navBar);
    
    // 导航栏边框（极细亮线，像一条微发光 hairline）
    g.setColour(juce::Colour(0x33FFFFFF));
    g.drawLine(0, 60, (float)getWidth(), 60.0f, 1.0f);

    // 标题悬停时改为轻微色散：左右出现红/蓝字形边缘。
    if (isTitleHovered)
    {
        const auto titleTextBounds = getTitleTextBounds().toFloat();
        if (!titleTextBounds.isEmpty())
        {
            juce::GlyphArrangement glyphs;
            const auto titleFont = titleLabel.getFont();
            const float baselineY = titleTextBounds.getY() + titleFont.getAscent();
            glyphs.addLineOfText(titleFont, titleLabel.getText(), titleTextBounds.getX(), baselineY);

            juce::Path titlePath;
            glyphs.createPath(titlePath);

            constexpr float kRedDispersionX = 1.0f;
            constexpr float kBlueDispersionX = 10.0f;

            g.setColour(juce::Colour(0x90FF4A5E)); // left red fringe
            g.fillPath(titlePath, juce::AffineTransform::translation(-kRedDispersionX, 0.0f));

            g.setColour(juce::Colour(0x905FAFFF)); // right blue fringe
            g.fillPath(titlePath, juce::AffineTransform::translation(+kBlueDispersionX, 0.0f));
        }
    }

    
    // 获取控制区域（统一移除导航栏高度，保持与交互判定一致）
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60);

    // ===== 控制区 vignette：中央微亮，四角更暗，加强空间纵深感 =====
    {
        const juce::Rectangle<float> caF = controlArea.toFloat();
        const float cx = caF.getCentreX();
        const float cy = caF.getCentreY();
        const float radius = std::sqrt(caF.getWidth() * caF.getWidth()
                                      + caF.getHeight() * caF.getHeight()) * 0.78f;
        juce::ColourGradient vignette(juce::Colour(0x14FFFFFF), cx, cy,
                                      juce::Colour(0x00000000), cx + radius, cy, true);
        vignette.addColour(0.20, juce::Colour(0x12FFFFFF));
        vignette.addColour(0.45, juce::Colour(0x0CFFFFFF));
        vignette.addColour(0.70, juce::Colour(0x05FFFFFF));
        vignette.addColour(0.90, juce::Colour(0x01000000));
        g.setGradientFill(vignette);
        g.fillRect(caF);

        // 叠加一层纵向柔和渐变，打散同心圈感
        juce::ColourGradient verticalSoft(juce::Colour(0x08000000), cx, caF.getY(),
                                          juce::Colour(0x12000000), cx, caF.getBottom(), false);
        verticalSoft.addColour(0.35, juce::Colour(0x04000000));
        verticalSoft.addColour(0.65, juce::Colour(0x0A000000));
        g.setGradientFill(verticalSoft);
        g.fillRect(caF);
    }
    
    // 直接在控制区域绘制正态分布曲线 (x轴在最底部)
    const float mean = controlArea.getCentreX();
    const float scale = controlArea.getWidth() / 6.0f; // ±3σ范围
    const float maxHeight = controlArea.getHeight() * 0.8f;
    const float baseY = controlArea.getBottom(); // x轴位置在最底部
    
    juce::Path normalPath;
    bool firstPoint = true;
    
    for (float x = controlArea.getX(); x <= controlArea.getRight(); x += 1.0f)
    {
        float normalizedX = (x - mean) / scale;
        float y = maxHeight * std::exp(-0.5f * normalizedX * normalizedX / (sigma * sigma));
        float plotY = baseY - y; // 从底部向上绘制
        
        if (firstPoint)
        {
            normalPath.startNewSubPath(x, plotY);
            firstPoint = false;
        }
        else
        {
            normalPath.lineTo(x, plotY);
        }
    }
    
    // ===== 绘制正态曲线：多层描边实现"发光灯条" bloom =====
    // 外层辉光 alpha 会随音频电平微微呼吸，让灯条在有声音时明显更"电"
    {
        // 灯条的基础色偏冷白（带一点点青），使发光感更"电"
        const juce::Colour glowTint(0xFFBFE6FF);
        // 基础 breath ∈ [1.0, ~1.6] 随音频电平增强
        const float breath = 1.0f + 0.6f * audioLevel;
        // 轻微的自发呼吸（不依赖音频），让空闲时也有一点点活气
        const float idleBreath = 0.9f + 0.1f * std::sin(animPhase * 1.3f);

        struct GlowLayer { float width; juce::Colour colour; };
        const GlowLayer layers[] = {
            { 14.0f, glowTint.withAlpha(juce::jlimit(0.0f, 0.30f, 0.06f * breath * idleBreath)) },
            {  8.0f, glowTint.withAlpha(juce::jlimit(0.0f, 0.45f, 0.12f * breath * idleBreath)) },
            {  4.5f, glowTint.withAlpha(juce::jlimit(0.0f, 0.70f, 0.28f * (0.85f + 0.15f * breath))) },
            {  2.2f, juce::Colours::white.withAlpha(0.85f) },
            {  1.0f, juce::Colours::white }
        };
        for (const auto& L : layers)
        {
            g.setColour(L.colour);
            g.strokePath(normalPath,
                         juce::PathStrokeType(L.width,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }
    }

    // ===== 滤波器控制器：淡黄中轴 + 淡白发光抛物线（开口向下） =====
    {
        const auto safeArea = getInteractionSafeArea(controlArea);
        const float axisY = (float)controlArea.getBottom() - 2.0f;
        const float apexY = (float)controlArea.getCentreY();
        const float axisX = getFilterAxisX(controlArea);

        const float stToPx = (float)safeArea.getWidth() / (float)(kMaxSemitone - kMinSemitone);
        const float halfWidthPx = juce::jmax(4.0f, 0.5f * (float)filterWidthSt * stToPx);
        const float leftX  = juce::jlimit((float)safeArea.getX(), (float)safeArea.getRight(), axisX - halfWidthPx);
        const float rightX = juce::jlimit((float)safeArea.getX(), (float)safeArea.getRight(), axisX + halfWidthPx);

        juce::Path parabolaPath;
        bool started = false;
        for (float x = leftX; x <= rightX; x += 1.0f)
        {
            const float y = getFilterParabolaY(x, controlArea);
            if (!started)
            {
                parabolaPath.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                parabolaPath.lineTo(x, y);
            }
        }

        // 抛物线向下的白色光幕（从曲线垂落到刻度线）
        {
            juce::Path curtainPath = parabolaPath;
            curtainPath.lineTo(rightX, axisY);
            curtainPath.lineTo(leftX, axisY);
            curtainPath.closeSubPath();

            const float curtainStrength = juce::jlimit(0.0f, 1.0f, 0.25f + 0.55f * audioLevel + 0.35f * audioPeak);
            juce::ColourGradient curtainGrad(
                juce::Colours::white.withAlpha(0.14f * curtainStrength), axisX, apexY,
                juce::Colours::white.withAlpha(0.0f), axisX, axisY, false);
            curtainGrad.addColour(0.35, juce::Colours::white.withAlpha(0.10f * curtainStrength));
            curtainGrad.addColour(0.70, juce::Colours::white.withAlpha(0.03f * curtainStrength));
            g.setGradientFill(curtainGrad);
            g.fillPath(curtainPath);
        }

        // 抛物线比正态曲线更淡更细
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.strokePath(parabolaPath, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::white.withAlpha(0.32f));
        g.strokePath(parabolaPath, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float axisPulse = juce::jlimit(0.0f, 1.0f, 0.35f + 0.65f * audioLevel + 0.45f * audioPeak);
        const juce::Colour axisColour = juce::Colour(0xFFFFE9A8).withAlpha(0.88f);
        g.setColour(axisColour.withAlpha(juce::jlimit(0.0f, 1.0f,
            (isDraggingFilterAxis ? 0.58f : 0.28f) + 0.35f * axisPulse)));
        g.drawLine(axisX, apexY, axisX, axisY, (isDraggingFilterAxis ? 3.2f : 2.2f) + 1.0f * axisPulse);
        g.setColour(axisColour.withAlpha(juce::jlimit(0.0f, 1.0f,
            (isDraggingFilterAxis ? 1.0f : 0.72f) + 0.18f * axisPulse)));
        g.drawLine(axisX, apexY, axisX, axisY, (isDraggingFilterAxis ? 2.2f : 1.6f) + 0.35f * axisPulse);

        // 端点小白点移除：避免与红蓝射线发射点视觉重叠。
    }

    // 绘制5根竖直线和交点（由每路 semitone 偏移决定水平位置，随窗口缩放）
    
    // 统一声相映射（与 pushDotParamsToProcessor 保持一致）：
    // 在 [45°, 135°] 内，panMin 固定 0，panMax 在 90°->0、45°/135°->1。
    // 在区间外，panMax 固定 1，panMin 从 0 线性增长到 1（0°/180°）。

    // ===== 圆点本体的"延迟绘制"队列 =====
    // 圆点必须绘制在所有激光（红/蓝/黄）之上 → 把每个圆点本体（halo + 球体 + 活跃环）
    // 包装成 lambda 暂存到这个数组里，等所有激光绘制完毕后再统一执行。
    // 循环中其他元素（竖直引导线 / 波形 / 爬动光）依然按原顺序绘制，保留原有视觉层次。
    std::array<std::function<void()>, 5> dotBodyDraws {};

    // 与音频侧一致的 gain 基准：用于将珍珠发光强度绑定到 band 强度
    const float centerTopY = getDotTrackTopY(2, controlArea);
    const float refHeightPx = baseY - centerTopY;

for (int i = 0; i < 5; ++i)
    {
        const float xPos = getDotColumnX(i, controlArea);
        
        if (xPos < controlArea.getX() || xPos > controlArea.getRight())
            continue;
        
        // 该竖直线与正态曲线的交点（= 圆点轨道的顶端，offsetT=1 时的位置）
        const float trackTopY = getDotTrackTopY(i, controlArea);
        // 圆点实际中心 Y：按 offsetT 在 [底, 轨道顶端] 之间插值
        const juce::Point<float> dotCenter = getDotCenter(i, controlArea);
        const float dotY = dotCenter.y;
        const float curHeightPx = baseY - dotY;
        const float bandGainVisual = (refHeightPx > 1.0f)
            ? juce::jlimit(0.0f, 1.0f, curHeightPx / refHeightPx)
            : 0.0f;

        // 绘制竖直引导线：始终绘制为直线
        const float yTop = trackTopY - 10.0f;
        const float yBot = baseY;
        
        juce::ColourGradient vg(juce::Colours::white.withAlpha(0.45f), xPos, yTop,
                                juce::Colours::white.withAlpha(0.00f), xPos, yBot, false);
        g.setGradientFill(vg);
        g.drawLine(xPos, yTop, xPos, yBot, 1.0f);

        // ===== 弱光爬动光：沿竖直线上下游走 =====
        {
            const float yTop = trackTopY - 10.0f;
            const float yBot = baseY;
            const float span = juce::jmax(1.0f, yBot - yTop);
            
            // 弱光爬动：基于动画相位产生呼吸感
            float posNorm = 0.5f + 0.3f * std::sin(animPhase * 1.5f + (float)i * 0.7f);
            const float yLight = yBot - posNorm * span;
            const float xLight = xPos;

            // 弱光：双层柔光晕
            const float glowR = 6.0f + 3.0f * audioLevel;
            const float baseAlpha = 0.55f;
            juce::ColourGradient glow(juce::Colour(0xFFFFE9A8).withAlpha(baseAlpha), xLight, yLight,
                                      juce::Colour(0x00FFE9A8),                     xLight + glowR, yLight, true);
            glow.addColour(0.4, juce::Colour(0xFFFFE9A8).withAlpha(baseAlpha * 0.45f));
            g.setGradientFill(glow);
            g.fillEllipse(xLight - glowR, yLight - glowR, glowR * 2.0f, glowR * 2.0f);
            // 中心小亮核
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            const float coreR = 1.4f;
            g.fillEllipse(xLight - coreR, yLight - coreR, coreR * 2.0f, coreR * 2.0f);
        }
        
        // ===== 按角度+半音直接计算当前 band 的声相 =====
        const int stDbg = juce::jlimit(kMinSemitone, kMaxSemitone, dotSemitoneOffsets[(size_t)i]);
        const float panDbg = getPanFromBlueAngleAndSemitone(stDbg);

        // 珍珠颜色映射：[纯左, 中间, 纯右] -> [纯红, 纯白, 纯蓝]
        const float panAbsDbg = std::abs(panDbg);
        const juce::Colour sideColour = (panDbg <= 0.0f) ? juce::Colours::red : juce::Colours::blue;
        const juce::Colour mixedColour = juce::Colours::white.interpolatedWith(sideColour, panAbsDbg);

        const juce::Colour leftColour = mixedColour;
        const juce::Colour rightColour = mixedColour;

        const float r = 12.5f;
        const juce::Rectangle<float> dotRect(xPos - r, dotY - r, r * 2.0f, r * 2.0f);

        // ============================================================
        //  高级光球绘制：halo 呼吸 + 双径向等离子球 + 旋转扫光 + 活跃光环
        //  ⬇⬇⬇ 收集到 dotBodyDraws[i]，延迟到所有激光绘制完成后执行（确保圆点在最上层）
        // ============================================================

        // 每个圆点自己的相位偏移，让 5 个球的呼吸不同步（更"活")
        const float dotPhase = animPhase + (float)i * 0.37f;
        // 基于音频电平 + 自发呼吸的脉动系数（用于 halo 扩张 / 内高光强度）
        const float pulse = (0.65f + 0.55f * bandGainVisual)
            * (1.0f
            + 0.35f * audioLevel
            + 0.18f * audioPeak
            + 0.08f * std::sin(dotPhase * 2.1f));

        dotBodyDraws[(size_t) i] =
            [this, &g, i, xPos, dotY, dotRect, leftColour, rightColour, dotPhase, pulse, r, bandGainVisual]()
        {
            // ---- (a) 外层大 halo：随脉动呼吸的超软光晕，像弥散在空气里的发光气体 ----
            {
                juce::Colour haloTint = leftColour.interpolatedWith(rightColour, 0.5f)
                                                  .interpolatedWith(juce::Colours::white, 0.3f);
                const float haloRadius = r * 2.8f * pulse; // 呼吸
                const float alphaScale = bandGainVisual * juce::jlimit(0.0f, 1.2f, 0.7f + 0.6f * audioLevel);
                juce::ColourGradient halo(haloTint.withAlpha(0.55f * alphaScale), xPos, dotY,
                                          haloTint.withAlpha(0.00f),
                                          xPos + haloRadius, dotY, true);
                halo.addColour(0.25, haloTint.withAlpha(0.35f * alphaScale));
                halo.addColour(0.55, haloTint.withAlpha(0.12f * alphaScale));
                g.setGradientFill(halo);
                if (alphaScale > 1.0e-3f)
                {
                    g.fillEllipse(xPos - haloRadius, dotY - haloRadius,
                                  haloRadius * 2.0f, haloRadius * 2.0f);
                }
            }

            // ---- (b) 外壳暗色勾边：在 halo 与球体之间加一圈极细的暗晕，形成"琉璃球"边界感 ----
            {
                const float shellR = r + 1.5f;
                juce::ColourGradient shell(juce::Colour(0x00000000),
                                            xPos, dotY,
                                            juce::Colour(0x66000000),
                                            xPos + shellR, dotY, true);
                shell.addColour(0.80, juce::Colour(0x00000000));
                shell.addColour(0.95, juce::Colour(0x55101018));
                g.setGradientFill(shell);
                g.fillEllipse(xPos - shellR, dotY - shellR, shellR * 2.0f, shellR * 2.0f);
            }

            // ---- (c) 球体本体：圆形裁剪 + 多层叠加，模拟"等离子球"质感 ----
            juce::Path dotClip;
            dotClip.addEllipse(dotRect);
            g.saveState();
            g.reduceClipRegion(dotClip);

            //    c-1 底层：深色底，让后续颜色叠加后呈现"自发光"感（亮处更亮、暗处更深）
            g.setColour(juce::Colour(0xFF0C0C14));
            g.fillRect(dotRect);

            //    c-2 左右水平渐变——保留你的左红右蓝染色语义
            {
                juce::ColourGradient horizGrad(leftColour,  dotRect.getX(),     dotY,
                                               rightColour, dotRect.getRight(), dotY, false);
                g.setGradientFill(horizGrad);
                g.fillRect(dotRect);
            }

            //    c-3 中心径向辉光（球心亮，边缘暗），把球体做成"发光核"
            {
                juce::ColourGradient core(juce::Colours::white.withAlpha(bandGainVisual * (0.75f + 0.2f * audioLevel)),
                                          xPos, dotY,
                                          juce::Colour(0x00000000),
                                          xPos + r, dotY, true);
                core.addColour(0.45, juce::Colours::white.withAlpha(0.30f));
                g.setGradientFill(core);
                g.fillRect(dotRect);
            }

            //    c-4 球面高光：左上径向白斑（模拟单一光源照射下的反射亮点）
            {
                const float hlX = xPos - r * 0.38f;
                const float hlY = dotY - r * 0.42f;
                const float hlR = r * 1.05f;
                juce::ColourGradient highlight(juce::Colours::white.withAlpha(0.90f),
                                               hlX, hlY,
                                               juce::Colours::white.withAlpha(0.00f),
                                               hlX + hlR, hlY, true);
                g.setGradientFill(highlight);
                g.fillEllipse(hlX - hlR, hlY - hlR, hlR * 2.0f, hlR * 2.0f);
            }

            //    c-5 旋转"扫光弧"：一条细细的、带相位旋转的高光弧线，让球面有轻微"滚动"的光动感
            {
                const float sweepPhase = dotPhase * 0.8f;
                const float arcCenterAngle = sweepPhase;
                const float arcWidth = juce::MathConstants<float>::pi * 0.35f; // 弧覆盖约 63°
                juce::Path sweepArc;
                // 画在距球心 r*0.72 的一条圆弧带（fromRadian/toRadian 使用 JUCE 顺时针/北为 0 的约定）
                sweepArc.addCentredArc(xPos, dotY, r * 0.72f, r * 0.72f, 0.0f,
                                        arcCenterAngle - arcWidth * 0.5f,
                                        arcCenterAngle + arcWidth * 0.5f,
                                        true);
                g.setColour(juce::Colours::white.withAlpha(0.30f + 0.25f * audioLevel));
                g.strokePath(sweepArc,
                             juce::PathStrokeType(1.2f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
            }

            //    c-6 球底阴影：径向暗晕靠近球体下边，增加立体感 + 减少"扁平贴纸"感
            {
                const float sx = xPos + r * 0.10f;
                const float sy = dotY + r * 0.35f;
                juce::ColourGradient rim(juce::Colour(0x00000000), sx, sy,
                                         juce::Colour(0x77000000),
                                         sx + r * 1.15f, sy + r * 1.15f, true);
                rim.addColour(0.70, juce::Colour(0x00000000));
                g.setGradientFill(rim);
                g.fillRect(dotRect);
            }
            g.restoreState();

            // ---- (d) 活跃环：被拖动 / 或高电平时自动"亮起"，带呼吸感 ----
            const bool isActive = (draggingDotIndex == i);
            {
                // 环的整体不透明度 = 基础(活跃或电平驱动) × 呼吸
                float ringAlpha = isActive ? 1.0f
                                            : juce::jlimit(0.0f, 1.0f, audioLevel * 1.2f);
                ringAlpha *= (0.75f + 0.25f * std::sin(dotPhase * 2.6f));
                ringAlpha *= bandGainVisual;
                if (ringAlpha > 0.02f)
                {
                    // 外圈大柔晕
                    g.setColour(juce::Colours::white.withAlpha(0.30f * ringAlpha));
                    g.drawEllipse(dotRect.expanded(4.0f + 2.0f * audioLevel), 1.0f);
                    // 内圈清晰环
                    g.setColour(juce::Colours::white.withAlpha(0.75f * ringAlpha));
                    g.drawEllipse(dotRect, 1.2f);
                }
            }
        };
    }
    
    // 绘制射线：以下边缘中点为原点(bottomCenter)，根据角度计算边界交点
    // 蓝线角度 = blueAngleDeg（0°=向右，90°=向上，180°=向左）
    // 红线角度 = 180° - blueAngleDeg（左右对称）
    float redAngleDeg = 180.0f - blueAngleDeg;
    juce::Point<float> redRayEnd  = calculateRayEndByAngle(redAngleDeg, controlArea);
    juce::Point<float> blueRayEnd = calculateRayEndByAngle(blueAngleDeg, controlArea);
    
    // ========================================================================
    //  激光射线：沿线方向渐变（根部粗+白→末端细+纯色）+ 能量光斑沿线游走 + 抖动
    // ========================================================================
    //
    // 关键改动：
    //  1) 不再用"等宽"多层描边，改成沿射线法线方向手绘多边形条带，宽度从根部 → 末端渐减
    //  2) 颜色沿线渐变：根部亮纯色 → 末端暗纯色（整条不再混白，也不再向外发射白色光晕）
    //  3) 根部宽度随 audioPeak 做轻微爆闪；末端柔和 fade-out（避免硬切头）
    //  4) 能量光斑按 animPhase 在线段上循环游走；音频响度越高越亮越快
    //  5) 根部的强度随 audioPeak 变化（有节奏时激光会"一跳一跳"）
    auto drawLaser = [&](juce::Point<float> p0, juce::Point<float> p1, juce::Colour pure)
    {
        const juce::Point<float> vec = p1 - p0;
        const float len = vec.getDistanceFromOrigin();
        if (len < 1e-3f) return;
        
        const juce::Point<float> dir    { vec.x / len, vec.y / len };
        const juce::Point<float> normal { -dir.y,       dir.x };
        
        // 根部爆闪强度（受 peak 驱动，基础 1.0，峰值时 1.30）
        const float rootBurst = 1.0f + 0.30f * audioPeak;
        // 每层"带状激光"的参数
        struct Band {
            float wRoot;     // 根部半宽
            float wTip;      // 末端半宽
            float aRootPure; // 根部纯色 alpha
            float aTipPure;  // 末端纯色 alpha
            float mixWhite;  // 根部白混合（0 不混，1 全白）
        };
        // 注意：每层宽度根部都比末端粗很多（wRoot >> wTip），视觉上实现"从粗变细"
        // 6 层从外向内逐渐变细变亮，构造立体的"激光辉光层叠"——保持纯红/蓝，不混白
        //
        // 调整：根部整体收窄约 50%（之前底部太粗），但末端宽度保持原值，
        //       让激光看起来更挺拔，远端的柔和拖尾不变。
        const Band bands[] = {
            // 最外层：超宽柔和散射，营造"空气中的红/蓝雾"
            { 11.0f * rootBurst, 4.5f,  0.10f * rootBurst, 0.015f, 0.0f },
            // 外层辉光：典型的"激光发光晕"
            {  7.0f * rootBurst, 3.0f,  0.22f * rootBurst, 0.03f,  0.0f },
            // 中外层
            {  4.5f * rootBurst, 2.0f,  0.40f * rootBurst, 0.06f,  0.0f },
            // 中层
            {  2.8f * rootBurst, 1.3f,  0.65f * rootBurst, 0.15f,  0.0f },
            // 次内核
            {  1.6f * rootBurst, 0.85f, 0.90f * rootBurst, 0.40f,  0.0f },
            // 亮核：最细，根部不再泡白，保持高纯度红/蓝
            {  0.9f,              0.55f, 1.00f,             0.85f,  0.0f }
        };
        
        // 构造沿线长度的渐变（JUCE 会按两端的坐标插值；中间可以插色标）
        for (const auto& b : bands)
        {
            // 多边形梯形：p0 左 + p0 右 + p1 右 + p1 左
            const juce::Point<float> pL0 = p0 + normal * b.wRoot;
            const juce::Point<float> pR0 = p0 - normal * b.wRoot;
            const juce::Point<float> pL1 = p1 + normal * b.wTip;
            const juce::Point<float> pR1 = p1 - normal * b.wTip;
            
            juce::Path band;
            band.startNewSubPath(pL0);
            band.lineTo(pR0);
            band.lineTo(pR1);
            band.lineTo(pL1);
            band.closeSubPath();
            
            // 沿 p0 → p1 方向做颜色渐变：
            //   根部 = 纯色往白混 mixWhite，alpha = aRootPure
            //   末端 = 纯色，alpha = aTipPure（收尾更暗）
            const juce::Colour cRoot = pure.interpolatedWith(juce::Colours::white, b.mixWhite)
                                            .withAlpha(juce::jlimit(0.0f, 1.0f, b.aRootPure));
            const juce::Colour cTip  = pure.withAlpha(juce::jlimit(0.0f, 1.0f, b.aTipPure));
            
            juce::ColourGradient laserGrad(cRoot, p0.x, p0.y,
                                           cTip,  p1.x, p1.y, false);
            // 插入一个中间色标：靠近根部先快速"退白"，再缓慢降到末端纯色
            laserGrad.addColour(0.18,
                pure.interpolatedWith(juce::Colours::white, b.mixWhite * 0.55f)
                    .withAlpha(juce::jlimit(0.0f, 1.0f,
                        b.aRootPure * 0.7f + b.aTipPure * 0.3f)));
            laserGrad.addColour(0.55,
                pure.withAlpha(juce::jlimit(0.0f, 1.0f,
                    b.aRootPure * 0.3f + b.aTipPure * 0.7f)));
            
            g.setGradientFill(laserGrad);
            g.fillPath(band);
        }
        // 取消了沿激光方向游走的"能量光斑"——激光保持平静的纯色渐变
    };
    
    drawLaser(bottomCenter, redRayEnd,  juce::Colour(0xFFFF3B3B));
    drawLaser(bottomCenter, blueRayEnd, juce::Colour(0xFF3B7BFF));
    
    // ===== 射线发射点：音频电平驱动的呼吸光斑 =====
    {
        // 基础 22px 光晕 + 电平脉动 + 峰值冲击
        const float basePulse = 1.0f
            + 0.40f * audioLevel
            + 0.55f * audioPeak
            + 0.10f * std::sin(animPhase * 3.1f);
        const float originGlowR = 22.0f * basePulse;
        const float centerAlpha = juce::jlimit(0.55f, 1.0f, 0.80f + 0.20f * audioLevel);
        
        // 内外两层：外层紫色大光晕（红蓝混合），内层更集中的白热核
        juce::ColourGradient outer(juce::Colour(0x55E0C8FF).withAlpha(0.55f * basePulse),
                                    bottomCenter.x, bottomCenter.y,
                                    juce::Colour(0x00FFFFFF),
                                    bottomCenter.x + originGlowR, bottomCenter.y, true);
        outer.addColour(0.40, juce::Colour(0x88E0C8FF).withAlpha(0.35f));
        outer.addColour(0.70, juce::Colour(0x44A098FF).withAlpha(0.15f));
        g.setGradientFill(outer);
        g.fillEllipse(bottomCenter.x - originGlowR, bottomCenter.y - originGlowR,
                      originGlowR * 2.0f, originGlowR * 2.0f);
        
        const float innerR = 9.0f * basePulse;
        juce::ColourGradient inner(juce::Colours::white.withAlpha(centerAlpha),
                                    bottomCenter.x, bottomCenter.y,
                                    juce::Colours::white.withAlpha(0.0f),
                                    bottomCenter.x + innerR, bottomCenter.y, true);
        g.setGradientFill(inner);
        g.fillEllipse(bottomCenter.x - innerR, bottomCenter.y - innerR,
                      innerR * 2.0f, innerR * 2.0f);
        
        // 最中心白点
        g.setColour(juce::Colours::white);
        const float dotR = 2.5f + 1.0f * audioPeak;
        g.fillEllipse(bottomCenter.x - dotR, bottomCenter.y - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    // ===== 最上层：圆点本体（在所有激光绘制完成后执行）=====
    // 之前在主循环里已把每个圆点的"halo + 球体 + 活跃环"打包成 lambda，
    // 这里按顺序执行，使圆点视觉位于红/蓝/黄三道激光之上。
    for (auto& f : dotBodyDraws)
        if (f) f();

    // ===== 下方半音刻度线（-36st ~ +36st）=====
    {
        const float axisY = (float)controlArea.getBottom() - 2.0f;
        for (int st = kMinSemitone; st <= kMaxSemitone; ++st)
        {
            const float x = semitoneToX(st, controlArea);
            const bool isMajor = (st % 12) == 0;
            const float tickH = isMajor ? 14.0f : 8.0f;

            g.setColour(juce::Colours::white.withAlpha(isMajor ? 0.65f : 0.30f));
            g.drawLine(x, axisY, x, axisY - tickH, isMajor ? 1.4f : 1.0f);

            if (isMajor)
            {
                g.setColour(juce::Colours::white.withAlpha(0.78f));
                g.setFont(12.0f);
                g.drawText(juce::String(st) + "st", (int)x - 22, (int)axisY - 30, 44, 14,
                           juce::Justification::centred, false);
            }
        }

        // 当前拖动竖线的 st 提示
        if (draggingDotColumnIndex >= 0 && draggingDotColumnIndex < 5)
        {
            const int idx = draggingDotColumnIndex;
            const float x = getDotColumnX(idx, controlArea);
            const float y = getDotCenter(idx, controlArea).y;

            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.setFont(13.0f);
            g.drawText(juce::String(dotSemitoneOffsets[(size_t)idx]) + "st",
                       (int)x - 24, (int)y - 52, 48, 16,
                       juce::Justification::centred, false);
        }
    }
}

juce::Rectangle<int> PuponvstAudioProcessorEditor::getInteractionSafeArea(const juce::Rectangle<int>& controlArea) const
{
    auto safe = controlArea;
    constexpr int kSafeInsetX = 14;
    constexpr int kSafeInsetBottom = 10;

    if (safe.getWidth() > (kSafeInsetX * 2 + 20))
        safe = safe.withTrimmedLeft(kSafeInsetX).withTrimmedRight(kSafeInsetX);
    if (safe.getHeight() > (kSafeInsetBottom + 20))
        safe = safe.withTrimmedBottom(kSafeInsetBottom);

    return safe;
}

void PuponvstAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60);

    // 仅大标题文字区域支持点击跳转官网。
    const juce::Point<int> mousePosLocal = event.getPosition();
    if (getTitleTextBounds().contains(mousePosLocal))
    {
        juce::URL("https://iisaacbeats.cn").launchInDefaultBrowser();
        return;
    }

    juce::Point<float> mousePos = event.position;
    
    // 鼠标必须在控制区域内才处理
    if (!controlArea.contains(mousePos.toInt()))
        return;
    
    // 重置所有拖动状态
    isDraggingRedLine = false;
    isDraggingBlueLine = false;
    isDraggingNormalCurve = false;
    isDraggingFilterAxis = false;
    isDraggingFilterParabola = false;
    draggingDotIndex = -1;
    draggingDotColumnIndex = -1;
    
    // 统一的容差设置
    const float dotHitRadius = 14.0f;         // 圆点命中半径（圆点半径 12.5 + 余量）
    const float columnHitHalfWidth = 8.0f;    // 竖线命中半宽
    const float filterAxisHitHalfWidth = 9.0f;
    const float filterParabolaThreshold = 10.0f;
    const float normalCurveThreshold = 12.0f; // 正态曲线容差
    const float rayThreshold = 8.0f;          // 射线容差
    
    // ========== 优先级 1: 检测5个圆点（体积大，需优先于曲线/射线） ==========
    for (int i = 0; i < 5; ++i)
    {
        juce::Point<float> dotCenter = getDotCenter(i, controlArea);
        if (mousePos.getDistanceFrom(dotCenter) <= dotHitRadius)
        {
            draggingDotIndex = i;
            return;
        }
    }

    // ========== 优先级 2: 检测滤波器中轴（低于珍珠 gain 控件，避免抢占圆点拖动） ==========
    {
        const float axisX = getFilterAxisX(controlArea);
        const float axisY = (float)controlArea.getBottom() - 2.0f;
        const float apexY = (float)controlArea.getCentreY();
        if (mousePos.x >= axisX - filterAxisHitHalfWidth && mousePos.x <= axisX + filterAxisHitHalfWidth
            && mousePos.y >= apexY && mousePos.y <= axisY)
        {
            isDraggingFilterAxis = true;
            return;
        }
    }

    // ========== 优先级 3: 检测竖直线（允许水平拖动改变 st） ==========
    for (int i = 0; i < 5; ++i)
    {
        const float xPos = getDotColumnX(i, controlArea);
        const float yTop = getDotTrackTopY(i, controlArea) - 10.0f;
        const float yBot = (float)controlArea.getBottom();

        if (mousePos.x >= xPos - columnHitHalfWidth && mousePos.x <= xPos + columnHitHalfWidth
            && mousePos.y >= yTop && mousePos.y <= yBot)
        {
            draggingDotColumnIndex = i;
            const juce::String semitoneParamIDs[5] = {
                ParameterIDs::dot0Semitone,
                ParameterIDs::dot1Semitone,
                ParameterIDs::dot2Semitone,
                ParameterIDs::dot3Semitone,
                ParameterIDs::dot4Semitone
            };
            if (auto* param = processor.getAPVTS().getParameter(semitoneParamIDs[i]))
                param->beginChangeGesture();
            return;
        }
    }

    // ========== 优先级 4: 检测滤波器抛物线（拖动改变宽度） ==========
    if (distanceToFilterParabola(mousePos, controlArea) <= filterParabolaThreshold)
    {
        isDraggingFilterParabola = true;
        return;
    }

    // ========== 优先级 5: 检测正态分布曲线 ==========
    float distToCurve = distanceToNormalCurve(mousePos, controlArea);
    
    if (distToCurve <= normalCurveThreshold)
    {
        isDraggingNormalCurve = true;
        return;
    }
    
    // ========== 优先级 6: 检测红色射线 ==========
    juce::Point<float> rayStart = bottomCenter;
    float redAngleDeg = 180.0f - blueAngleDeg;  // 红线角度（左右对称）
    juce::Point<float> redActualEnd  = calculateRayEndByAngle(redAngleDeg, controlArea);
    
    if (isPointNearLine(mousePos, rayStart, redActualEnd, rayThreshold))
    {
        isDraggingRedLine = true;
        return;
    }
    
    // ========== 优先级 7: 检测蓝色射线 ==========
    juce::Point<float> blueActualEnd = calculateRayEndByAngle(blueAngleDeg, controlArea);
    
    if (isPointNearLine(mousePos, rayStart, blueActualEnd, rayThreshold))
    {
        isDraggingBlueLine = true;
        return;
    }
    
    // 都没有命中，忽略此次按下
}

void PuponvstAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60);

    // 如果没有命中任何可拖动项，mouseDown中没有设置拖动状态，这里直接忽略
    if (!isDraggingNormalCurve && !isDraggingRedLine && !isDraggingBlueLine
        && !isDraggingFilterAxis && !isDraggingFilterParabola
        && draggingDotIndex < 0 && draggingDotColumnIndex < 0)
        return;
    
    juce::Point<float> mousePos = event.position;
    
    // 限制鼠标位置在控制区域内
    mousePos.x = juce::jlimit<float>((float)controlArea.getX(), (float)controlArea.getRight(), mousePos.x);
    mousePos.y = juce::jlimit<float>((float)controlArea.getY(), (float)controlArea.getBottom(), mousePos.y);
    
    // ========== 1. 处理竖线水平拖动（半音整数吸附） ==========
    if (draggingDotColumnIndex >= 0)
    {
        const int i = draggingDotColumnIndex;
        const int st = xToSemitone(mousePos.x, controlArea);
        dotSemitoneOffsets[(size_t)i] = st;

        // 半音偏移实时同步给音频引擎
        processor.setDotSemitoneOffset(i, st);

        const juce::String semitoneParamIDs[5] = {
            ParameterIDs::dot0Semitone,
            ParameterIDs::dot1Semitone,
            ParameterIDs::dot2Semitone,
            ParameterIDs::dot3Semitone,
            ParameterIDs::dot4Semitone
        };
        if (auto* param = processor.getAPVTS().getParameter(semitoneParamIDs[i]))
            param->setValue(param->convertTo0to1((float) st));

        pushDotParamsToProcessor();
        repaint();
        return;
    }

    // ========== 2. 处理圆点竖向拖动 ==========
    if (draggingDotIndex >= 0)
    {
        const int i = draggingDotIndex;
        const float trackTopY = getDotTrackTopY(i, controlArea);
        const float bottomY = (float)controlArea.getBottom();
        const float trackLen = bottomY - trackTopY; // 轨道长度（屏幕像素，正值）
        
        // 根据鼠标 y 反推进度 t ∈ [0, 1]
        float t = 1.0f;
        if (trackLen > 1e-3f)
        {
            t = (bottomY - mousePos.y) / trackLen;
            t = juce::jlimit(0.0f, 1.0f, t);
        }
        
        // 圆点参数解耦：仅更新当前被拖动的一个 dot
        dotOffsetT[(size_t)i] = t;

        // 同步更新对应 APVTS 参数（拖动中不通知宿主，避免高频回调）
        juce::String paramID = "dot" + juce::String(i);
        if (auto* param = processor.getAPVTS().getParameter(paramID))
            param->setValue(t);
        
        pushDotParamsToProcessor();
        repaint();
        return;
    }

    // ========== 3. 处理滤波器中轴拖动（中心偏移 st） ==========
    if (isDraggingFilterAxis)
    {
        filterCenterSt = xToSemitone(mousePos.x, controlArea);

        // 直接下发到引擎，保证拖动时实时听到变化
        processor.setFilterCenterOffsetSemitones(filterCenterSt);

        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::filterCenterSt))
        {
            const float norm = ((float)filterCenterSt - (float)kMinSemitone)
                             / (float)(kMaxSemitone - kMinSemitone);
            param->setValue(juce::jlimit(0.0f, 1.0f, norm));
        }

        repaint();
        return;
    }

    // ========== 4. 处理滤波器抛物线拖动（宽度 st） ==========
    if (isDraggingFilterParabola)
    {
        const float axisX = getFilterAxisX(controlArea);
        const float axisY = (float)controlArea.getBottom() - 2.0f;
        const float apexY = (float)controlArea.getCentreY();

        const float yOnCurve = juce::jlimit(apexY + 1.0f, axisY, mousePos.y);
        const float h = juce::jmax(1.0f, axisY - apexY);
        const float dx = std::abs(mousePos.x - axisX);
        const float halfWidthPx = juce::jmax(2.0f, dx * std::sqrt(h / juce::jmax(1.0f, yOnCurve - apexY)));

        const auto safeArea = getInteractionSafeArea(controlArea);
        const float pxToSt = (float)(kMaxSemitone - kMinSemitone) / juce::jmax(1.0f, (float)safeArea.getWidth());
        const int widthStFromMouse = (int)std::lround(2.0f * halfWidthPx * pxToSt);
        filterWidthSt = juce::jlimit(10, 72, widthStFromMouse);

        // 直接下发到引擎，保证拖动时实时听到变化
        processor.setFilterWidthSemitones(filterWidthSt);

        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::filterWidthSt))
        {
            const float norm = ((float)filterWidthSt - 10.0f) / 62.0f;
            param->setValue(juce::jlimit(0.0f, 1.0f, norm));
        }

        repaint();
        return;
    }

    // ========== 1. 处理正态曲线拖动 ==========
    if (isDraggingNormalCurve)
    {
        // 根据鼠标位置反推 sigma，使曲线经过鼠标点
        const float mean = controlArea.getCentreX();
        const float scale = controlArea.getWidth() / 6.0f;
        const float maxHeight = controlArea.getHeight() * 0.8f;
        const float baseY = controlArea.getBottom();
        
        float normalizedX = (mousePos.x - mean) / scale;
        float currentY = baseY - mousePos.y;
        currentY = juce::jlimit(1.0f, maxHeight, currentY);
        
        float yRatio = currentY / maxHeight;
        
        if (std::abs(normalizedX) > 0.01f && yRatio < 0.9999f)
        {
            float logValue = -2.0f * std::log(yRatio);
            if (logValue > 1e-6f)
            {
            float newSigma = std::abs(normalizedX) / std::sqrt(logValue);
                sigma = juce::jlimit(0.24f, 8.0f, newSigma);
                
                // 同步更新 APVTS 参数（不通知宿主，避免死锁）
                // 归一化公式：将 [0.24, 8.0] 映射到 [0.0, 1.0]
                if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::sigma))
                {
                    float normalizedValue = (sigma - 0.24f) / (8.0f - 0.24f);
                    normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
                    param->setValue(normalizedValue);
                }
            }
        }
        
        // sigma 变了 → 各圆点轨道顶端变了 → 记录的 dotOffsetT 对应的实际高度也变
        // gain 是 dotOffsetT，本身不变；pan 也不依赖 sigma，所以严格来说无需同步
        // 但为了保留 "屏幕显示 ⇔ 音频输出一致" 的语义，统一推一次
        pushDotParamsToProcessor();
        repaint();
    }
    // ========== 2. 处理红/蓝色射线拖动（基于角度） ==========
    else if (isDraggingRedLine || isDraggingBlueLine)
    {
        // 根据鼠标相对原点(bottomCenter)的位置计算角度
        // 角度定义（与 atan2 一致）：0° = 水平向右，90° = 垂直向上，180° = 水平向左
        // 蓝线角度 = blueAngleDeg，红线角度 = 180° - blueAngleDeg（对称）
        
        float dx = mousePos.x - bottomCenter.x;
        float dy = bottomCenter.y - mousePos.y;  // 转换为数学坐标系（Y向上）
        
        // 计算角度（度数），atan2 返回范围 [-180°, 180°]
        float angleRad = std::atan2(dy, dx);
        float mouseAngleDeg = angleRad * (180.0f / juce::MathConstants<float>::pi);
        
        // 转换为 [0°, 360°) 范围
        if (mouseAngleDeg < 0) mouseAngleDeg += 360.0f;
        
        // 限制到上半平面 [0°, 180°]（只允许 Y >= 0 的方向）
        if (mouseAngleDeg > 180.0f) mouseAngleDeg = 360.0f - mouseAngleDeg;
        mouseAngleDeg = juce::jlimit(0.0f, 180.0f, mouseAngleDeg);
        
        // 根据拖动的是红线还是蓝线，设置对应的角度
        if (isDraggingRedLine)
        {
            // 拖动红线：鼠标位置对应红线角度
            float redAngleDeg = mouseAngleDeg;
            blueAngleDeg = 180.0f - redAngleDeg;  // 蓝线角度 = 180° - 红线角度（对称）
        }
        else
        {
            // 拖动蓝线：鼠标位置对应蓝线角度
            blueAngleDeg = mouseAngleDeg;
        }
        
        // 红线顺时针角度（以水平向左为 0°）与 blueAngleDeg 数值等价
        redRayClockwiseDeg = blueAngleDeg;

        isVerticalRay = false;
        
        // 同步更新 APVTS 参数：归一化值 = redRayClockwiseDeg / 180.0f
        // 其中 redRayClockwiseDeg 与 blueAngleDeg 数值等价
        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::redRayClockwiseAngle))
            param->setValue(blueAngleDeg / 180.0f);
        
        // 射线斜率变了 → 每个圆点的染色进度变了 → pan 变了
        pushDotParamsToProcessor();
        repaint();
    }
}

void PuponvstAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    const bool wasDraggingDot = (draggingDotIndex >= 0);
    const bool wasDraggingDotColumn = (draggingDotColumnIndex >= 0);
    const int releasedDotColumnIndex = draggingDotColumnIndex;
    const bool wasDraggingRay = (isDraggingRedLine || isDraggingBlueLine);
    const bool wasDraggingCurve = isDraggingNormalCurve;
    const bool wasDraggingFilterAxis = isDraggingFilterAxis;
    const bool wasDraggingFilterParabola = isDraggingFilterParabola;

    isDraggingRedLine = false;
    isDraggingBlueLine = false;
    isDraggingNormalCurve = false;
    isDraggingFilterAxis = false;
    isDraggingFilterParabola = false;
    draggingDotIndex = -1;
    draggingDotColumnIndex = -1;
    
    // 拖动结束后，才通知宿主参数变化（避免拖动时频繁触发宿主回调导致死锁）
    if (wasDraggingDot)
    {
        // 通知宿主圆点参数变化
        for (int i = 0; i < 5; ++i)
        {
            juce::String paramID = "dot" + juce::String(i);
            if (auto* param = processor.getAPVTS().getParameter(paramID))
                param->setValueNotifyingHost(param->getValue());
        }
    }

    if (wasDraggingDotColumn)
    {
        const juce::String semitoneParamIDs[5] = {
            ParameterIDs::dot0Semitone,
            ParameterIDs::dot1Semitone,
            ParameterIDs::dot2Semitone,
            ParameterIDs::dot3Semitone,
            ParameterIDs::dot4Semitone
        };
        // 仅提交本次实际被拖动的一路，避免松手时触发 5 路无效参数通知。
        if (releasedDotColumnIndex >= 0 && releasedDotColumnIndex < 5)
        {
            if (auto* param = processor.getAPVTS().getParameter(semitoneParamIDs[(size_t)releasedDotColumnIndex]))
            {
                param->setValueNotifyingHost(param->getValue());
                param->endChangeGesture();
            }
        }
    }

    if (wasDraggingRay)
    {
        // 通知宿主射线参数变化
        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::redRayClockwiseAngle))
            param->setValueNotifyingHost(param->getValue());
    }
    
    if (wasDraggingCurve)
    {
        // 通知宿主 sigma 参数变化
        // 注意：mouseDrag 中已经通过 param->setValue() 设置了归一化值
        // 这里只需要触发通知，让宿主知道参数发生了变化
        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::sigma))
        {
            // 获取当前参数的归一化值
            float currentNormalized = param->getValue();
            
            // 使用 setValueNotifyingHost 触发通知
            // 注意：parameterChanged 回调接收的是去归一化后的实际值
            param->beginChangeGesture();
            param->setValueNotifyingHost(currentNormalized);
            param->endChangeGesture();
        }
    }

    if (wasDraggingFilterAxis)
    {
        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::filterCenterSt))
            param->setValueNotifyingHost(param->getValue());
    }

    if (wasDraggingFilterParabola)
    {
        if (auto* param = processor.getAPVTS().getParameter(ParameterIDs::filterWidthSt))
            param->setValueNotifyingHost(param->getValue());
    }

    if (wasDraggingDot || wasDraggingDotColumn || wasDraggingFilterAxis || wasDraggingFilterParabola)
        repaint(); // 清除被拖动圆点/竖线的高亮描边
}

void PuponvstAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    const bool shouldHover = getTitleTextBounds().contains(event.getPosition());
    if (shouldHover != isTitleHovered)
    {
        isTitleHovered = shouldHover;
        setMouseCursor(isTitleHovered ? juce::MouseCursor::PointingHandCursor
                                      : juce::MouseCursor::NormalCursor);
        repaint(titleLabel.getBounds().expanded(24, 14));
    }
}

void PuponvstAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    if (isTitleHovered)
    {
        isTitleHovered = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(titleLabel.getBounds().expanded(24, 14));
    }
}

// 辅助函数：检查点是否靠近线段（返回点到线段的距离是否小于阈值）
bool PuponvstAudioProcessorEditor::isPointNearLine(const juce::Point<float>& point, 
                    const juce::Point<float>& lineStart, 
                    const juce::Point<float>& lineEnd, 
                    float threshold)
{
    // 计算点到线段的最短距离
    float lineLength = lineStart.getDistanceFrom(lineEnd);
    if (lineLength < 1.0f) return point.getDistanceFrom(lineStart) <= threshold;
    
    // 使用参数化的投影 t，钳制在 [0, 1] 保证投影点落在线段上
    float t = juce::jlimit(0.0f, 1.0f, 
        ((point.x - lineStart.x) * (lineEnd.x - lineStart.x) + 
         (point.y - lineStart.y) * (lineEnd.y - lineStart.y)) / (lineLength * lineLength));
    
    juce::Point<float> closestPoint(
        lineStart.x + t * (lineEnd.x - lineStart.x),
        lineStart.y + t * (lineEnd.y - lineStart.y)
    );
    
    return point.getDistanceFrom(closestPoint) <= threshold;
}

// 计算正态曲线在指定x位置的Y坐标
float PuponvstAudioProcessorEditor::getNormalCurveY(float x, const juce::Rectangle<int>& controlArea) const
{
    const float mean = controlArea.getCentreX();
    const float scale = controlArea.getWidth() / 6.0f;
    const float maxHeight = controlArea.getHeight() * 0.8f;
    const float baseY = (float)controlArea.getBottom();
    
    float normalizedX = (x - mean) / scale;
    float y = maxHeight * std::exp(-0.5f * normalizedX * normalizedX / (sigma * sigma));
    return baseY - y;
}

juce::Rectangle<int> PuponvstAudioProcessorEditor::getTitleTextBounds() const
{
    const auto labelBounds = titleLabel.getBounds();
    if (labelBounds.isEmpty())
        return {};

    const auto font = titleLabel.getFont();
    const auto text = titleLabel.getText();
    const int textWidth = juce::jmax(0, font.getStringWidth(text));
    const int textHeight = juce::jmax(0, (int)std::ceil(font.getHeight()));

    const int actualWidth = juce::jmin(textWidth, labelBounds.getWidth());
    const int actualHeight = juce::jmin(textHeight, labelBounds.getHeight());

    return { labelBounds.getX(),
             labelBounds.getY() + (labelBounds.getHeight() - actualHeight) / 2,
             actualWidth,
             actualHeight };
}

// 计算点到正态曲线的最短距离（使用采样法，遍历曲线段计算最短距离）
float PuponvstAudioProcessorEditor::distanceToNormalCurve(const juce::Point<float>& point, 
                                                          const juce::Rectangle<int>& controlArea) const
{
    const float searchRange = 40.0f;
    const float sampleStep = 2.0f;
    
    float minX = juce::jmax((float)controlArea.getX(), point.x - searchRange);
    float maxX = juce::jmin((float)controlArea.getRight(), point.x + searchRange);
    
    float minDist = std::numeric_limits<float>::max();
    
    juce::Point<float> prev(minX, getNormalCurveY(minX, controlArea));
    for (float x = minX + sampleStep; x <= maxX; x += sampleStep)
    {
        juce::Point<float> curr(x, getNormalCurveY(x, controlArea));
        
        float segLen = prev.getDistanceFrom(curr);
        if (segLen >= 1e-4f)
        {
            float t = juce::jlimit(0.0f, 1.0f,
                ((point.x - prev.x) * (curr.x - prev.x) +
                 (point.y - prev.y) * (curr.y - prev.y)) / (segLen * segLen));
            juce::Point<float> proj(prev.x + t * (curr.x - prev.x),
                                    prev.y + t * (curr.y - prev.y));
            float d = point.getDistanceFrom(proj);
            if (d < minDist) minDist = d;
        }
        else
        {
            float d = point.getDistanceFrom(prev);
            if (d < minDist) minDist = d;
        }
        prev = curr;
    }
    
    return minDist;
}

float PuponvstAudioProcessorEditor::getFilterAxisX(const juce::Rectangle<int>& controlArea) const
{
    return semitoneToX(juce::jlimit(kMinSemitone, kMaxSemitone, filterCenterSt), controlArea);
}

float PuponvstAudioProcessorEditor::getFilterParabolaY(float x, const juce::Rectangle<int>& controlArea) const
{
    const auto safeArea = getInteractionSafeArea(controlArea);
    const float axisX = getFilterAxisX(controlArea);
    const float axisY = (float)controlArea.getBottom() - 2.0f;
    const float apexY = (float)controlArea.getCentreY();

    const float h = juce::jmax(1.0f, axisY - apexY);
    const float stToPx = (float)safeArea.getWidth() / (float)(kMaxSemitone - kMinSemitone);
    const float halfWidthPx = juce::jmax(2.0f, 0.5f * (float)juce::jlimit(10, 72, filterWidthSt) * stToPx);
    const float t = (x - axisX) / halfWidthPx;
    return juce::jlimit((float)controlArea.getY(), axisY, apexY + h * t * t);
}

float PuponvstAudioProcessorEditor::distanceToFilterParabola(const juce::Point<float>& point,
                                                             const juce::Rectangle<int>& controlArea) const
{
    const auto safeArea = getInteractionSafeArea(controlArea);
    const float axisY = (float)controlArea.getBottom() - 2.0f;
    const float stToPx = (float)safeArea.getWidth() / (float)(kMaxSemitone - kMinSemitone);
    const float halfWidthPx = juce::jmax(2.0f, 0.5f * (float)juce::jlimit(10, 72, filterWidthSt) * stToPx);
    const float axisX = getFilterAxisX(controlArea);

    const float minX = juce::jmax((float)safeArea.getX(), axisX - halfWidthPx - 16.0f);
    const float maxX = juce::jmin((float)safeArea.getRight(), axisX + halfWidthPx + 16.0f);

    float minDist = std::numeric_limits<float>::max();
    juce::Point<float> prev(minX, getFilterParabolaY(minX, controlArea));

    for (float x = minX + 2.0f; x <= maxX; x += 2.0f)
    {
        juce::Point<float> curr(x, getFilterParabolaY(x, controlArea));
        const float segLen = prev.getDistanceFrom(curr);
        if (segLen > 1.0e-4f)
        {
            const float t = juce::jlimit(0.0f, 1.0f,
                ((point.x - prev.x) * (curr.x - prev.x)
               + (point.y - prev.y) * (curr.y - prev.y)) / (segLen * segLen));
            juce::Point<float> proj(prev.x + t * (curr.x - prev.x),
                                    prev.y + t * (curr.y - prev.y));
            minDist = juce::jmin(minDist, point.getDistanceFrom(proj));
        }
        prev = curr;
    }

    // 点击刻度线附近也允许开始拖宽度（更符合"拖到交点"手感）
    if (std::abs(point.y - axisY) <= 8.0f && point.x >= minX && point.x <= maxX)
        minDist = juce::jmin(minDist, std::abs(point.y - axisY));

    return minDist;
}

// 根据斜率 k（数学坐标系，Y 轴向上）计算射线与控制区域边界的交点（屏幕坐标）
// 射线从 bottomCenter 出发，方向必须向上（屏幕上 Y 减小的方向）
// isVertical = true 时忽略 k，返回正上方与上边界的交点
juce::Point<float> PuponvstAudioProcessorEditor::calculateRayEndBySlope(float k, bool isVertical,
                                                                        const juce::Rectangle<int>& controlArea) const
{
    const float originX = (float)controlArea.getCentreX();
    const float originY = (float)controlArea.getBottom(); // 屏幕坐标原点(Y向下)
    const float left    = (float)controlArea.getX();
    const float right   = (float)controlArea.getRight();
    const float top     = (float)controlArea.getY();
    
    // 1) 垂直向上
    if (isVertical)
        return juce::Point<float>(originX, top);
    
    // 2) 水平情况（k == 0 或接近 0）
    //    需要根据 k 的"方向意图"来决定射线朝向：
    //    - k 原本应该 > 0（红线斜率为正或蓝线斜率为负取反后）→ 指向右边界
    //    - k 原本应该 < 0（红线斜率为负或蓝线斜率为正取反后）→ 指向左边界
    //    由于浮点数 +0 == -0，无法通过 signbit 区分，因此：
    //    当 |k| 极小时，我们检查 k 是否"应该"为正或负——
    //    实际上，调用约定保证了：红线用 k，蓝线用 -k。
    //    当两线都水平时，我们希望红线指向左、蓝线指向右（对称）。
    //    但更直观的做法是：水平时，红线向左、蓝线向右（因为红左蓝右是默认状态）。
    //    然而这里无法知道调用者是谁，所以采用更简单的方法：
    //    当 k 接近 0 时，返回一个"中性"的终点——两个边界的中点底部，
    //    但这样两条线还是会重合...
    //
    //    最终方案：修改函数签名，增加 directionHint 参数？
    //    不，更简单的方案是：当 k 接近 0 时，根据 k 的"原始方向"返回不同值。
    //    由于无法区分 +0 和 -0，我们在调用处（paint 和 mouseDown 中）
    //    传入一个带有方向信息的极小非零值。
    //    
    //    但为了保持函数封装完整性，这里做一个简单处理：
    //    当 k 接近 0 时，如果 k > -1e-6f（即 k 为 +0 或很小的正数），返回右边界；
    //    如果 k < 1e-6f（即 k 为 -0 或很小的负数），返回左边界。
    //    这样 k == 0 时，两个判断都满足... 还是不行。
    //
    //    【最终正确方案】：当 k 的绝对值极小时，根据 k 的符号位来决定方向。
    //    利用 std::signbit 可以区分 +0.0 和 -0.0！
    if (std::abs(k) < 1e-6f)
    {
        // std::signbit 可以区分 +0.0 和 -0.0
        if (std::signbit(k))
            return juce::Point<float>(left, originY);  // k = -0 → 指向左边界（蓝线）
        else
            return juce::Point<float>(right, originY); // k = +0 → 指向右边界（红线）
    }
    
    // 数学坐标系下：射线从原点 (0,0) 沿方向 (dx, dy_up = k * dx) 出发
    // 要求 dy_up > 0 (向上) => 若 k > 0 则 dx > 0（右上方向），若 k < 0 则 dx < 0（左上方向）
    //
    // 屏幕坐标映射：screenX = originX + dx, screenY = originY - dy_up
    //
    // 与上边界相交（screenY = top）：dy_up = originY - top = H (控制区域高度)
    //   => dx_top = H / k，对应屏幕 X = originX + H / k
    // 与左边界相交（screenX = left）：dx = left - originX = -W/2
    //   => dy_up_left = k * (-W/2)，必须 > 0
    // 与右边界相交（screenX = right）：dx = right - originX = +W/2
    //   => dy_up_right = k * (+W/2)，必须 > 0
    
    const float H = originY - top;            // 控制区域高度
    const float halfW = (right - left) * 0.5f; // 控制区域半宽
    
    if (k > 0.0f)
    {
        // 指向右上：可能击中上边界或右边界，取先到达的那个
        // 参数化：沿 dx 方向递增，t_top = H / k (dx 达到该值时击中上边界), t_right = halfW
        float dxAtTop = H / k;
        if (dxAtTop <= halfW)
        {
            // 先击中上边界
            return juce::Point<float>(originX + dxAtTop, top);
        }
        else
        {
            // 先击中右边界
            float dyAtRight = k * halfW;
            return juce::Point<float>(right, originY - dyAtRight);
        }
    }
    else // k < 0
    {
        // 指向左上：dx 为负，|dx| 递增，t_top = H / (-k) = -H/k，t_left = halfW
        float dxAtTop = H / k;             // 负值
        float absDxAtTop = -dxAtTop;       // |dx|
        if (absDxAtTop <= halfW)
        {
            // 先击中上边界
            return juce::Point<float>(originX + dxAtTop, top);
        }
        else
        {
            // 先击中左边界
            float dyAtLeft = k * (-halfW); // = -k * halfW > 0
            return juce::Point<float>(left, originY - dyAtLeft);
        }
    }
}

juce::Point<float> PuponvstAudioProcessorEditor::calculateRayEndByAngle(float angleDeg,
                                                                        const juce::Rectangle<int>& controlArea) const
{
    // angleDeg: 0° = 水平向右，90° = 垂直向上，180° = 水平向左
    // 这是常规极坐标定义（与 atan2 返回值一致）
    // 计算射线与控制区域边界的交点
    
    const float originX = (float) controlArea.getCentreX();
    const float originY = (float) controlArea.getBottom();
    const float left = (float) controlArea.getX();
    const float right = (float) controlArea.getRight();
    const float top = (float) controlArea.getY();
    const float H = originY - top;            // 控制区域高度
    const float halfW = (right - left) * 0.5f; // 半宽
    
    // 将角度转换为弧度，并计算方向向量
    // 常规极坐标：0° = 向右（dirX=1, dirY=0），90° = 向上（dirX=0, dirY=1），180° = 向左（dirX=-1, dirY=0）
    float angleRad = angleDeg * (juce::MathConstants<float>::pi / 180.0f);
    
    // 方向向量（数学坐标系，Y向上）
    float dirX = std::cos(angleRad);  // 0° → +1(右), 90° → 0, 180° → -1(左)
    float dirY = std::sin(angleRad);  // 0° → 0, 90° → 1(上), 180° → 0
    
    // 射线参数方程：origin + t * (dirX, dirY), t >= 0
    // 需要找到最小的 t 使得射线击中边界
    // 注意：这里使用数学坐标系（Y向上），原点为(0,0)，上边界为 y=H
    
    float t_min = std::numeric_limits<float>::max();
    
    // 与上边界相交：t * dirY = H => t = H / dirY
    // 只有当 dirY > 0 时，射线才会向上击中上边界
    if (dirY > 0.0f)
    {
        float t_top = H / dirY;  // H = originY - top（数学坐标系中的高度）
        float hitX = originX + t_top * dirX;  // 屏幕坐标 X
        // 检查交点是否在左/右边界内
        if (hitX >= left && hitX <= right)
            t_min = juce::jmin(t_min, t_top);
    }
    
    // 与左边界相交：originX + t * dirX = left => t = (left - originX) / dirX
    if (dirX < 0.0f)  // 只有向左的方向才可能与左边界相交
    {
        float t_left = (left - originX) / dirX;
        if (t_left > 0)
        {
            // 对于水平射线（dirY=0），hitY = originY
            float hitY = originY - t_left * dirY;  // 转换为屏幕坐标（Y向下）
            if (hitY >= top && hitY <= originY)
                t_min = juce::jmin(t_min, t_left);
        }
    }
    
    // 与右边界相交：originX + t * dirX = right => t = (right - originX) / dirX
    if (dirX > 0.0f)  // 只有向右的方向才可能与右边界相交
    {
        float t_right = (right - originX) / dirX;
        if (t_right > 0)
        {
            // 对于水平射线（dirY=0），hitY = originY
            float hitY = originY - t_right * dirY;  // 转换为屏幕坐标（Y向下）
            if (hitY >= top && hitY <= originY)
                t_min = juce::jmin(t_min, t_right);
        }
    }
    
    // 计算终点
    if (t_min < std::numeric_limits<float>::max())
    {
        float endX = originX + t_min * dirX;
        float endY = originY - t_min * dirY;  // 转换回屏幕坐标（Y向下）
        return juce::Point<float>(endX, endY);
    }
    
    // 备用：不应该到达这里（处理水平射线等边界情况）
    // 水平射线：dirY = 0, dirX = ±1
    if (std::abs(dirY) < 1e-6f)
    {
        if (dirX < 0.0f)
            return juce::Point<float>(left, originY);   // 向左：击中左边界
        else
            return juce::Point<float>(right, originY);  // 向右：击中右边界
    }
    
    // 其他未知情况
    return juce::Point<float>(originX, top);
}

// 根据鼠标屏幕坐标计算相对 bottomCenter 的斜率（数学坐标系，Y 向上）
// 保护策略（严禁出现 k=∞ 的垂直射线）：
//   - dyUp 很小（鼠标被 clamp 到下边界或低于原点）→ k=0（水平射线）
//   - |dx| 很小（鼠标几乎在原点正上方）→ |k| 饱和到 kMaxAbs，保留方向符号
//   - 其他情况正常按 dyUp/dx 计算，并对结果做 [-kMaxAbs, kMaxAbs] 的饱和裁剪
// 始终返回 true；不再产生 isVertical 标志
bool PuponvstAudioProcessorEditor::computeSlopeFromMouse(const juce::Point<float>& mousePos,
                                                         const juce::Rectangle<int>& controlArea,
                                                         float& outK) const
{
    const float originX = (float)controlArea.getCentreX();
    const float originY = (float)controlArea.getBottom();
    
    const float dx = mousePos.x - originX;
    const float dyUp = originY - mousePos.y; // 数学坐标系：Y 向上
    
    // 斜率饱和上限：足够大使视觉上几乎垂直，但仍是有限值，避免渲染 / 交点计算出现除零
    constexpr float kMaxAbs = 1.0e4f;
    // 阈值：低于此值视为鼠标触及水平边界 → 射线回归水平
    constexpr float kDyUpHorizontalThreshold = 1.0f; // 像素
    // 阈值：低于此值视为鼠标正对原点上方 → 饱和斜率
    constexpr float kDxSaturateThreshold = 1.0e-3f; // 像素
    
    // 鼠标接近 / 低于下边界：水平射线
    if (dyUp <= kDyUpHorizontalThreshold)
    {
        // 保留左右符号以便后续判定方向：若鼠标在右侧，k=+0 方向右；反之 -0 方向左
        // 由于 k==0 在 calculateRayEndBySlope 中被当作水平处理，这里简单给 0 即可
        outK = 0.0f;
        return true;
    }
    
    // 鼠标几乎在原点正上方：饱和斜率（方向取 dx 的符号；dx 为 0 时默认为正）
    if (std::abs(dx) < kDxSaturateThreshold)
    {
        outK = (dx >= 0.0f) ? kMaxAbs : -kMaxAbs;
        return true;
    }
    
    float k = dyUp / dx;
    outK = juce::jlimit(-kMaxAbs, kMaxAbs, k);
    return true;
}

float PuponvstAudioProcessorEditor::semitoneToX(int semitone, const juce::Rectangle<int>& controlArea) const
{
    const auto safeArea = getInteractionSafeArea(controlArea);
    const int st = juce::jlimit(kMinSemitone, kMaxSemitone, semitone);
    const float norm = (float)(st - kMinSemitone) / (float)(kMaxSemitone - kMinSemitone);
    return (float) safeArea.getX() + norm * (float) safeArea.getWidth();
}

int PuponvstAudioProcessorEditor::xToSemitone(float x, const juce::Rectangle<int>& controlArea) const
{
    const auto safeArea = getInteractionSafeArea(controlArea);
    const float width = (float) safeArea.getWidth();
    if (width <= 1.0f)
        return 0;

    const float norm = juce::jlimit(0.0f, 1.0f, (x - (float) safeArea.getX()) / width);
    const float stF = (float)kMinSemitone + norm * (float)(kMaxSemitone - kMinSemitone);
    return juce::jlimit(kMinSemitone, kMaxSemitone, (int)std::lround(stF));
}

// 返回第 i (0..4) 根竖直线的屏幕 X 坐标
float PuponvstAudioProcessorEditor::getDotColumnX(int i, const juce::Rectangle<int>& controlArea) const
{
    i = juce::jlimit(0, 4, i);
    return semitoneToX(dotSemitoneOffsets[(size_t)i], controlArea);
}

// 返回第 i 根竖直线与正态曲线的交点 Y（= 圆点轨道的顶端，offsetT=1 时的位置）
float PuponvstAudioProcessorEditor::getDotTrackTopY(int i, const juce::Rectangle<int>& controlArea) const
{
    return getNormalCurveY(getDotColumnX(i, controlArea), controlArea);
}

// 返回第 i 个圆点的当前屏幕中心坐标：在 [底部, 轨道顶端] 之间按 dotOffsetT[i] 插值
juce::Point<float> PuponvstAudioProcessorEditor::getDotCenter(int i, const juce::Rectangle<int>& controlArea) const
{
    i = juce::jlimit(0, 4, i);
    const float xPos = getDotColumnX(i, controlArea);
    const float trackTopY = getDotTrackTopY(i, controlArea);
    const float bottomY = (float)controlArea.getBottom();
    const float t = juce::jlimit(0.0f, 1.0f, dotOffsetT[(size_t)i]);
    // t=1 → trackTopY（最高）；t=0 → bottomY（最低）
    const float y = bottomY + t * (trackTopY - bottomY);
    return { xPos, y };
}

// 声相区间映射：
//   - [45°, 135°]：panMin=0，panMax 在 90°->0、45°/135°->1
//   - (0°,45°) 和 (135°,180°)：panMax=1，panMin 从 0 线性增长到 1
// 最终每个 band 的 |pan| = panMin + (panMax - panMin) * |stNorm|。
// 方向规则保持不变：由 angle 两侧翻转 + st 正负决定。
// 特殊规则：st == 0 时始终返回 pan=0（保持居中，不受 panMin 影响）。
void PuponvstAudioProcessorEditor::getPanBoundsFromBlueAngle(float& outPanMin, float& outPanMax) const
{
    const float angleClamped = juce::jlimit(0.0f, 180.0f, blueAngleDeg);

    if (angleClamped >= 45.0f && angleClamped <= 135.0f)
    {
        const float dCenter = juce::jlimit(0.0f, 1.0f, std::abs(angleClamped - 90.0f) / 45.0f);
        outPanMin = 0.0f;
        outPanMax = juce::jlimit(0.0f, 1.0f, dCenter * (1.2f - 0.2f * dCenter));
        return;
    }

    const float dOuter = (angleClamped < 45.0f)
        ? juce::jlimit(0.0f, 1.0f, (45.0f - angleClamped) / 45.0f)
        : juce::jlimit(0.0f, 1.0f, (angleClamped - 135.0f) / 45.0f);
    outPanMin = dOuter;
    outPanMax = 1.0f;
}

float PuponvstAudioProcessorEditor::getPanFromBlueAngleAndSemitone(int semitone) const
{
    if (semitone == 0)
        return 0.0f;

    float panMin = 0.0f, panMax = 0.0f;
    getPanBoundsFromBlueAngle(panMin, panMax);

    const float angleClamped = juce::jlimit(0.0f, 180.0f, blueAngleDeg);
    const float directionFlipByAngle = (angleClamped <= 90.0f) ? 1.0f : -1.0f;

    const int st = juce::jlimit(kMinSemitone, kMaxSemitone, semitone);
    const float stNorm = juce::jlimit(-1.0f, 1.0f, (float) st / (float) kMaxSemitone);

    const float magnitude = juce::jlimit(0.0f, 1.0f,
        panMin + (panMax - panMin) * std::abs(stNorm));
    const float signedPan = directionFlipByAngle * ((stNorm >= 0.0f) ? magnitude : -magnitude);
    return juce::jlimit(-1.0f, 1.0f, signedPan);
}

// 同步 5 个圆点的 gain / pan 到音频处理引擎
// gain = dotOffsetT[i]（1.0 = 100%，与 UI 的高度百分比一致）
void PuponvstAudioProcessorEditor::pushDotParamsToProcessor()
{
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60);
    if (controlArea.getWidth() <= 0 || controlArea.getHeight() <= 0)
        return;
    
    const float bottom = (float)controlArea.getBottom();

    // Gain 基准：以中间圆点(i=2)在 offsetT=1 时的轨道顶端高度作为 100%
    // 即：中间圆点处于默认（最顶端）位置时，gain = 1.0
    //     其他圆点的实际屏幕高度 / 中间圆点默认屏幕高度 = 当前 gain
    // 这样拖动正态曲线时，各圆点的实际可见高度变化会直接反映到 gain 上
    const float centerTopY   = getDotTrackTopY(2, controlArea); // 中间圆点默认屏幕 Y（最高点）
    const float refHeightPx  = bottom - centerTopY;             // 100% 基准高度（像素）
    
for (int i = 0; i < 5; ++i)
    {
        const int st = juce::jlimit(kMinSemitone, kMaxSemitone, dotSemitoneOffsets[(size_t)i]);
        const float pan = getPanFromBlueAngleAndSemitone(st);

        // 圆点的当前屏幕中心 Y（受 dotOffsetT[i] 与当前 sigma 共同决定）
        const juce::Point<float> c = getDotCenter(i, controlArea);
        const float curHeightPx = bottom - c.y;
        const float gain = (refHeightPx > 1.0f)
            ? juce::jlimit(0.0f, 1.0f, curHeightPx / refHeightPx)
            : 0.0f;
        
        processor.setDotGain(i, gain);
        processor.setDotPan (i, pan);
        processor.setDotSemitoneOffset(i, dotSemitoneOffsets[(size_t)i]);
    }

    // ===== 把整套 UI 控制状态同步给 Processor（用于宿主存档/恢复） =====
    // 任何会触发本函数的交互（圆点 / 射线 / 正态曲线 / resize）都会顺带把状态镜像更新一份。
    //
    // 关键保护：构造函数完成前（initialised=false）跳过此步！
    // 因为 setSize() 会同步触发 resized() → 此函数 → 如果此时把默认值（redRayClockwiseDeg=90、
    // sigma=1、dotOffsetT=全1）镜像给 Processor，就会覆盖掉宿主刚刚通过
    // setStateInformation 恢复出的真存档（或上次 Editor 关闭时残留的最后状态）。
    if (initialised)
    {
        PuponvstAudioProcessor::EditorState s;
        s.blueAngleDeg        = blueAngleDeg;
        s.redRayClockwiseDeg  = redRayClockwiseDeg;
        s.isVerticalRay       = isVerticalRay;
        s.sigma               = sigma;
        s.dotOffsetT          = dotOffsetT;
        s.dotSemitoneOffsets  = dotSemitoneOffsets;
        s.hasValidValues      = true;
        processor.setEditorState(s);
    }
}

void PuponvstAudioProcessorEditor::resized()
{
    // 导航栏布局
    auto navBar = getLocalBounds().removeFromTop(60);
    auto navInner = navBar.reduced(30, 0);  // 左侧边距从50减少到30，使标题和版本号整体左移20像素

    // 大标题 "Pupon" 紧贴左边，副标题紧随其后（间距很小）
    // 使用当前字体动态测量 "Pupon" 实际宽度，避免硬编码导致的"间距过大"
    // 同时设置最小宽度兜底，防止字体未就绪时 getStringWidth 返回过小值导致大标题被压缩到看不见
    const auto titleText  = titleLabel.getText();
    const auto titleFont  = titleLabel.getFont();
    const int  measuredW  = titleFont.getStringWidth(titleText);
    const int  fallbackW  = (int) (titleFont.getHeight() * 0.6f * juce::jmax(1, titleText.length()));
    const int  titleWidth = juce::jmax(measuredW, fallbackW, 120); // 至少 120px
    constexpr int kTitleVersionGap = 2;   // 大标题与副标题之间的固定间距（像素）

    const int rightW = juce::jlimit(150, 240, navInner.getWidth() / 2);
    auto rightControls = navInner.removeFromRight(rightW);

    auto titleArea = navInner.removeFromLeft(titleWidth + kTitleVersionGap);
    const int versionTextW = versionLabel.getFont().getStringWidth(versionLabel.getText());
    const int versionW = juce::jlimit(120, 220, versionTextW + 16);
    auto versionArea = navInner.removeFromLeft(versionW).translated(-25, 0); // 版本号文本左移 25px

    titleLabel.setBounds(titleArea);
    versionLabel.setBounds(versionArea.translated(0, 7));

    // 预设组优先按抬头整体中心定位，而不是依赖剩余空间，避免默认尺寸下被挤向右侧。
    auto presetInner = navBar.reduced(30, 16);
    const int btnW = 24;
    const int btnGap = 6;
    const int comboH = 26;
    const int comboY = navBar.getY() + (navBar.getHeight() - comboH) / 2;
    const int versionVisualRight = versionArea.getX() + juce::jmin(versionTextW, versionArea.getWidth());
    const int leftLimit = juce::jmax(presetInner.getX(), versionVisualRight + 8);
    const int rightLimit = juce::jmin(presetInner.getRight(), rightControls.getX() - 8);
    const int fixedW = btnW * 2 + btnGap * 2;
    const int availableW = juce::jmax(fixedW + 90, rightLimit - leftLimit);
    const int presetComboW = juce::jlimit(90, 180, availableW - fixedW);
    const int allW = fixedW + presetComboW;

    int startX = navBar.getCentreX() - allW / 2;
    const int maxStartX = juce::jmax(leftLimit, rightLimit - allW);
    startX = juce::jlimit(leftLimit, maxStartX, startX);

    presetPrevButton.setBounds(startX, comboY, btnW, comboH);
    presetCombo.setBounds(startX + btnW + btnGap, comboY, presetComboW, comboH);
    presetNextButton.setBounds(startX + btnW + btnGap + presetComboW + btnGap, comboY, btnW, comboH);

    auto rightInner = rightControls.reduced(0, 16);
    const int comboGap = 6;
    const int rightComboW = juce::jmax(62, (rightInner.getWidth() - comboGap) / 2);
    auto qualityArea = rightInner.removeFromLeft(rightComboW);
    rightInner.removeFromLeft(comboGap);
    auto formantArea = rightInner.removeFromLeft(rightComboW);
    qualityCombo.setBounds(qualityArea.withHeight(26).withY(navBar.getY() + (navBar.getHeight() - 26) / 2));
    formantCombo.setBounds(formantArea.withHeight(26).withY(navBar.getY() + (navBar.getHeight() - 26) / 2));

    // 更新控制区域和下边界中心点（射线原点）
    auto controlArea = getLocalBounds();
    controlArea.removeFromTop(60);
    const auto safeArea = getInteractionSafeArea(controlArea);
    bottomCenter = juce::Point<float>((float) safeArea.getCentreX(), (float) safeArea.getBottom());

    // 基于角度模型下，窗口缩放时 blueAngleDeg（以及等价的 redRayClockwiseDeg）保持不变，
    // 射线视觉方向也自然保持不变，无需额外计算。
    
    // 窗口尺寸变化 → 控制区高度变 → 射线与竖直线交点的屏幕 y 变 → pan 有可能有极微的重新量化误差
    // 为保持 UI 与音频状态绝对一致，同步一次
    pushDotParamsToProcessor();
}

// ===== AudioProcessorValueTreeState::Listener 回调 =====
// 当宿主自动化参数时，更新 UI 成员并触发重绘
// 注意：此函数在宿主回调上下文中调用，不能直接调用 repaint()，否则可能死锁
void PuponvstAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // newValue 是归一化值 [0,1]，对应红线顺时针角度 [0°, 180°]
    if (parameterID == ParameterIDs::redRayClockwiseAngle)
    {
        // 蓝线角度：0° = 向右，90° = 向上，180° = 向左
        blueAngleDeg = newValue * 180.0f;
        redRayClockwiseDeg = blueAngleDeg;

        needsRepaint = true;
    }
    else if (parameterID == ParameterIDs::sigma)
    {
        // 重要：对于 AudioParameterFloat，parameterChanged 的 newValue 
        // 是去归一化后的实际值，即 [0.24, 8.0] 范围内的值
        float actualSigma = newValue;
        
        sigma = juce::jlimit(0.24f, 8.0f, actualSigma);
        needsRepaint = true;
    }
    else if (parameterID == ParameterIDs::filterCenterSt)
    {
        filterCenterSt = juce::jlimit(kMinSemitone, kMaxSemitone, (int) std::lround(newValue));
        needsRepaint = true;
    }
    else if (parameterID == ParameterIDs::filterWidthSt)
    {
        filterWidthSt = juce::jlimit(10, 72, (int) std::lround(newValue));
        needsRepaint = true;
    }
    else if (parameterID == ParameterIDs::dot0
          || parameterID == ParameterIDs::dot1
          || parameterID == ParameterIDs::dot2
          || parameterID == ParameterIDs::dot3
          || parameterID == ParameterIDs::dot4)
    {
        int dotIndex = parameterID.substring(3).getIntValue();
        if (dotIndex >= 0 && dotIndex < 5)
        {
            dotOffsetT[dotIndex] = juce::jlimit(0.0f, 1.0f, newValue);
            needsRepaint = true;
        }
    }
    else if (parameterID == ParameterIDs::dot0Semitone
          || parameterID == ParameterIDs::dot1Semitone
          || parameterID == ParameterIDs::dot2Semitone
          || parameterID == ParameterIDs::dot3Semitone
          || parameterID == ParameterIDs::dot4Semitone)
    {
        int dotIndex = -1;
        if (parameterID == ParameterIDs::dot0Semitone) dotIndex = 0;
        else if (parameterID == ParameterIDs::dot1Semitone) dotIndex = 1;
        else if (parameterID == ParameterIDs::dot2Semitone) dotIndex = 2;
        else if (parameterID == ParameterIDs::dot3Semitone) dotIndex = 3;
        else if (parameterID == ParameterIDs::dot4Semitone) dotIndex = 4;

        if (dotIndex >= 0)
        {
            dotSemitoneOffsets[(size_t) dotIndex] = juce::jlimit(kMinSemitone, kMaxSemitone, (int) std::lround(newValue));
            needsRepaint = true;
        }
    }

    // 异步触发 UI 更新，避免在宿主回调上下文中直接调用 repaint() 导致死锁
    if (needsRepaint)
    {
        if (!suppressPresetDirtyMark && currentPresetIndex >= 0)
        {
            currentPresetDirty = true;
            updatePresetComboDisplayText();
        }
        pushDotParamsToProcessor();
        triggerAsyncUpdate();
    }
}

// ===== AsyncUpdater 回调 =====
// 在主消息线程中异步执行，安全调用 repaint()
void PuponvstAudioProcessorEditor::handleAsyncUpdate()
{
    if (needsRepaint.load())
    {
        needsRepaint = false;
        repaint();
    }
}

void PuponvstAudioProcessorEditor::refreshPresetList()
{
    if (!presetFolder.exists())
        presetFolder.createDirectory();

    presetFiles.clear();
    presetFolder.findChildFiles(presetFiles, juce::File::findFiles, false, "*.puponpreset");
    rebuildPresetComboItems();
}

void PuponvstAudioProcessorEditor::rebuildPresetComboItems()
{
    isUpdatingPresetCombo = true;
    presetCombo.clear(juce::dontSendNotification);

    presetCombo.addItem("Save Preset", kPresetActionSaveId);
    presetCombo.addItem("Change Default Folder", kPresetActionChangeFolderId);
    presetCombo.addSeparator();

    for (int i = 0; i < presetFiles.size(); ++i)
    {
        const auto name = presetFiles.getReference(i).getFileNameWithoutExtension();
        presetCombo.addItem(name, kPresetItemBaseId + i);
    }

    if (presetFiles.isEmpty())
    {
        constexpr int kNoPresetId = 999;
        presetCombo.addItem("(No presets in folder)", kNoPresetId);
        presetCombo.setItemEnabled(kNoPresetId, false);
    }

    presetCombo.setSelectedId(0, juce::dontSendNotification);
    presetCombo.setTooltip("Preset folder: " + presetFolder.getFullPathName());
    presetPrevButton.setEnabled(!presetFiles.isEmpty());
    presetNextButton.setEnabled(!presetFiles.isEmpty());

    if (presetFiles.isEmpty())
        currentPresetIndex = -1;
    else
        currentPresetIndex = juce::jlimit(0, presetFiles.size() - 1, currentPresetIndex < 0 ? 0 : currentPresetIndex);

    updatePresetComboDisplayText();
    isUpdatingPresetCombo = false;
}

void PuponvstAudioProcessorEditor::updatePresetComboDisplayText()
{
    juce::String text = currentPresetName;
    if (currentPresetDirty && currentPresetIndex >= 0)
        text << " (change)";

    if (text.isEmpty())
        text = "Presets";

    presetCombo.setTextWhenNothingSelected(text);
    presetCombo.setSelectedId(0, juce::dontSendNotification);
}

void PuponvstAudioProcessorEditor::handlePresetComboChange()
{
    if (isUpdatingPresetCombo)
        return;

    const int selectedId = presetCombo.getSelectedId();
    if (selectedId == kPresetActionSaveId)
    {
        savePresetToFile();
    }
    else if (selectedId == kPresetActionChangeFolderId)
    {
        choosePresetFolder();
    }
    else if (selectedId >= kPresetItemBaseId)
    {
        const int index = selectedId - kPresetItemBaseId;
        if (juce::isPositiveAndBelow(index, presetFiles.size()))
        {
            currentPresetIndex = index;
            loadPresetFromFile(presetFiles.getReference(index));
        }
    }

    presetCombo.setSelectedId(0, juce::dontSendNotification);
}

void PuponvstAudioProcessorEditor::switchPresetByStep(int step)
{
    if (presetFiles.isEmpty())
        return;

    if (currentPresetIndex < 0)
        currentPresetIndex = 0;

    const int count = presetFiles.size();
    currentPresetIndex = (currentPresetIndex + step + count) % count;
    loadPresetFromFile(presetFiles.getReference(currentPresetIndex));
}

void PuponvstAudioProcessorEditor::savePresetToFile()
{
    presetFileChooser = std::make_unique<juce::FileChooser>(
        "Save Pupon preset",
        presetFolder,
        "*.puponpreset");

    const int flags = juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;

    presetFileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        if (!file.hasFileExtension("puponpreset"))
            file = file.withFileExtension(".puponpreset");

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        if (!file.replaceWithData(state.getData(), state.getSize()))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Save preset failed",
                                                   "Could not write preset file.");
            return;
        }

        currentPresetName = file.getFileNameWithoutExtension();
        currentPresetDirty = false;
        refreshPresetList();
        currentPresetIndex = presetFiles.indexOf(file);
        updatePresetComboDisplayText();
    });
}

void PuponvstAudioProcessorEditor::choosePresetFolder()
{
    presetFileChooser = std::make_unique<juce::FileChooser>(
        "Choose default preset folder",
        presetFolder,
        "*");

    const int flags = juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectDirectories;

    presetFileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File() || !file.isDirectory())
            return;

        presetFolder = file;
        currentPresetIndex = -1;
        currentPresetDirty = false;
        currentPresetName = "Presets";
        refreshPresetList();
    });
}

void PuponvstAudioProcessorEditor::loadPresetFromFile(const juce::File& file)
{
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Load preset failed",
                                               "Preset file is empty or unreadable.");
        return;
    }

    suppressPresetDirtyMark = true;
    processor.setStateInformation(data.getData(), (int)data.getSize());
    suppressPresetDirtyMark = false;

    // Pull non-APVTS editor-state fields to ensure immediate visual sync.
    const auto restored = processor.getEditorState();
    if (restored.hasValidValues)
    {
        blueAngleDeg = restored.blueAngleDeg;
        redRayClockwiseDeg = restored.redRayClockwiseDeg;
        isVerticalRay = restored.isVerticalRay;
        sigma = juce::jlimit(0.24f, 8.0f, restored.sigma);
        dotOffsetT = restored.dotOffsetT;
        dotSemitoneOffsets = restored.dotSemitoneOffsets;
    }

    for (int i = 0; i < 5; ++i)
        processor.setDotSemitoneOffset(i, dotSemitoneOffsets[(size_t)i]);

    currentPresetName = file.getFileNameWithoutExtension();
    currentPresetDirty = false;
    if (currentPresetIndex < 0)
        currentPresetIndex = presetFiles.indexOf(file);
    updatePresetComboDisplayText();

    pushDotParamsToProcessor();
    repaint();
}
