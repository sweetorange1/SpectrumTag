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
    apvts.addParameterListener(ParameterIDs::rayslopeK, this);
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
// 内容包含：5 圆点高度、红蓝射线斜率、正态曲线方差、黄色激光归一化值。
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
        // 从 blueAngleDeg 计算 rayslopeK
        {
            float redAngleDeg = 180.0f - s.blueAngleDeg;
            float redAngleRad = redAngleDeg * (juce::MathConstants<float>::pi / 180.0f);
            float verticalRad = 90.0f * (juce::MathConstants<float>::pi / 180.0f);
            float angleDiff = redAngleRad - verticalRad;
            constexpr float kMaxSlope = 1.0e4f;
            if (std::abs(angleDiff) < 0.0001f)
                s.rayslopeK = 0.0f;
            else if (angleDiff > juce::MathConstants<float>::halfPi - 0.01f)
                s.rayslopeK = -kMaxSlope;
            else if (angleDiff < -juce::MathConstants<float>::halfPi + 0.01f)
                s.rayslopeK = kMaxSlope;
            else
                s.rayslopeK = std::tan(angleDiff);
        }
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

    // 红线斜率（红蓝激光控制器）- 归一化范围 [0, 1]，默认 0.25 (45°)
    // 宿主参数 0 → 蓝线向右；0.25 → 蓝线45°右上；0.5 → 蓝线垂直向上；1 → 蓝线向左
    //
    // 使用角度映射（连续变化）：
    //   归一化值 n ∈ [0,1] → 蓝线角度 angleBlue = n * 180°（0°=向右，90°=向上，180°=向左）
    //   红线角度 angleRed = 180° - angleBlue（左右对称）
    //   内部存储 rayslopeK = tan(angleRed)（红线斜率，数学坐标系）
    //
    // 映射关系：
    //   n=0   → angleBlue=0°   → angleRed=180°  → rayslopeK=tan(180°)=0      → 蓝线向右，红线向左（水平）
    //   n=0.25 → angleBlue=45° → angleRed=135° → rayslopeK=tan(135°)=-1     → 蓝线右上，红线左上
    //   n=0.5 → angleBlue=90°  → angleRed=90°   → rayslopeK=tan(90°)=∞(近似) → 两线都垂直向上
    //   n=1   → angleBlue=180° → angleRed=0°    → rayslopeK=tan(0°)=0        → 蓝线向左，红线向右（水平）
    //
    // 注意：由于斜率在90°趋于无穷，实际使用时用大数近似（±1e4）
    //   角度定义：0°=向右，90°=向上，180°=向左（与常规极坐标一致）
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::rayslopeK,
        "Red-Blue Laser Slope",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.25f,
        juce::AudioParameterFloatAttributes()
            .withLabel("angle")
            .withStringFromValueFunction([](float value, int) {
                // value 是归一化值 [0,1]，对应蓝线角度 0°-180°
                // 显示蓝线角度（保留一位小数）
                float blueAngleDeg = value * 180.0f;
                return juce::String(blueAngleDeg, 1) + "°";
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
        juce::StringArray{ "Fastest", "FastestTolerable", "Best" },
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

    return layout;
}

void PuponvstAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // 当宿主自动化参数时，同步更新 EditorState
    // newValue 是归一化值 [0,1]，对应蓝线角度 [0°, 180°]
    EditorState s = getEditorState();
    bool stateChanged = false;

    if (parameterID == ParameterIDs::rayslopeK)
    {
        // 蓝线角度：0° = 向右，90° = 向上，180° = 向左
        s.blueAngleDeg = newValue * 180.0f;
        
        // 计算对应的 rayslopeK（红线斜率）
        // 角度定义：0° = 向右，90° = 向上，180° = 向左（与 calculateRayEndByAngle 一致）
        // 红线角度 = 180° - 蓝线角度
        // 红线斜率 = tan(angleRedRad)（数学坐标系中，角度从X轴正向逆时针测量）
        float redAngleDeg = 180.0f - s.blueAngleDeg;
        float redAngleRad = redAngleDeg * (juce::MathConstants<float>::pi / 180.0f);
        
        constexpr float kMaxSlope = 1.0e4f;
        // 处理边界情况：cos(angle) = 0 时 tan 无穷大（垂直方向）
        if (std::abs(std::cos(redAngleRad)) < 1e-6f)
        {
            // 红线垂直向上（90°）或垂直向下（270°）
            // 根据 sin 的符号决定斜率方向（应该向上，所以 sin > 0）
            s.rayslopeK = (std::sin(redAngleRad) >= 0) ? kMaxSlope : -kMaxSlope;
        }
        else
        {
            s.rayslopeK = std::tan(redAngleRad);
        }
        
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
    else if (parameterID.startsWith("dot"))
    {
        int dotIndex = parameterID.substring(3).getIntValue();
        if (dotIndex >= 0 && dotIndex < 5)
        {
            s.dotOffsetT[dotIndex] = juce::jlimit(0.0f, 1.0f, newValue);
            stateChanged = true;
        }
    }

    if (stateChanged)
    {
        setEditorState(s);
    }
}