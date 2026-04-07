#pragma once

#include <JuceHeader.h>
#include <array>

class LDSJvstAudioProcessor : public juce::AudioProcessor
{
public:
    LDSJvstAudioProcessor();
    ~LDSJvstAudioProcessor() override;

    // AudioProcessor overrides
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // 提供给界面线程使用：获取示波器数据快照（按时间从旧到新排列）
    void getOscilloscopeSnapshot(juce::Array<float>& dest);

    bool bypassed = false;

private:
    static constexpr int oscilloscopeBufferSize = 2048;

    void pushSamplesToOscilloscope(const float* samples, int numSamples);

    juce::SpinLock oscilloscopeLock;
    std::array<float, oscilloscopeBufferSize> oscilloscopeBuffer {};
    int oscilloscopeWritePos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LDSJvstAudioProcessor)
};
