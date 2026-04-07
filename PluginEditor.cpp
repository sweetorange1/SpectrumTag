#include "PluginEditor.h"
#include <JuceHeader.h>

LDSJvstAudioProcessorEditor::OscilloscopeComponent::OscilloscopeComponent(LDSJvstAudioProcessor& p)
    : processor(p)
{
    startTimerHz(30);
}

void LDSJvstAudioProcessorEditor::OscilloscopeComponent::timerCallback()
{
    processor.getOscilloscopeSnapshot(samples);
    repaint();
}

void LDSJvstAudioProcessorEditor::OscilloscopeComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(b, 6.0f);

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

    if (samples.isEmpty())
        return;

    const float midY = b.getCentreY();
    const float scaleY = b.getHeight() * 0.40f;

    juce::Path waveform;

    const int n = samples.size();
    const float dx = (n > 1 ? (b.getWidth() / (float) (n - 1)) : 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float x = b.getX() + dx * (float) i;
        const float s = juce::jlimit(-1.0f, 1.0f, samples.getUnchecked(i));
        const float y = midY - s * scaleY;

        if (i == 0)
            waveform.startNewSubPath(x, y);
        else
            waveform.lineTo(x, y);
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(b.getX(), midY, b.getRight(), midY, 1.0f);

    g.setColour(juce::Colours::lime.withAlpha(0.9f));
    g.strokePath(waveform, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

LDSJvstAudioProcessorEditor::LDSJvstAudioProcessorEditor(LDSJvstAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(true, true);
    setResizeLimits(360, 200, 1400, 900);

    setSize(520, 260);

    bypassButton.onClick = [this]() {
        processor.bypassed = !processor.bypassed;
        bypassButton.setButtonText(processor.bypassed ? juce::String("Bypassed") : juce::String("Bypass"));
    };

    addAndMakeVisible(oscilloscope);
    addAndMakeVisible(bypassButton);
}

LDSJvstAudioProcessorEditor::~LDSJvstAudioProcessorEditor() {}

void LDSJvstAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void LDSJvstAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto top = area.removeFromTop(36);
    bypassButton.setBounds(top.removeFromRight(120));

    area.removeFromTop(10);
    oscilloscope.setBounds(area);
}
