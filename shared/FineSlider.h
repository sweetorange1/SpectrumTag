#pragma once
#include <JuceHeader.h>

namespace spectrumtag {
// Preserve JUCE's attachments and one begin/end gesture. Only the coordinates
// sent to its drag handler are scaled, including mid-drag Shift transitions.
class FineSlider : public juce::Slider {
public:
    void mouseDown(const juce::MouseEvent& event) override {
        lastMouse=virtualMouse=event.position;
        dragging=isEnabled()&&event.mods.isLeftButtonDown()&&!event.mods.isAltDown()&&!event.mods.isPopupMenu();
        if(dragging&&event.mods.isShiftDown())virtualMouse.x=getPositionOfValue(getValue());
        juce::Slider::mouseDown(event.withNewPosition(virtualMouse));
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if(!dragging)return;
        const float sensitivity=event.mods.isShiftDown()?.1f:1.f;
        virtualMouse.x=juce::jlimit(getPositionOfValue(getMinimum()),getPositionOfValue(getMaximum()),
            virtualMouse.x+(event.position.x-lastMouse.x)*sensitivity);
        lastMouse=event.position;
        juce::Slider::mouseDrag(event.withNewPosition(virtualMouse));
    }
    void mouseUp(const juce::MouseEvent& event) override {
        juce::Slider::mouseUp(event.withNewPosition(virtualMouse));dragging=false;
    }
    void mouseWheelMove(const juce::MouseEvent& event,const juce::MouseWheelDetails& wheel) override {
        auto adjusted=wheel;
        if(event.mods.isShiftDown()){adjusted.deltaX*=.1f;adjusted.deltaY*=.1f;}
        juce::Slider::mouseWheelMove(event,adjusted);
    }
private:
    bool dragging=false;
    juce::Point<float> lastMouse,virtualMouse;
};
}
