#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

PuponvstAudioProcessor::PuponvstAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // 添加参数变化监听
    apvts.addParameterListener(ParameterIDs::redRayClockwiseAngle, this);
    apvts.addParameterListener(ParameterIDs::sigma, this);
    apvts.addParameterListener(ParameterIDs::filterCenterSt, this);
    apvts.addParameterListener(ParameterIDs::filterWidthSt, this);
    apvts.addParameterListener(ParameterIDs::rbPitchQuality, this);
    apvts.addParameterListener(ParameterIDs::rbFormantMode, this);
    apvts.addParameterListener(ParameterIDs::dot0, this);
    apvts.addParameterListener(ParameterIDs::dot1, this);
    apvts.addParameterListener(ParameterIDs::dot2, this);
    apvts.addParameterListener(ParameterIDs::dot3, this);
    apvts.addParameterListener(ParameterIDs::dot4, this);
    apvts.addParameterListener(ParameterIDs::dot0Semitone, this);
    apvts.addParameterListener(ParameterIDs::dot1Semitone, this);
    apvts.addParameterListener(ParameterIDs::dot2Semitone, this);
    apvts.addParameterListener(ParameterIDs::dot3Semitone, this);
    apvts.addParameterListener(ParameterIDs::dot4Semitone, this);
}

PuponvstAudioProcessor::~PuponvstAudioProcessor() 
{
    // 调试信息：析构函数开始执行
    DBG("PuponvstAudioProcessor destructor called");
    
    // 清理资源 - 不需要获取锁，因为对象即将被销毁
    oscilloscopeWritePos = 0;
    std::fill(oscilloscopeBuffer.begin(), oscilloscopeBuffer.end(), 0.0f);
    
    DBG("PuponvstAudioProcessor destructor completed successfully");
}

bool PuponvstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    // 必须有输出
    if (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled())
        return false;

#if ! JucePlugin_IsSynth
    // 作为效果器：输入/输出声道数必须一致（支持 mono/stereo）
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
#endif
}

void PuponvstAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType sl(oscilloscopeLock);
    std::fill(oscilloscopeBuffer.begin(), oscilloscopeBuffer.end(), 0.0f);
    oscilloscopeWritePos = 0;

    // 初始化音频处理引擎
    const int numCh = juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels());
    preparedSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    pitchEngine.prepare(sampleRate, samplesPerBlock, numCh);

    // 状态复位
    lastBarSeconds = 2.0;

    // 把 Rubber Band LiveShifter 的启动延迟上报给宿主 DAW，便于延迟补偿
    setLatencySamples(pitchEngine.getLatencySamples());

    // 同步滤波器参数到引擎（避免某些宿主在恢复状态后未触发回调时出现不同步）
    if (auto* centerParam = apvts.getRawParameterValue(ParameterIDs::filterCenterSt))
        pitchEngine.setFilterCenterOffsetSemitones((int) std::lround(centerParam->load()));
    if (auto* widthParam = apvts.getRawParameterValue(ParameterIDs::filterWidthSt))
        pitchEngine.setFilterWidthSemitones((int) std::lround(widthParam->load()));
    if (auto* qualityParam = apvts.getRawParameterValue(ParameterIDs::rbPitchQuality))
        pitchEngine.setPitchQualityMode((int) std::lround(qualityParam->load()));
    if (auto* formantParam = apvts.getRawParameterValue(ParameterIDs::rbFormantMode))
        pitchEngine.setFormantMode((int) std::lround(formantParam->load()));
    if (auto* st0 = apvts.getRawParameterValue(ParameterIDs::dot0Semitone))
        pitchEngine.setDotSemitoneOffset(0, (int) std::lround(st0->load()));
    if (auto* st1 = apvts.getRawParameterValue(ParameterIDs::dot1Semitone))
        pitchEngine.setDotSemitoneOffset(1, (int) std::lround(st1->load()));
    if (auto* st2 = apvts.getRawParameterValue(ParameterIDs::dot2Semitone))
        pitchEngine.setDotSemitoneOffset(2, (int) std::lround(st2->load()));
    if (auto* st3 = apvts.getRawParameterValue(ParameterIDs::dot3Semitone))
        pitchEngine.setDotSemitoneOffset(3, (int) std::lround(st3->load()));
    if (auto* st4 = apvts.getRawParameterValue(ParameterIDs::dot4Semitone))
        pitchEngine.setDotSemitoneOffset(4, (int) std::lround(st4->load()));
}

void PuponvstAudioProcessor::releaseResources()
{
    pitchEngine.reset();
}

void PuponvstAudioProcessor::pushSamplesToOscilloscope(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    const juce::SpinLock::ScopedLockType sl(oscilloscopeLock);

    for (int i = 0; i < numSamples; ++i)
    {
        oscilloscopeBuffer[(size_t) oscilloscopeWritePos] = samples[i];
        oscilloscopeWritePos = (oscilloscopeWritePos + 1) % oscilloscopeBufferSize;
    }
}

void PuponvstAudioProcessor::getOscilloscopeSnapshot(juce::Array<float>& dest)
{
    dest.resize(oscilloscopeBufferSize);

    const juce::SpinLock::ScopedLockType sl(oscilloscopeLock);

    // 以 writePos 作为“最新数据之后的位置”，从旧到新拷贝
    for (int i = 0; i < oscilloscopeBufferSize; ++i)
    {
        const int idx = (oscilloscopeWritePos + i) % oscilloscopeBufferSize;
        dest.set(i, oscilloscopeBuffer[(size_t) idx]);
    }
}

void PuponvstAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // 质量/共振峰模式变化后，在块边界重建 shifter（并更新延迟上报）
    if (pitchEngineOptionsDirty.exchange(false, std::memory_order_acq_rel))
    {
        const int numCh = juce::jmax((int) totalNumInputChannels, (int) totalNumOutputChannels);
        pitchEngine.prepare(preparedSampleRate, preparedBlockSize, numCh);
        setLatencySamples(pitchEngine.getLatencySamples());
    }

    // 清理多余输出通道（例如输入是mono而输出是stereo等情况）
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    juce::ignoreUnused(numSamples);

    // ===== 从宿主 PlayHead 获取 BPM 和拍号信息 =====
    {
        double barSeconds = lastBarSeconds;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                const double bpm = pos->getBpm().orFallback(120.0);
                const auto   ts  = pos->getTimeSignature().orFallback(
                                       juce::AudioPlayHead::TimeSignature { 4, 4 });
                const int    num = juce::jmax(1, ts.numerator);
                const int    den = juce::jmax(1, ts.denominator);
                // 一拍秒数 = 60/bpm（按 4 分音符为一拍）；一小节 = num * (60/bpm) * (4/den)
                // 这样 6/8、3/4 等拍号也能正确处理
                barSeconds = (60.0 / juce::jmax(1.0, bpm))
                           * (double) num
                           * (4.0 / (double) den);
                lastBarSeconds = barSeconds;
            }
        }
    }

    // ===== 核心音频处理：5 路重采样 pitch shift + 混音 + 声相 + 硬削波 =====
    pitchEngine.process(buffer);

    // 示例波形：抓取主输出的第0通道（已是处理后的信号）
    if (totalNumOutputChannels > 0)
        pushSamplesToOscilloscope(buffer.getReadPointer(0), buffer.getNumSamples());
}

juce::AudioProcessorEditor* PuponvstAudioProcessor::createEditor() { return new PuponvstAudioProcessorEditor(*this); }
bool PuponvstAudioProcessor::hasEditor() const { return true; }

const juce::String PuponvstAudioProcessor::getName() const { return "Puponvst"; }
bool PuponvstAudioProcessor::acceptsMidi() const { return false; }
bool PuponvstAudioProcessor::producesMidi() const { return false; }
bool PuponvstAudioProcessor::isMidiEffect() const { return false; }
double PuponvstAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int PuponvstAudioProcessor::getNumPrograms() { return 1; }
int PuponvstAudioProcessor::getCurrentProgram() { return 0; }
void PuponvstAudioProcessor::setCurrentProgram(int) {}
const juce::String PuponvstAudioProcessor::getProgramName(int) { return {}; }
void PuponvstAudioProcessor::changeProgramName(int, const juce::String&) {}

void PuponvstAudioProcessor::setEditorState(const EditorState& s)
{
    const juce::SpinLock::ScopedLockType sl(editorStateLock);
    editorState = s;
    editorState.hasValidValues = true;
}

PuponvstAudioProcessor::EditorState PuponvstAudioProcessor::getEditorState() const
{
    const juce::SpinLock::ScopedLockType sl(editorStateLock);
    return editorState;
}

// ===== 状态序列化（保存到宿主工程）=====
// 用 ValueTree + XML 文本序列化，向后兼容（未来加字段时旧版工程可读）。
// 内容包含：5 圆点高度、红蓝射线角度、正态曲线方差、黄色激光归一化值。
void PuponvstAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // 保存 APVTS 状态 + EditorState
    juce::ValueTree tree("PuponState");
    tree.setProperty("version", 1, nullptr);
    
    // 保存 APVTS 参数值 (copyState() 返回的就是 ValueTree)
    juce::ValueTree paramsTree = apvts.copyState();
    tree.appendChild(paramsTree, nullptr);
    
    // 保存 EditorState（用于非参数状态）
    EditorState s = getEditorState();
    tree.setProperty("blueAngleDeg_extra", s.blueAngleDeg, nullptr);
    tree.setProperty("redRayClockwiseDeg_extra", s.redRayClockwiseDeg, nullptr);
    tree.setProperty("sigma_extra", s.sigma, nullptr);
    for (int i = 0; i < 5; ++i)
    {
        tree.setProperty("dot" + juce::String(i) + "_extra", s.dotOffsetT[(size_t) i], nullptr);
        tree.setProperty("dot" + juce::String(i) + "_st_extra", s.dotSemitoneOffsets[(size_t) i], nullptr);
    }

    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void PuponvstAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (!xml->hasTagName("PuponState"))
            return;

        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid())
            return;

        // 恢复 APVTS 参数状态
        if (tree.getNumChildren() > 0)
        {
            auto paramsTree = tree.getChild(0);
            if (paramsTree.hasType("PARAMETERS"))
            {
                apvts.replaceState(paramsTree);
            }
        }

        // 恢复 EditorState
        EditorState s;
        s.blueAngleDeg  = (float) tree.getProperty("blueAngleDeg_extra", s.blueAngleDeg);
        s.redRayClockwiseDeg = (float) tree.getProperty("redRayClockwiseDeg_extra", s.blueAngleDeg);
        s.sigma         = (float) tree.getProperty("sigma_extra", s.sigma);
        for (int i = 0; i < 5; ++i)
        {
            s.dotOffsetT[(size_t) i] = (float) tree.getProperty("dot" + juce::String(i) + "_extra",
                                                                 s.dotOffsetT[(size_t) i]);
            s.dotSemitoneOffsets[(size_t) i] = juce::jlimit(-36, +36,
                (int) tree.getProperty("dot" + juce::String(i) + "_st_extra", s.dotSemitoneOffsets[(size_t) i]));
        }
        s.hasValidValues = true;

        {
            const juce::SpinLock::ScopedLockType sl(editorStateLock);
            editorState = s;
        }

        // 兼容参数重命名：无论旧工程是否包含新 APVTS ID，都以恢复出的角度回写当前参数。
        if (auto* angleParam = apvts.getParameter(ParameterIDs::redRayClockwiseAngle))
            angleParam->setValue(juce::jlimit(0.0f, 1.0f, s.redRayClockwiseDeg / 180.0f));
    }
}

// 插件入口实现
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PuponvstAudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout PuponvstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // 红线顺时针角度（红蓝激光控制器）- 归一化范围 [0, 1]，默认 0.25 (45°)
    // 角度定义：以红线水平向左为 0°，顺时针旋转到水平向右为 180°
    //
    // 使用角度映射（连续变化）：
    //   归一化值 n ∈ [0,1] → redRayClockwiseDeg = n * 180°（0°=向左，90°=向上，180°=向右）
    //   与蓝线角度关系：blueAngleDeg = redRayClockwiseDeg（数值等价）
    //   数学角度（从 +X 逆时针）下：redMathDeg = 180° - redRayClockwiseDeg
    //
    // 映射关系：
    //   n=0    → redRayClockwiseDeg=0°   → 红线水平向左，蓝线水平向右
    //   n=0.25 → redRayClockwiseDeg=45°  → 红线左上，蓝线右上
    //   n=0.5  → redRayClockwiseDeg=90°  → 两线垂直向上
    //   n=1    → redRayClockwiseDeg=180° → 红线水平向右，蓝线水平向左

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::redRayClockwiseAngle,
        "Red Ray Clockwise Angle",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.25f,
        juce::AudioParameterFloatAttributes()
            .withLabel("deg")
            .withStringFromValueFunction([](float value, int) {
                // value 是归一化值 [0,1]，显示红线顺时针角度（0°=向左）
                float redRayClockwiseDeg = value * 180.0f;
                return juce::String(redRayClockwiseDeg, 1) + "°";
            })
    ));

    // 正态分布标准差 - 范围 [0.24, 8.0]，默认 1.0
    // 注意：interval 必须设为 0，否则归一化值会被量化
    // 当 sigma 接近下限 0.24 时，归一化值接近 0，会被 interval=0.01 量化到 0，导致跳变
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::sigma,
        "Gaussian Sigma",
        juce::NormalisableRange<float>(0.24f, 8.0f, 0.0f),  // interval=0 禁用量化
        2.70f,
        juce::AudioParameterFloatAttributes()
            .withLabel("sigma")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2);
            })
    ));

    // 每路滤波器频段中心偏移（st）
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ParameterIDs::filterCenterSt,
        "Filter Center",
        -36,
        +36,
        0,
        juce::AudioParameterIntAttributes()
            .withLabel("st")
    ));

    // 每路滤波器频段宽度（st），用于控制抛物线与底部刻度线的两个交点距离
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ParameterIDs::filterWidthSt,
        "Filter Width",
        10,
        72,
        72,
        juce::AudioParameterIntAttributes()
            .withLabel("st")
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::rbPitchQuality,
        "Pitch Quality",
        juce::StringArray{ "Fastest", "Mid", "Best" },
        1
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ParameterIDs::rbFormantMode,
        "Formant Mode",
        juce::StringArray{ "Complex", "Vocal" },
        0
    ));

    // 5个珍珠圆点位置（归一化高度）- 范围 [0.0, 1.0]，默认 1.0（顶端）
    for (int i = 0; i < 5; ++i)
    {
        juce::String paramID = "dot" + juce::String(i);
        juce::String paramName = "Pearl Dot " + juce::String(i + 1);
        
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            paramID,
            paramName,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
            1.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("gain")
                .withStringFromValueFunction([](float value, int) {
                    return juce::String(int(value * 100.0f)) + "%";
                })
        ));
    }

    // 5 路每路独立半音偏移（可被宿主自动化）
    const int kDefaultSemitone[5] = { -24, -12, 0, +12, +24 };
    const juce::String semitoneIDs[5] = {
        ParameterIDs::dot0Semitone,
        ParameterIDs::dot1Semitone,
        ParameterIDs::dot2Semitone,
        ParameterIDs::dot3Semitone,
        ParameterIDs::dot4Semitone
    };

    for (int i = 0; i < 5; ++i)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(
            semitoneIDs[i],
            "Pearl Dot " + juce::String(i + 1) + " Semitone",
            -36,
            +36,
            kDefaultSemitone[i],
            juce::AudioParameterIntAttributes().withLabel("st")
        ));
    }

    return layout;
}

void PuponvstAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // 当宿主自动化参数时，同步更新 EditorState
    // newValue 是归一化值 [0,1]，对应红线顺时针角度 [0°, 180°]
    EditorState s = getEditorState();
    bool stateChanged = false;

    if (parameterID == ParameterIDs::redRayClockwiseAngle)
    {
        s.redRayClockwiseDeg = newValue * 180.0f;
        // redRayClockwiseDeg 与 blueAngleDeg 数值等价，方向解释不同。
        // blueAngleDeg 解释：0°=向右，90°=向上，180°=向左。
        s.blueAngleDeg = s.redRayClockwiseDeg;

        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::sigma)
    {
        // 重要：对于 AudioParameterFloat，parameterChanged 的 newValue 
        // 是去归一化后的实际值，即 [0.24, 8.0] 范围内的值
        // 这不是归一化值 [0,1]！
        float actualSigma = newValue;
        
        float newSigma = juce::jlimit(0.24f, 8.0f, actualSigma);
        s.sigma = newSigma;
        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::filterCenterSt)
    {
        const int centerSt = juce::jlimit(-36, +36, (int) std::lround(newValue));
        pitchEngine.setFilterCenterOffsetSemitones(centerSt);
    }
    else if (parameterID == ParameterIDs::filterWidthSt)
    {
        const int widthSt = juce::jlimit(10, 72, (int) std::lround(newValue));
        pitchEngine.setFilterWidthSemitones(widthSt);
    }
    else if (parameterID == ParameterIDs::rbPitchQuality)
    {
        pitchEngine.setPitchQualityMode((int) std::lround(newValue));
        pitchEngineOptionsDirty.store(true, std::memory_order_release);
    }
    else if (parameterID == ParameterIDs::rbFormantMode)
    {
        pitchEngine.setFormantMode((int) std::lround(newValue));
        pitchEngineOptionsDirty.store(true, std::memory_order_release);
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
            s.dotOffsetT[dotIndex] = juce::jlimit(0.0f, 1.0f, newValue);
            stateChanged = true;
        }
    }
    else if (parameterID == ParameterIDs::dot0Semitone)
    {
        const int st = juce::jlimit(-36, +36, (int) std::lround(newValue));
        s.dotSemitoneOffsets[0] = st;
        pitchEngine.setDotSemitoneOffset(0, st);
        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::dot1Semitone)
    {
        const int st = juce::jlimit(-36, +36, (int) std::lround(newValue));
        s.dotSemitoneOffsets[1] = st;
        pitchEngine.setDotSemitoneOffset(1, st);
        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::dot2Semitone)
    {
        const int st = juce::jlimit(-36, +36, (int) std::lround(newValue));
        s.dotSemitoneOffsets[2] = st;
        pitchEngine.setDotSemitoneOffset(2, st);
        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::dot3Semitone)
    {
        const int st = juce::jlimit(-36, +36, (int) std::lround(newValue));
        s.dotSemitoneOffsets[3] = st;
        pitchEngine.setDotSemitoneOffset(3, st);
        stateChanged = true;
    }
    else if (parameterID == ParameterIDs::dot4Semitone)
    {
        const int st = juce::jlimit(-36, +36, (int) std::lround(newValue));
        s.dotSemitoneOffsets[4] = st;
        pitchEngine.setDotSemitoneOffset(4, st);
        stateChanged = true;
    }

    if (stateChanged)
    {
        setEditorState(s);
    }
}