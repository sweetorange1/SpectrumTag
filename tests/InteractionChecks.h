#pragma once
#include "shared/Presentation.h"
static juce::Component* componentWithID(juce::Component& root,const juce::String& id) {
    if(root.getComponentID()==id)return &root;
    for(auto* child:root.getChildren())if(auto* found=componentWithID(*child,id))return found;
    return nullptr;
}
static void interactionChecks(const juce::File& dir) {
    SpectrumTagAudioProcessor p;Transport transport;p.setPlayHead(&transport);
    p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    require(std::abs(*p.getAPVTS().getRawParameterValue(ParameterIDs::imageAmount)-.5f)<1e-6,"amount defaults to .5");
    require(std::abs(p.getAPVTS().getParameterRange(ParameterIDs::amplitudeRatio).end-.33f)<1e-6,"amplitude upper limit .33");
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor=dynamic_cast<SpectrumTagAudioProcessorEditor*>(base.get());require(editor!=nullptr,"editor type");
    auto* fft=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"fftSize"));require(fft&&fft->getSelectedId()==3,"4096 parameter displays 4096 after removing 8192");
    for(int choice=0;choice<3;++choice){fft->setSelectedId(choice+1,juce::sendNotificationSync);require((int)*p.getAPVTS().getRawParameterValue(ParameterIDs::fftSize)==choice,"FFT UI and processing agree");}
    set(p,ParameterIDs::fftSize,3);require(fft->getSelectedId()==3,"legacy fourth automation slot displays 4096");set(p,ParameterIDs::fftSize,2);
    require(!componentWithID(*editor,"noiseFill")->isEnabled(),"noise control disabled before Record/import");
    const auto* ratioParameter=p.getAPVTS().getParameter(ParameterIDs::amplitudeRatio);
    require(std::abs(ratioParameter->convertFrom0to1(ratioParameter->getValueForText("20%"))-.2f)<1e-5,"percentage text input parses correctly");
    auto* amount=dynamic_cast<juce::Slider*>(componentWithID(*editor,"imageAmount"));
    require(amount!=nullptr,"amount control");amount->setValue(.76,juce::sendNotificationSync);
    const auto time=juce::Time::getCurrentTime();const juce::Point<float> point(20,10);
    juce::MouseEvent alt(juce::Desktop::getInstance().getMainMouseSource(),point,
        juce::ModifierKeys(juce::ModifierKeys::altModifier|juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,amount,amount,time,point,time,1,false);
    amount->mouseDown(alt);amount->mouseUp(alt);
    require(std::abs(*p.getAPVTS().getRawParameterValue(ParameterIDs::imageAmount)-.5f)<1e-5,"Alt-left click resets amount and parameter");
    auto* visibility=dynamic_cast<juce::Button*>(componentWithID(*editor,"imageVisibility"));
    require(visibility!=nullptr,"visibility control");visibility->onClick();
    require(!imageBox(*editor)->isVisible(),"hide image");
    editor->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);
    require(imageBox(*editor)->isVisible()&&p.getEditorState().imageVisible,"new image restores visibility");
    const auto loadedPath=imageBox(*editor)->getImagePath();
    require(spectrumtag::Preferences::read().imagePath==loadedPath,"image preference saved");
    auto maskBefore=imageBox(*editor)->generateMask(129,64);
    auto* ratio=dynamic_cast<juce::Slider*>(componentWithID(*editor,"amplitudeRatio"));require(ratio!=nullptr,"ratio control");
    ratio->setValue(.25,juce::sendNotificationSync);
    auto preview=imageBox(*editor)->getPreviewImage();
    require(preview.getPixelAt(20,20).getRed()>0&&preview.getPixelAt(20,20).getRed()<255,"ratio is visible in preview");
    require(maskBefore==imageBox(*editor)->generateMask(129,64),"ratio preview does not alter binary mask");
    auto* palette=dynamic_cast<juce::Button*>(componentWithID(*editor,"palette1"));require(palette!=nullptr,"palette button");palette->onClick();
    require(p.getEditorState().palette==1&&spectrumtag::Preferences::read().palette==1,"palette remembered");
    auto* language=dynamic_cast<juce::Button*>(componentWithID(*editor,"tooltipLanguage"));require(language!=nullptr,"language button");
    const auto firstTip=ratio->getTooltip();language->onClick();require(firstTip!=ratio->getTooltip(),"tooltip language changes");language->onClick();
    SpectrumTagAudioProcessor fresh;require(fresh.getEditorState().imagePath==loadedPath&&fresh.getEditorState().palette==1,"new instance restores last image and colour");
    std::unique_ptr<juce::AudioProcessorEditor> freshEditor(fresh.createEditor());
    require(imageBox(*freshEditor)->hasImage(),"new editor loads remembered image");
    visibility->onClick();juce::MemoryBlock state;p.getStateInformation(state);
    SpectrumTagAudioProcessor recalled;recalled.setStateInformation(state.getData(),(int)state.getSize());
    require(recalled.getEditorState().palette==1&&!recalled.getEditorState().imageVisible,"project recalls colour and hidden image");
    // No UI polling during this burst: every generated FFT frame must reach the queue.
    juce::MidiBuffer midi;
    for(int block=0;block<80;++block){juce::AudioBuffer<float> audio(2,256);
        for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)audio.setSample(ch,i,.2f*std::sin((block*256+i)*.1f));
        p.processBlock(audio,midi);}
    std::vector<spectrumtag::SpectrumFrame> frames;p.drainSpectrumFrames(frames);
    require((int)frames.size()==p.getDisplayUpdateCounter(),"all FFT frames retained between UI refreshes");
    for(size_t i=1;i<frames.size();++i)require(frames[i].time>frames[i-1].time,"history timestamps ordered");
    for(const auto& frame:frames)p.getSpectrumHistory()->append(frame);
    p.startPrint(std::vector<float>(33*20,1),33,20,0,1,10);
    juce::AudioBuffer<float> audio(2,256);audio.clear();p.processBlock(audio,midi);require(p.isPrintRunning(),"long Print started");
    transport.playing=false;p.processBlock(audio,midi);require(!p.isPrintRunning(),"stop cancels unfinished Print");
    const auto stopped=p.getVisualisedSamples();p.processBlock(audio,midi);require(stopped==p.getVisualisedSamples(),"stop freezes spectrum clock");
    p.startPrint(std::vector<float>(33*20,1),33,20,0,1,10);p.processBlock(audio,midi);require(!p.isPrintRunning(),"stopped block rejects live Print instead of arming it");
    transport.playing=true;p.processBlock(audio,midi);require(!p.isPrintRunning(),"stopped Print does not restart on Play");
    p.startPrint(std::vector<float>(33*20,1),33,20,0,1,10);p.processBlock(audio,midi);require(p.isPrintRunning(),"new Print works during playback");
    transport.playing=false;p.processBlock(audio,midi);require(!p.isPrintRunning(),"second stop cancels active Print");
    juce::Thread::sleep(45);juce::Timer::callPendingTimersSynchronously();
    for(auto* child:editor->getChildren())if(auto* button=dynamic_cast<RoundPrintButton*>(child))require(button->isEnabled(),"Print button reenabled after stop");
    visibility->onClick();
    png(dir.getChildFile("editor-1.4.6.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    for(int i=0;i<8;++i){auto* button=dynamic_cast<juce::Button*>(componentWithID(*editor,"palette"+juce::String(i)));button->onClick();
        png(dir.getChildFile("palette-"+juce::String(i)+".png"),editor->createComponentSnapshot(editor->getLocalBounds()));}
    std::cout<<"INTERACTION CHECKS PASSED; retained "<<frames.size()<<" consecutive FFT frames\n";
}
