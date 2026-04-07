#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LDSJvstAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    LDSJvstAudioProcessorEditor(LDSJvstAudioProcessor&);
    ~LDSJvstAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class OscilloscopeComponent final : public juce::Component,
                                        private juce::Timer
    {
    public:
        explicit OscilloscopeComponent(LDSJvstAudioProcessor&);

        void paint(juce::Graphics&) override;

    private:
        void timerCallback() override;

        LDSJvstAudioProcessor& processor;
        juce::Array<float> samples;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeComponent)
    };

    LDSJvstAudioProcessor& processor;
    OscilloscopeComponent oscilloscope { processor };
    juce::TextButton bypassButton { "Bypass" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LDSJvstAudioProcessorEditor)
};

