#pragma once
static void revision1410Checks(const juce::File& dir) {
    using namespace spectrumtag;
    SpectrumTagAudioProcessor p;Transport transport;transport.playing=false;p.setPlayHead(&transport);
    p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    auto fixed=p.getEditorState();fixed.viewValid=true;fixed.viewFollow=false;fixed.viewEnd=20;p.setEditorState(fixed);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
    auto* scale=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"fftScale"));
    auto* speed=dynamic_cast<juce::Slider*>(componentWithID(*editor,"speed"));
    auto* label=dynamic_cast<juce::Label*>(componentWithID(*editor,"timeSpan"));require(label,"1410 duration label exists");
    auto span=[&]{tickUI();return (double)view->getProperties()["displayEnd"]-(double)view->getProperties()["displayStart"];};
    double reference=0;
    for(int mode:{2,0,1,4,5,6}) {
        const double beforeSpan=span();const double anchor=(double)view->getProperties()["displayStart"]+box->getX()/(double)box->getParentComponent()->getWidth()*beforeSpan;
        scale->setSelectedId(mode+1,juce::sendNotificationSync);tickUI();
        if(mode==2||mode==3) {
            const double afterAnchor=(double)view->getProperties()["displayStart"]+box->getX()/(double)box->getParentComponent()->getWidth()*span();
            require(std::abs(anchor-afterAnchor)<.001,"1410 preset retains source time anchor");
            const double expected=550.*(mode==2?840./528.:1186./560.)/(mode==2?18.:5.)/60.;
            require(std::abs(speed->getValue()-expected)<1e-6,"1410 preset writes common-scale Speed value");
        }
        speed->setValue(2,juce::sendNotificationSync);const double seconds=span();
        if(reference==0)reference=seconds;
        require(std::abs(seconds-reference)<1e-6,"1410 equal Speed means equal visible seconds in every mode");
        require(label->getText().contains(juce::String(seconds,2)),"1410 duration label matches real viewport");
        const auto beforeSize=seconds;editor->setSize(2480,1360);require(std::abs(span()-beforeSize)<1e-6,"1410 whole UI scaling does not change time");editor->setSize(1240,680);
    }
    for(int height:{468,550})for(int mode:{0,1,2,3,4,5,6})for(float oldSpeed:{.1f,1.f,4.f}) {
        SpectrumTagAudioProcessor old;set(old,ParameterIDs::frequencyPreset,(float)juce::jmax(0,mode-1));set(old,ParameterIDs::fftScale,mode==1?1.f:0.f);set(old,ParameterIDs::speed,oldSpeed);
        auto es=old.getEditorState();es.spectrumHeightPx=height;es.viewZoom=1.7;es.viewValid=true;es.viewEnd=42;old.setEditorState(es);
        juce::MemoryBlock data;old.getStateInformation(data);auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),(int)data.getSize());auto tree=juce::ValueTree::fromXml(*xml);tree.setProperty("version",5,nullptr);juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),data);
        old.setStateInformation(data.getData(),(int)data.getSize());
        const double oldBase=(mode==2||mode==3)?height*(mode==2?840./528.:1186./560.)/(mode==2?18.:5.):60.;
        const double actual=*old.getAPVTS().getRawParameterValue(ParameterIDs::speed)*60.*old.getEditorState().viewZoom;
        require(std::abs(actual-oldSpeed*oldBase*1.7)<.0002,"1410 old project keeps actual time rate including range limits");
        require(old.getEditorState().viewEnd==42,"1410 migration preserves timeline end");
        const float converted=*old.getAPVTS().getRawParameterValue(ParameterIDs::speed);const double zoom=old.getEditorState().viewZoom;
        old.getStateInformation(data);old.setStateInformation(data.getData(),(int)data.getSize());
        require(std::abs(*old.getAPVTS().getRawParameterValue(ParameterIDs::speed)-converted)<1e-6&&std::abs(old.getEditorState().viewZoom-zoom)<1e-12*juce::jmax(1.,zoom),"1410 resaved project never migrates twice");
    }
    // A real SPT3 fixture with legacy speeds. Check physical audio and masks,
    // including the old high-speed Super Vision value exceeding the new range.
    RecordingEngine legacy;juce::AudioBuffer<float> audio(2,48000);for(int c=0;c<2;++c)for(int i=0;i<48000;++i)audio.setSample(c,i,.03f*std::sin(i*.071f));
    legacy.setArmed(true);legacy.capture(audio,0,48000,true);legacy.setArmed(false);require(legacy.flush(),"1410 legacy source");
    for(int mode:{2,3}){PrintEdit e;e.fillVersion=0;e.start=mode==2?.1:.6;e.duration=.25;e.low=.1f;e.high=.8f;e.rows=513;e.cols=12;e.fftSize=1024;e.speed=mode==2?1.f:4.f;e.viewZoom=1.3;e.scaleMode=mode;e.mask=std::make_shared<const std::vector<float>>(513*12,1.f);legacy.add(e);}
    require(legacy.flush(),"1410 legacy Print fixture");auto before=legacy.snapshot();
    // Write the historical schema, independent of the current writer version.
    juce::MemoryOutputStream bytes;const auto& s=*before;bytes.writeInt(0x53505433);bytes.writeDouble(s.sampleRate);bytes.writeInt(s.channels);bytes.writeInt(s.fftSize);bytes.writeInt64(s.origin);bytes.writeInt64(s.first);bytes.writeInt64(s.last);bytes.writeInt((int)s.original.size());
    for(auto page:s.original){bytes.writeBool(page!=nullptr);if(page){for(int n=0;n<recordingPageSize;n+=8){unsigned bits=0;for(int b=0;b<8;++b)if(page->valid[n+b])bits|=1u<<b;bytes.writeByte((char)bits);}for(int c=0;c<s.channels;++c)for(float v:page->samples[c])bytes.writeFloat(v);}}
    bytes.writeInt((int)s.edits.size());for(const auto& e:s.edits){bytes.writeString(e.id);bytes.writeString(e.imagePath);bytes.writeDouble(e.start);bytes.writeDouble(e.duration);bytes.writeFloat(e.low);bytes.writeFloat(e.high);bytes.writeFloat(e.ratio);bytes.writeFloat(e.compensation);bytes.writeInt(e.fftSize);bytes.writeInt(e.rows);bytes.writeInt(e.cols);bytes.writeBool(e.invert);bytes.writeFloat(e.speed);bytes.writeFloat(e.amount);bytes.writeFloat(e.displayTop);bytes.writeFloat(e.imageAspect);bytes.writeDouble(e.viewZoom);bytes.writeInt(e.imageMode);bytes.writeInt(e.scaleMode);for(float v:*e.mask)bytes.writeByte((char)juce::roundToInt(v*255));}
    const auto cache=dir.getChildFile("1410-legacy-SPT3.sptrec");require(cache.replaceWithData(bytes.getData(),bytes.getDataSize()),"1410 fixture saved");
    RecordingEngine loaded;loaded.restore(cache.getFullPathName(),true);require(loaded.flush()&&loaded.errorCode()==0,"1410 SPT3 migrated");auto after=loaded.snapshot();
    for(size_t i=0;i<before->edits.size();++i){const auto& a=before->edits[i];const auto& b=after->edits[i];require(sameAudioEdit(a,b),"1410 migrated Print retains mask, position and duration");const double oldBase=468.*(a.scaleMode==2?840./528.:1186./560.)/(a.scaleMode==2?18.:5.);require(std::abs(b.speed*60.*b.viewZoom-a.speed*oldBase*a.viewZoom)<.00001,"1410 old Print keeps its display rate");}
    for(int64_t i=0;i<48000;i+=17)require(before->sample(0,i)==after->sample(0,i),"1410 migrated Print retains actual audio");
    RecordingEngine again;again.restore(loaded.save(),true);require(again.flush()&&sameEdits(again.edits(),loaded.edits()),"1410 SPT4 does not migrate again");
    // The editor-closed automatic Print must finish after the same sample count
    // for all seven modes, not merely look equal in the UI.
    int firstBlocks=-1;
    for(int mode=0;mode<=6;++mode){SpectrumTagAudioProcessor closed;Transport t;closed.setPlayHead(&t);closed.setRateAndBufferSizeDetails(48000,256);closed.prepareToPlay(48000,256);closed.setNonRealtime(true);set(closed,ParameterIDs::frequencyPreset,(float)juce::jmax(0,mode-1));set(closed,ParameterIDs::fftScale,mode==1?1.f:0.f);set(closed,ParameterIDs::speed,2);
        auto state=closed.getEditorState();state.hasValidValues=true;state.imagePath=dir.getChildFile("transparent-detail.png").getFullPathName();state.imgBoxWidthPx=60;state.viewValid=true;state.viewZoom=2;state.spectrumHeightPx=550;closed.setEditorState(state);
        juce::AudioBuffer<float> block(2,256);block.clear();juce::MidiBuffer midi;for(int i=0;i<60;++i){t.sample=i*256;closed.processBlock(block,midi);}set(closed,ParameterIDs::printTrigger,1);
        int count=0;bool started=false;for(;count<300;++count){t.sample+=256;closed.processBlock(block,midi);if(closed.isPrintRunning())started=true;else if(started)break;}
        require(started&&count<300,"1410 automatic Print starts and finishes");if(firstBlocks<0)firstBlocks=count;require(count==firstBlocks,"1410 actual Print length is mode independent");
    }
    // A stopped live click must complete immediately even when the host is not
    // calling processBlock. It must not arm a future Print on the next Play.
    // 图片对象不再由偏好预置：先拖入一张，实时打印才有可用的源图。
    dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));const auto visible=box->isVisible();
    for(int i=0;i<3;++i){print->onClick();require(!p.isPrintRunning()&&!p.getPrintTriggerParam()->get()&&print->isEnabled()&&box->isVisible()==visible,"1410 stopped live click is immediate no-op");}
    set(p,ParameterIDs::printTrigger,1);tickUI();require(!p.isPrintRunning()&&!p.getPrintTriggerParam()->get()&&!p.consumeAutomationPrintRequest(),"1410 stopped trigger resets without audio callbacks");
    juce::MidiBuffer midi;juce::AudioBuffer<float> block(2,256);block.clear();transport.playing=true;transport.sample=0;p.processBlock(block,midi);tickUI();require(!p.isPrintRunning(),"1410 stopped Print never queues on Play");
    print->onClick();require(p.isPrintRunning(),"1410 live Print still starts during playback");transport.playing=false;transport.sample+=256;p.processBlock(block,midi);tickUI();require(!p.isPrintRunning()&&print->isEnabled()&&!p.getPrintTriggerParam()->get(),"1410 stopping unfinished live Print resets button and trigger");
    print->onClick();require(!p.isPrintRunning()&&print->isEnabled(),"1410 stopped second click stays usable");
    for(const char* id:{"speed","amplitudeRatio","imageAmount","noiseFill"})require(dynamic_cast<FineSlider*>(componentWithID(*editor,id)),"1410 every continuous control supports Shift");
    for(double skew:{1.,.5}) {
        FineSlider slider;slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);slider.setBounds(0,0,300,30);slider.setRange(0,1,0);slider.setSkewFactor(skew);
        int begins=0,ends=0;slider.onDragStart=[&]{++begins;};slider.onDragEnd=[&]{++ends;};
        auto event=[&](float x,bool shift,bool button=true){const auto now=juce::Time::getCurrentTime();return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,15},juce::ModifierKeys((button?juce::ModifierKeys::leftButtonModifier:0)|(shift?juce::ModifierKeys::shiftModifier:0)),1,0,0,0,0,&slider,&slider,now,{x,15},now,1,true);};
        auto move=[&](bool fine){slider.setValue(.4,juce::dontSendNotification);const float x=slider.getPositionOfValue(.4);slider.mouseDown(event(x,fine));const double a=slider.valueToProportionOfLength(slider.getValue());slider.mouseDrag(event(x+40,fine));const double delta=slider.valueToProportionOfLength(slider.getValue())-a;slider.mouseUp(event(x+40,fine));return delta;};
        const double normal=move(false),fine=move(true);require(std::abs(fine/normal-.1)<.0001,"1410 Shift drag is ten times finer including skewed Speed");
        slider.setValue(.4,juce::dontSendNotification);const float x=slider.getPositionOfValue(.4);slider.mouseDown(event(x,false));slider.mouseDrag(event(x+20,false));const auto atShift=slider.getValue();slider.mouseDrag(event(x+20,true));require(slider.getValue()==atShift,"1410 pressing Shift mid-drag does not jump");slider.mouseDrag(event(x+40,true));const auto atRelease=slider.getValue();slider.mouseDrag(event(x+40,false));require(slider.getValue()==atRelease,"1410 releasing Shift mid-drag does not jump");slider.mouseDrag(event(x+50,false));slider.mouseUp(event(x+50,false));
        slider.setValue(.4,juce::dontSendNotification);slider.mouseDown(event(290,true));require(std::abs(slider.getValue()-.4)<1e-6,"1410 Shift click does not jump to cursor");slider.mouseUp(event(290,true));
        require(begins==4&&ends==4,"1410 Shift transitions remain a single automation gesture");
        auto wheelEvent=event(100,false,false);juce::MouseWheelDetails wheel;wheel.deltaY=.2f;wheel.isSmooth=true;
        slider.setValue(.4,juce::dontSendNotification);slider.mouseWheelMove(wheelEvent,wheel);const double normalWheel=slider.valueToProportionOfLength(slider.getValue())-slider.valueToProportionOfLength(.4);
        slider.setValue(.4,juce::dontSendNotification);juce::Thread::sleep(2);slider.mouseWheelMove(event(100,true,false),wheel);const double fineWheel=slider.valueToProportionOfLength(slider.getValue())-slider.valueToProportionOfLength(.4);
        require(std::abs(fineWheel/normalWheel-.1)<.0001,"1410 Shift wheel is ten times finer");
    }
    auto* lang=dynamic_cast<juce::Button*>(componentWithID(*editor,"tooltipLanguage"));
    if(!p.getEditorState().chinese)lang->onClick();tickUI();png(dir.getChildFile("1410-unified-time-zh.png"),editor->createComponentSnapshot(editor->getLocalBounds()));lang->onClick();tickUI();png(dir.getChildFile("1410-unified-time-en.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    std::cout<<"1.4.10 CHECKS PASSED; common viewport="<<reference<<" s at 2x, all-mode closed-editor Print blocks="<<firstBlocks<<", preset anchor, DPI, legacy state/cache audio and one-time migration\n";
}
