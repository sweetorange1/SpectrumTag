#pragma once
static void revision146Checks(const juce::File& dir){
    // Accelerated/offline recording must keep absolute positions and show its whole span.
    SpectrumTagAudioProcessor p;Transport transport;p.setPlayHead(&transport);p.setRateAndBufferSizeDetails(48000,8192);p.prepareToPlay(48000,8192);p.setNonRealtime(true);p.recording().setArmed(true);
    juce::MidiBuffer midi;juce::AudioBuffer<float> block(2,8192);
    for(int64_t i=0;i<60*48000;i+=8192){int count=(int)juce::jmin<int64_t>(8192,60*48000-i);block.setSize(2,count);for(int c=0;c<2;++c)for(int j=0;j<count;++j)block.setSample(c,j,.15f*std::sin((i+j)*.23));transport.sample=i;p.processBlock(block,midi);}
    p.setNonRealtime(false);transport.playing=false;block.clear();p.processBlock(block,midi);require(p.recording().flush(30000),"offline capture flush");
    auto source=p.recording().snapshot();require(source&&source->first==0&&source->last==60*48000&&p.recording().errorCode()==0,"offline capture retains all 60 seconds");
    for(double t=.02;t<59.98;t+=.031)require(source->frameAt(t)!=nullptr,"full offline recording spectrum has no missing tiles");
    set(p,ParameterIDs::speed,4);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();auto* view=componentWithID(*editor,"spectrumView");
    auto fit=[&]{fitSpectrum(*view);tickUI();};fit();
    auto start=[&]{return (double)view->getProperties()["displayStart"];};auto end=[&]{return (double)view->getProperties()["displayEnd"];};
    require(std::abs(start())<1e-5&&std::abs(end()-60)<1e-5,"Speed 4 fits full 60-second overview without zoom clamp");
    auto* img=imageBox(*editor);auto aspect=img->aspect();const auto originalBounds=img->getBounds();
    auto wheel=[&](int mods){auto now=juce::Time::getCurrentTime();juce::Point<float> point(400,200);juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(mods),1,0,0,0,0,view,view,now,point,now,0,false);juce::MouseWheelDetails w;w.deltaY=.2f;view->mouseWheelMove(e,w);tickUI();};
    set(p,ParameterIDs::speed,1);tickUI();fit();float old=*p.getAPVTS().getRawParameterValue(ParameterIDs::speed);wheel(juce::ModifierKeys::shiftModifier);float shifted=*p.getAPVTS().getRawParameterValue(ParameterIDs::speed);require(shifted>old,"Shift-wheel changes Speed");wheel(juce::ModifierKeys::ctrlModifier);require(*p.getAPVTS().getRawParameterValue(ParameterIDs::speed)>shifted,"Ctrl-wheel also changes Speed");
    const double before=end();wheel(0);require(end()<before,"plain wheel pans earlier");
    require(img->getBounds()==originalBounds&&std::abs((double)img->getWidth()/img->getHeight()-aspect)<.01,"wheel navigation never distorts image");
    auto* ranges=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"frequencyRange"));require(ranges&&ranges->getNumItems()==4,"four display ceilings");
    const double tops[]={20000,24000,22050,24000};for(int i=0;i<4;++i){ranges->setSelectedId(i+1,juce::sendNotificationSync);tickUI();require(std::abs((double)view->getProperties()["displayTop"]-tops[i])<.1,"display ceiling respects Nyquist");require(img->getBounds()==originalBounds,"frequency range keeps image aspect");}
    auto* language=dynamic_cast<juce::Button*>(componentWithID(*editor,"tooltipLanguage"));auto* record=dynamic_cast<juce::Button*>(componentWithID(*editor,"recordButton"));const auto text=record->getButtonText();language->onClick();require(record->getButtonText()!=text,"language changes control text");
    for(auto name:{"imageMode","fftScale"}){auto* combo=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,name));require(combo&&combo->getSelectedId()>0&&combo->getText().isNotEmpty(),"translated combo retains its selected value");}
    fit();tickUI();png(dir.getChildFile("full-recording-60s.png"),editor->createComponentSnapshot(editor->getLocalBounds()));tickUI();png(dir.getChildFile("full-recording-60s.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    // Actual single click does not load or change an image.
    const auto path=img->getImagePath();img->mouseDown(mouseAt(*img,{20,20}));img->mouseUp(mouseAt(*img,{20,20}));require(img->getImagePath()==path,"single click retains image");
    // Previous 1.4.5 cache remains readable (read-only fixture on this workstation).
    juce::File oldCache("D:/Codex/PluginData/SpectrumTag/Recordings/d33619f653154e63a4ef37169563cf2a.sptrec");
    if(oldCache.existsAsFile()){spectrumtag::RecordingEngine old;old.restore(oldCache.getFullPathName(),true);require(old.flush(30000)&&old.snapshot()&&std::abs(old.snapshot()->end()-57.5453541667)<1e-6,"legacy cache span preserved");}
    SpectrumTagAudioProcessor hi;hi.setRateAndBufferSizeDetails(96000,256);hi.prepareToPlay(96000,256);set(hi,ParameterIDs::frequencyRange,3);std::unique_ptr<juce::AudioProcessorEditor> hiEditor(hi.createEditor());tickUI();std::cout<<"high_rate="<<hi.getSampleRate()<<" range="<<*hi.getAPVTS().getRawParameterValue(ParameterIDs::frequencyRange)<<" top="<<(double)componentWithID(*hiEditor,"spectrumView")->getProperties()["displayTop"]<<"\n";require((double)componentWithID(*hiEditor,"spectrumView")->getProperties()["displayTop"]==48000,"96 kHz audio displays true 48 kHz ceiling");
    // Coverage distinguishes captured silence from absent samples, including local seeks.
    SpectrumTagAudioProcessor gaps;Transport play;gaps.setPlayHead(&play);gaps.setRateAndBufferSizeDetails(48000,256);gaps.prepareToPlay(48000,256);
    gaps.recording().setArmed(true);juce::AudioBuffer<float> silence(2,48000);silence.clear();gaps.recording().capture(silence,0,48000);gaps.recording().capture(silence,2,48000);gaps.recording().setArmed(false);require(gaps.recording().flush(),"coverage fixture");
    play.sample=8*48000;play.playing=true;
    auto playBlocks=[&]{for(int k=0;k<24;++k){juce::AudioBuffer<float> audio(2,256);for(int c=0;c<2;++c)for(int i=0;i<256;++i)audio.setSample(c,i,.37f);gaps.processBlock(audio,midi);play.sample+=256;block=audio;}};
    playBlocks();require(std::abs(block.getSample(0,200)-.37f)<1e-6,"unrecorded host position passes original input");
    gaps.audition().seek(1.25);playBlocks();require(std::abs(block.getSample(0,200))<1e-6,"unrecorded local audition gap is silent");
    gaps.audition().seek(.25);playBlocks();require(std::abs(block.getSample(0,200))<1e-6,"captured silence plays captured silence");
    std::cout<<"1.4.6 CHECKS PASSED; full offline overview, wheel mappings, Nyquist ceilings, aspect and covered/uncovered playback\n";
}
