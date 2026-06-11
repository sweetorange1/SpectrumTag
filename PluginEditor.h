#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "PluginProcessor.h"

class PuponvstAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater
{
public:
    PuponvstAudioProcessorEditor(PuponvstAudioProcessor&);
    ~PuponvstAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    class HeaderComboBox : public juce::ComboBox
    {
    public:
        using juce::ComboBox::ComboBox;

        void mouseDown(const juce::MouseEvent& event) override
        {
            // Match outside-click behavior: if a popup is already open, second click closes it.
            if (juce::PopupMenu::dismissAllActiveMenus())
                return;

            juce::ComboBox::mouseDown(event);
        }
    };

    class HeaderComboLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawComboBox(juce::Graphics& g,
                          int width,
                          int height,
                          bool isButtonDown,
                          int buttonX,
                          int buttonY,
                          int buttonW,
                          int buttonH,
                          juce::ComboBox& box) override;

        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

        void drawComboBoxTextWhenNothingSelected(juce::Graphics& g,
                                                 juce::ComboBox& box,
                                                 juce::Label& label) override;

        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override
        {
            juce::ignoreUnused(label);
            popupMenuWidth = box.getWidth();
            const auto screenBounds = box.getScreenBounds();
            const auto belowBox = juce::Rectangle<int>(screenBounds.getX(), screenBounds.getBottom(), screenBounds.getWidth(), 1);

            return juce::PopupMenu::Options()
                .withTargetScreenArea(belowBox)
                .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
                .withMinimumWidth(box.getWidth())
                .withMaximumNumColumns(1)
                .withStandardItemHeight(22);
        }

        void getIdealPopupMenuItemSize(const juce::String& text,
                                       const bool isSeparator,
                                       const int standardMenuItemHeight,
                                       int& idealWidth,
                                       int& idealHeight) override
        {
            juce::LookAndFeel_V4::getIdealPopupMenuItemSize(text, isSeparator, standardMenuItemHeight,
                                                            idealWidth, idealHeight);
            if (!isSeparator && popupMenuWidth > 0)
                idealWidth = popupMenuWidth;
        }

    private:
        int popupMenuWidth = 0;
    };

    class PresetArrowButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;

        void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
        {
            auto r = getLocalBounds().toFloat();
            const float scale = isButtonDown ? 0.92f : 1.0f;
            auto iconBounds = r.withSizeKeepingCentre(r.getWidth() * scale, r.getHeight() * scale);

            if (isMouseOverButton)
            {
                juce::ColourGradient glow(juce::Colour(0x55BFD4FF), iconBounds.getCentreX(), iconBounds.getCentreY(),
                                          juce::Colour(0x00BFD4FF), iconBounds.getRight(), iconBounds.getCentreY(), true);
                g.setGradientFill(glow);
                g.fillEllipse(iconBounds.reduced(2.0f));
            }

            g.setColour(findColour(isButtonDown ? juce::TextButton::textColourOnId
                                                : juce::TextButton::textColourOffId));
            g.setFont(juce::Font(13.5f, juce::Font::bold));
            g.drawFittedText(getButtonText(), iconBounds.toNearestInt(), juce::Justification::centred, 1);
        }
    };

    // ===== 动效驱动 =====
    // Timer 回调：每帧从 processor 拉取一小段样本，更新音频电平；推进时间相位；然后 repaint()
    void timerCallback() override;
    
    // 平滑后的音频电平 (RMS 近似) ∈ [0, 1]，用于驱动呼吸式动效
    float audioLevel = 0.0f;
    // 短时 peak，用于驱动更瞬时的冲击（例如激光能量光斑扩张）
    float audioPeak = 0.0f;
    // 时间相位（秒），单调累加，用于激光游走光斑、光球扫光等与时间相关的动画
    float animPhase = 0.0f;
    PuponvstAudioProcessor& processor;
    // GUI组件
    juce::Label titleLabel;       // 大标题 "Pupon"，字号 32
    juce::Label versionLabel;     // 副标题 "v1.0.4"，小号普通无衬线字体
    bool isTitleHovered = false;
    PresetArrowButton presetPrevButton { "<" };
    HeaderComboBox presetCombo;
    PresetArrowButton presetNextButton { ">" };
    HeaderComboBox qualityCombo;
    HeaderComboBox formantCombo;
    HeaderComboLookAndFeel headerComboLookAndFeel;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> qualityAttachment;
    std::unique_ptr<ComboAttachment> formantAttachment;
    std::unique_ptr<juce::FileChooser> presetFileChooser;
    juce::File presetFolder;
    juce::Array<juce::File> presetFiles;
    int currentPresetIndex = -1;
    juce::String currentPresetName { "Presets" };
    bool currentPresetDirty = false;
    bool suppressPresetDirtyMark = false;
    bool isUpdatingPresetCombo = false;

    static constexpr int kPresetActionSaveId = 1;
    static constexpr int kPresetActionChangeFolderId = 2;
    static constexpr int kPresetItemBaseId = 1000;

    // 自定义字体（AtomicMarker.otf，二进制资源加载）
    juce::Typeface::Ptr atomicMarkerTypeface;
    
    // 窗口等比缩放约束器
    juce::ComponentBoundsConstrainer resizeConstrainer;
    
    // 正态分布参数
    float sigma = 2.70f;

    // ===== 射线斜率模型 =====
    // 坐标系定义：以控制区域下边缘中点(bottomCenter)为原点，X轴水平向右，Y轴向上（数学坐标系）
    // 蓝线角度：0° = 水平向右，90° = 垂直向上，180° = 水平向左
    // 红线角度 = 180° - 蓝线角度（左右对称）
    // 红线顺时针角度（以水平向左为 0°）与 blueAngleDeg 数值等价
    float blueAngleDeg = 90.0f;  // 蓝线角度（度），默认垂直向上
    float redRayClockwiseDeg = 90.0f;
    bool  isVerticalRay = false;  // 是否为垂直射线（k为无穷大时两线重合向正上方）
    
    // 射线交互状态
    bool isDraggingRedLine = false;
    bool isDraggingBlueLine = false;
    bool isDraggingNormalCurve = false;
    bool isDraggingFilterAxis = false;
    bool isDraggingFilterParabola = false;

    // 滤波器控制器（单位 st）：
    // centerSt 控制抛物线对称轴相对 0st 的偏移，widthSt 控制底部交点间距
    int filterCenterSt = 0;
    int filterWidthSt  = 72;

    // ===== 5个圆点的拖动状态 =====
    // 圆点按从左到右索引 0..4；分组：组L = {0,1}，组C = {2}，组R = {3,4}
    // offsetT[i] 表示圆点在"轨道"上的位置：1.0 = 默认位置（竖直线与正态曲线的交点，最高），
    //                                     0.0 = 控制区域底部（最低）
    std::array<float, 5> dotOffsetT { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    // 每个珍珠竖线对应的半音偏移（单位：st，始终为整数）
    std::array<int, 5> dotSemitoneOffsets { -24, -12, 0, +12, +24 };
    int draggingDotIndex = -1; // -1 表示没有拖动圆点；否则为 0..4
    int draggingDotColumnIndex = -1; // -1 表示没有拖动竖线X；否则为 0..4

    // 控制区域下边缘中点（射线发射原点，屏幕坐标）
    juce::Point<float> bottomCenter;
    
    // 辅助函数
    bool isPointNearLine(const juce::Point<float>& point, 
                       const juce::Point<float>& lineStart, 
                       const juce::Point<float>& lineEnd, 
                       float threshold);

    // 计算正态曲线在指定x位置的Y坐标
    float getNormalCurveY(float x, const juce::Rectangle<int>& controlArea) const;
    
    // 计算点到正态曲线的最短距离
    float distanceToNormalCurve(const juce::Point<float>& point, 
                                 const juce::Rectangle<int>& controlArea) const;

    // 将控制区域转换为交互安全区，避免拖动命中窗口可缩放边缘。
    juce::Rectangle<int> getInteractionSafeArea(const juce::Rectangle<int>& controlArea) const;

    // 过滤器抛物线几何辅助
    float getFilterAxisX(const juce::Rectangle<int>& controlArea) const;
    float getFilterParabolaY(float x, const juce::Rectangle<int>& controlArea) const;
    float distanceToFilterParabola(const juce::Point<float>& point,
                                   const juce::Rectangle<int>& controlArea) const;

    // 根据斜率 k (数学坐标系, Y轴向上)，从原点(controlArea下边缘中点)出发计算射线与控制区域边界的交点(屏幕坐标)
    // isVertical = true 时表示垂直向上射线
    juce::Point<float> calculateRayEndBySlope(float k, bool isVertical,
                                               const juce::Rectangle<int>& controlArea) const;
    
    // 根据角度计算射线终点（角度版本，更直观）
    // angleDeg: 0° = 水平向右，90° = 垂直向上，180° = 水平向左
    // 返回射线与控制区域边界的交点(屏幕坐标)
    juce::Point<float> calculateRayEndByAngle(float angleDeg,
                                               const juce::Rectangle<int>& controlArea) const;
    
    // 根据鼠标屏幕坐标计算相对于原点的斜率（数学坐标系，Y轴向上）
    // 保护策略：dyUp 过小（鼠标贴下边）→ k=0（水平）；|dx| 过小（鼠标几乎正上方）→ 斜率饱和到一个大的有限值
    // 始终返回 true；不会再产生垂直射线状态
    bool computeSlopeFromMouse(const juce::Point<float>& mousePos,
                                const juce::Rectangle<int>& controlArea,
                                float& outK) const;

    // ===== 圆点几何辅助 =====
    // 半音偏移范围映射（下方面板最左 -36st，最右 +36st）
    static constexpr int kMinSemitone = -36;
    static constexpr int kMaxSemitone = +36;
    float semitoneToX(int semitone, const juce::Rectangle<int>& controlArea) const;
    int xToSemitone(float x, const juce::Rectangle<int>& controlArea) const;

    // 返回第 i (0..4) 根竖直线的屏幕 X
    float getDotColumnX(int i, const juce::Rectangle<int>& controlArea) const;
    // 返回第 i 根竖直线与正态曲线的交点 Y（即圆点轨道的顶端，t=1 时的位置）
    float getDotTrackTopY(int i, const juce::Rectangle<int>& controlArea) const;
    // 返回第 i 个圆点在当前 offsetT 下的屏幕中心坐标
    juce::Point<float> getDotCenter(int i, const juce::Rectangle<int>& controlArea) const;

    // 声相映射辅助：根据 blueAngleDeg 计算当前 pan 区间 [panMin, panMax]
    // 以及把 semitone 映射为最终 pan（与 push/paint 共用，避免重复逻辑）。
    void getPanBoundsFromBlueAngle(float& outPanMin, float& outPanMax) const;
    float getPanFromBlueAngleAndSemitone(int semitone) const;

    // 返回标题文本在编辑器坐标中的实际可点击区域（按字体宽高裁切）。
    juce::Rectangle<int> getTitleTextBounds() const;

    // ===== 参数同步：把 5 个圆点的 gain / pan 推送给 Processor（音频线程会读取）=====
    // 调用时机：初始化、拖动圆点 / 射线 / 正态曲线之后、窗口 resize 之后
    void pushDotParamsToProcessor();

    void refreshPresetList();
    void rebuildPresetComboItems();
    void handlePresetComboChange();
    void updatePresetComboDisplayText();
    void switchPresetByStep(int step);
    void savePresetToFile();
    void choosePresetFolder();
    void loadPresetFromFile(const juce::File& file);

    // AudioProcessorValueTreeState::Listener 回调 - 响应参数自动化
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // AsyncUpdater 回调 - 异步触发 UI 更新，避免参数回调中的死锁
    void handleAsyncUpdate() override;

    // 构造函数初始化标志：在构造完成前为 false，期间 setSize() 触发的早期 resized()
    // 不应该把"还未恢复完毕"的成员值（默认 0/1 等）镜像到 Processor 的 editorState，
    // 否则会覆盖宿主 setStateInformation 恢复出的真存档，导致参数无法保存。
    bool initialised = false;

    // 标记是否有待处理的参数变化需要重绘
    std::atomic<bool> needsRepaint{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PuponvstAudioProcessorEditor)
};