#pragma once
static void revision147Checks(const juce::File& dir){
    using namespace spectrumtag;
    SpectrumTagAudioProcessor p;Transport transport;transport.playing=false;p.setPlayHead(&transport);
    p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> source(2,6*48000);std::mt19937 rng(147);std::uniform_real_distribution<float> random(-.025f,.025f);
    for(int c=0;c<2;++c)for(int i=0;i<source.getNumSamples();++i)source.setSample(c,i,random(rng));
    p.recording().setArmed(true);p.recording().capture(source,0,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"147 source capture");
    set(p,ParameterIDs::speed,1);set(p,ParameterIDs::noiseFill,.65f);set(p,ParameterIDs::frequencyRange,1);set(p,ParameterIDs::fftScale,0);set(p,ParameterIDs::frequencyPreset,0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
    auto* visibility=dynamic_cast<juce::Button*>(componentWithID(*editor,"imageVisibility"));
    auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));
    auto* speed=dynamic_cast<juce::Slider*>(componentWithID(*editor,"speed"));
    auto* ratio=dynamic_cast<juce::Slider*>(componentWithID(*editor,"amplitudeRatio"));
    auto* language=dynamic_cast<juce::Button*>(componentWithID(*editor,"tooltipLanguage"));
    for(int i=0;i<2;++i){language->onClick();require(language->getButtonText()==juce::String::fromUTF8(p.getEditorState().chinese?"中文":"EN"),"language button names current mode");}
    auto* range=componentWithID(*editor,"frequencyRange");auto* fft=componentWithID(*editor,"fftSize");
    require(componentWithID(*editor,"frequencyRangeLabel")->getX()==componentWithID(*editor,"fftSizeLabel")->getX(),"Range label aligns with FFT label");
    require(range->getX()==fft->getX()&&range->getWidth()==fft->getWidth()&&range->getHeight()==fft->getHeight(),"Range dropdown aligns with FFT dropdown");
    fitSpectrum(*view);tickUI();
    box->setNormalisedBounds({.32f,.22f,.23f,.44f});if(!box->isVisible())visibility->onClick();
    auto paint=[&]{return editor->createComponentSnapshot(editor->getLocalBounds());};
    auto bounds=[&]{paint();auto& v=view->getProperties();return juce::Rectangle<float>((float)v["selectedX"],(float)v["selectedY"],(float)v["selectedW"],(float)v["selectedH"]);};
    const auto started=juce::Time::getMillisecondCounterHiRes();print->onClick();const double feedbackMs=juce::Time::getMillisecondCounterHiRes()-started;
    require(!box->isVisible()&&(bool)view->getProperties()["previewActive"],"Print immediately hides source and shows simulation");
    png(dir.getChildFile("147-print-progress.png"),paint());
    require((float)view->getProperties()["renderProgress"]>=0&&(float)view->getProperties()["renderProgress"]<1,"pending Print shows bounded real progress");
    require(p.recording().flush(),"147 Print completes");const auto renderMs=juce::Time::getMillisecondCounterHiRes()-started;
    for(int i=0;i<5&&(bool)view->getProperties()["previewActive"];++i)tickUI();
    require(!(bool)view->getProperties()["previewActive"],"matching final raster replaces simulation");
    auto original=p.recording().snapshot();require(original->edits.size()==1,"one stamped image");const auto edit=original->edits[0];
    const auto baseBounds=bounds();const auto sourceBounds=box->getBounds();
    // Range and scale are destructive edit controls only while a Print is selected.
    auto* rangeControl=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"frequencyRange"));
    auto* scaleControl=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"fftScale"));
    for(auto* control:{rangeControl,scaleControl}){
        const int beforeChoice=control->getSelectedId();control->setSelectedId(control==rangeControl?1:2,juce::sendNotificationSync);
        juce::Thread::sleep(210);tickUI();require(p.recording().flush(),"148 selected mapping render");tickUI();
        const auto mapped=p.recording().snapshot();const auto& next=mapped->edits.front();
        require(mapped->revision>original->revision&&(next.low!=edit.low||next.high!=edit.high),"148 selected display mapping changes audio frequency bounds");
        require(next.start==edit.start&&next.duration==edit.duration,"148 mapping preserves Print time placement");
        const auto mappedBounds=bounds();require(std::abs(mappedBounds.getHeight()-baseBounds.getHeight())<.1f&&std::abs(mappedBounds.getWidth()-baseBounds.getWidth())<.1f,"148 remapping keeps visual Print aspect");
        editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),0));require(p.recording().flush(),"148 mapping undo");tickUI();
        require(control->getSelectedId()==beforeChoice&&sameAudioEdit(p.recording().edits()->front(),edit),"148 mapping undo restores control and edit");
        for(int64_t i=original->first;i<original->last;i+=29)require(p.recording().snapshot()->sample(0,i)==original->sample(0,i),"148 mapping undo restores audio exactly");
        original=p.recording().snapshot();
    }

    auto wheel=[&](int modifiers,float delta){auto point=bounds().getCentre();const auto now=juce::Time::getCurrentTime();juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(modifiers),1,0,0,0,0,view,view,now,point,now,0,false);juce::MouseWheelDetails w;w.deltaY=delta;view->mouseWheelMove(e,w);tickUI();};
    auto click=[&]{auto point=bounds().getCentre();view->mouseDown(mouseAt(*view,point));view->mouseUp(mouseAt(*view,point));tickUI();};
    for(int modifier:{juce::ModifierKeys::ctrlModifier,juce::ModifierKeys::shiftModifier}){
        wheel(modifier,.2f);juce::Thread::sleep(210);tickUI();
        require(p.recording().snapshot()->revision==original->revision&&sameAudioEdit(p.recording().edits()->front(),edit),"selected modifier wheel never changes audio or duration");
        const auto stretched=bounds();require(stretched.getWidth()>baseBounds.getWidth()*1.1f&&stretched.getWidth()<baseBounds.getWidth()*1.3f&&std::abs(stretched.getHeight()-baseBounds.getHeight())<.01f,"modifier wheel gently stretches display horizontally");
        require(std::abs(box->getWidth()-sourceBounds.getWidth())<=1&&std::abs(box->getHeight()-sourceBounds.getHeight())<=1,"wheel keeps source image dimensions unchanged");
        click();const auto restored=bounds();require(std::abs(restored.getWidth()-baseBounds.getWidth())<.1f&&std::abs(restored.getHeight()-baseBounds.getHeight())<.1f,"single click on already selected Print restores original aspect and size");
        require(p.recording().snapshot()->revision==original->revision,"restoring aspect is display only");
    }
    // Unrelated parameter edits after a wheel stretch must not commit that stretch.
    wheel(juce::ModifierKeys::ctrlModifier,.2f);ratio->setValue(.12,juce::sendNotificationSync);juce::Thread::sleep(210);tickUI();require(p.recording().flush(),"ratio after wheel");tickUI();
    require(p.recording().edits()->front().duration==edit.duration&&p.recording().edits()->front().speed==edit.speed,"ratio edit preserves actual duration after display stretch");
    click();auto beforeSpeed=p.recording().snapshot();speed->onDragStart();speed->setValue(1.6,juce::sendNotificationSync);tickUI();
    auto simulated=bounds();require(p.recording().snapshot()->revision==beforeSpeed->revision,"slider simulation does not render before release");
    require(std::abs(simulated.getWidth()-box->getWidth())<2&&std::abs(simulated.getHeight()-box->getHeight())<2,"Speed simulation and selected border share image geometry");
    png(dir.getChildFile("147-speed-preview.png"),paint());speed->onDragEnd();juce::Thread::sleep(210);tickUI();require(p.recording().flush(),"Speed release");tickUI();tickUI();
    const auto beforeMove=p.recording().snapshot();auto moving=beforeMove->edits.front();require(std::abs(moving.duration-edit.duration/1.6)<.001,"slider changes true Print duration");
    // Move the actual selected region: no engine mutation until the release.
    const auto down=bounds().getCentre();view->mouseDown(mouseAt(*view,down));auto b=bounds();
    auto drag=mouseAt(*view,down.translated(95,35));view->mouseDrag(drag);tickUI();auto ghost=bounds();
    require(p.recording().snapshot()->revision==beforeMove->revision&&sameAudioEdit(p.recording().edits()->front(),moving),"Print drag leaves audio untouched before release");
    require(std::abs(ghost.getX()-b.getX()-95)<1&&std::abs(ghost.getY()-b.getY()-35)<1,"drag ghost and border follow pointer");
    png(dir.getChildFile("147-move-preview.png"),paint());view->mouseUp(drag);require(p.recording().flush(),"move commits");tickUI();tickUI();
    const auto moved=p.recording().snapshot();auto placedBounds=bounds();require(std::abs(placedBounds.getX()-ghost.getX())<1&&std::abs(placedBounds.getY()-ghost.getY())<1,"move release does not jump the viewport");require(moved->edits.size()==1&&moved->edits[0].id==moving.id&&moved->edits[0].start>moving.start&&moved->edits[0].high<moving.high,"move replaces same Print at new time and frequency");
    require(editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),0)),"move undo");require(p.recording().flush(),"move undo render");tickUI();
    require(sameAudioEdit(p.recording().edits()->front(),moving),"one undo restores entire move including noise seed");
    for(int64_t i=beforeMove->first;i<beforeMove->last;i+=17)require(beforeMove->sample(0,i)==p.recording().snapshot()->sample(0,i),"move undo restores exact prior audio and noise");
    editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier),0));require(p.recording().flush(),"move redo render");tickUI();
    const auto revision=p.recording().snapshot()->revision;const auto repeatStart=juce::Time::getMillisecondCounterHiRes();print->onClick();require(p.recording().flush(),"same Print no-op");
    require(p.recording().snapshot()->revision==revision,"identical Print skips DSP and snapshot rebuild");
    std::cout<<"147_TIMING immediate_feedback_ms="<<feedbackMs<<" render_and_snapshot_ms="<<renderMs<<" repeat_print_ms="<<juce::Time::getMillisecondCounterHiRes()-repeatStart<<"\n";
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));require(p.recording().flush(),"delete moved noise");tickUI();
    auto cleared=p.recording().snapshot();require(!cleared->originalSpectrum.empty(),"148 original spectrum is retained alongside original audio");
    for(size_t t=0;t<cleared->spectrum.size();++t)require(cleared->spectrum[t]==cleared->originalSpectrum[t],"148 independent Delete directly restores cached original spectrum tiles");for(int64_t i=cleared->first;i<cleared->last;++i)require(cleared->sample(0,i)==cleared->sample(0,i,false),"delete moved Print removes all noise and restores original samples");
    // Latest UX: full view spans only the recording, retaining internal gaps.
    SpectrumTagAudioProcessor late;Transport lateHost;lateHost.playing=false;late.setPlayHead(&lateHost);late.setRateAndBufferSizeDetails(48000,256);late.prepareToPlay(48000,256);
    juce::AudioBuffer<float> lateAudio(2,48000);lateAudio.clear();late.recording().setArmed(true);late.recording().capture(lateAudio,60,48000,true);late.recording().capture(lateAudio,89,48000,true);late.recording().setArmed(false);require(late.recording().flush(),"late-only capture");
    std::unique_ptr<juce::AudioProcessorEditor> lateEditor(late.createEditor());tickUI();auto* lateView=componentWithID(*lateEditor,"spectrumView");fitSpectrum(*lateView);tickUI();
    require(std::abs((double)lateView->getProperties()["displayStart"]-60)<1e-6&&std::abs((double)lateView->getProperties()["displayEnd"]-90)<1e-6,"fit spans first through last recording sample");
    require((double)lateView->getProperties()["overviewStart"]==60&&(double)lateView->getProperties()["overviewEnd"]==90,"overview uses the same captured range");
    require(late.recording().snapshot()->start()==60&&late.recording().snapshot()->end()==90&&!late.recording().snapshot()->contains(10*48000),"fit neither pads audio nor shifts captured positions");
    for(int i=0;i<3;++i)tickUI();png(dir.getChildFile("147-late-recording-overview.png"),lateEditor->createComponentSnapshot(lateEditor->getLocalBounds()));
    // Placement mode prints when a visible image exists; hide it to test spectrum seeking.
    auto* lateEye=dynamic_cast<juce::Button*>(componentWithID(*lateEditor,"imageVisibility"));
    if(imageBox(*lateEditor)->isVisible())lateEye->onClick();tickUI();
    lateView->mouseDown(mouseAt(*lateView,{70+(lateView->getWidth()-70)*.75f,(float)lateView->getHeight()-50}));lateHost.playing=true;juce::MidiBuffer lateMidi;juce::AudioBuffer<float> lateBlock(2,256);lateBlock.clear();late.processBlock(lateBlock,lateMidi);
    require(std::abs(late.audition().position())<.001&&!late.audition().isLocal(),"stopped overview gap click follows project cursor on next Play");
    auto clickSpectrum=[&](double seconds){fitSpectrum(*lateView);tickUI();const auto point=juce::Point<float>(70+(lateView->getWidth()-70)*(float)((seconds-60)/30),100);lateView->mouseDown(mouseAt(*lateView,point));lateView->mouseUp(mouseAt(*lateView,point));};
    lateHost.playing=false;lateHost.sample=5*48000;late.processBlock(lateBlock,lateMidi);
    clickSpectrum(60.5);lateHost.playing=true;late.processBlock(lateBlock,lateMidi);
    require(std::abs(late.audition().position()-60.5)<.001&&late.audition().isLocal(),"next Play starts at clicked captured silence");
    lateHost.playing=false;late.processBlock(lateBlock,lateMidi);clickSpectrum(60.5);clickSpectrum(70);
    lateHost.playing=true;late.processBlock(lateBlock,lateMidi);
    require(std::abs(late.audition().position()-5)<.001&&!late.audition().isLocal(),"uncaptured click cancels previous pending seek and next Play uses DAW cursor");
    lateView->mouseDown(mouseAt(*lateView,{70+(lateView->getWidth()-70)*((89.5f-60)/30),(float)lateView->getHeight()-50}));late.processBlock(lateBlock,lateMidi);
    require(std::abs(late.audition().position()-89.5)<.001&&late.audition().isLocal(),"overview recorded click retains absolute DAW time");
    lateView->mouseDown(mouseAt(*lateView,{70+(lateView->getWidth()-70)*.75f,(float)lateView->getHeight()-50}));late.processBlock(lateBlock,lateMidi);
    require(std::abs(late.audition().position()-82.5)<.001&&late.audition().isLocal(),"overview gap click during playback retains independent local position");
    lateHost.playing=false;
    for(int seconds:{0,120}){lateHost.sample=seconds*48000;late.processBlock(lateBlock,lateMidi);tickUI();fitSpectrum(*lateView);tickUI();
        auto screen=lateEditor->createComponentSnapshot(lateEditor->getLocalBounds());
        require(std::abs((double)lateView->getProperties()["cursorTime"]-seconds)<1e-6,"cursor keeps DAW time outside recorded overview");
        require((int)lateView->getProperties()["overviewCursorSide"]==(seconds==0?-1:1),"offscreen arrow identifies actual cursor side");
        require(std::abs((double)lateView->getProperties()["displayStart"]-60)<1e-6&&std::abs((double)lateView->getProperties()["displayEnd"]-90)<1e-6,"outside cursor does not alter fitted recording range");
        if(seconds==120)png(dir.getChildFile("147-outside-recording-cursor.png"),screen);
    }
    {const auto point=juce::Point<float>(400,200);auto e=mouseAt(*lateView,point);juce::MouseWheelDetails w;w.deltaY=-.5f;lateView->mouseWheelMove(e,w);tickUI();require((double)lateView->getProperties()["displayEnd"]>90,"wheel can browse beyond recording end");}
    // Two recorded clips separated by a hole: host playback and local audition
    // must choose different sources for that same uncaptured interval.
    SpectrumTagAudioProcessor holes;Transport holesHost;holes.setPlayHead(&holesHost);holes.setRateAndBufferSizeDetails(48000,256);holes.prepareToPlay(48000,256);
    juce::AudioBuffer<float> clip(2,48000);for(int c=0;c<2;++c)for(int i=0;i<48000;++i)clip.setSample(c,i,.4f);
    holes.recording().setArmed(true);holes.recording().capture(clip,0,48000,true);for(int c=0;c<2;++c)for(int i=0;i<48000;++i)clip.setSample(c,i,.6f);holes.recording().capture(clip,2,48000,true);holes.recording().setArmed(false);require(holes.recording().flush(),"two-clip audition fixture");
    PrintEdit attenuation;attenuation.start=0;attenuation.duration=1;attenuation.low=0;attenuation.high=1;attenuation.rows=65;attenuation.cols=20;attenuation.ratio=.2f;attenuation.mask=std::make_shared<const std::vector<float>>(65*20,1.f);holes.recording().add(attenuation);require(holes.recording().flush(),"render first audition clip");
    juce::MidiBuffer holesMidi;juce::AudioBuffer<float> input(2,256);
    auto processHole=[&]{for(int c=0;c<2;++c)for(int i=0;i<256;++i)input.setSample(c,i,.37f);holes.processBlock(input,holesMidi);holesHost.sample+=256;};
    auto hostAt=[&](double t){holes.audition().requestHostFollow();holesHost.sample=llround(t*48000);holesHost.playing=true;for(int k=0;k<25;++k)processHole();};
    hostAt(.4);require(std::abs(input.getSample(0,200)-.08f)<2e-5,"host-aligned first clip plays rendered audio");
    hostAt(1.4);require(std::abs(input.getSample(0,200)-.37f)<1e-6,"host-aligned gap passes original DAW input");
    hostAt(2.4);require(std::abs(input.getSample(0,200)-.6f)<1e-6,"host-aligned later clip plays recorded audio");
    hostAt(8);holes.auditionAt(1.25);processHole();require(holes.audition().isLocal()&&std::abs(input.getSample(0,200))<1e-6,"playing gap click silences unrelated DAW audio immediately");
    for(int k=0;k<140;++k)processHole();require(std::abs(input.getSample(0,200))<1e-6,"local gap stays silent");
    for(int k=0;k<75;++k)processHole();require(std::abs(input.getSample(0,200)-.6f)<1e-6,"local audition resumes when reaching next recorded clip");
    holesHost.playing=false;holesHost.sample=48000;processHole();holesHost.playing=true;for(int k=0;k<30;++k)processHole();
    require(!holes.audition().isLocal()&&std::abs(input.getSample(0,200)-.37f)<1e-6,"Stop cancels local audition and normal gap passthrough resumes");
    // Exercise the lifecycle permutations used by hosts around an offline freeze.
    for(int route=0;route<4;++route){SpectrumTagAudioProcessor frozen;Transport host;frozen.setPlayHead(&host);frozen.setRateAndBufferSizeDetails(48000,8192);frozen.prepareToPlay(48000,8192);frozen.recording().setArmed(true);
        frozen.releaseResources();frozen.setNonRealtime(true);frozen.prepareToPlay(48000,8192);require(frozen.recording().isArmed(),"arming survives suspend before freeze");
        juce::AudioBuffer<float> audio(2,8192);juce::MidiBuffer midi;
        for(int block=0;block<12;++block){for(int c=0;c<2;++c)for(int i=0;i<8192;++i)audio.setSample(c,i,.1f+block*.01f);host.sample=block*8192;frozen.processBlock(audio,midi);}
        if(route==0)frozen.releaseResources();
        if(route==1){frozen.setNonRealtime(false);frozen.prepareToPlay(48000,8192);}
        if(route==2){frozen.setNonRealtime(false);host.sample=12*8192;frozen.processBlock(audio,midi);}
        if(route==3){juce::Thread::sleep(1100);frozen.pollTransportTimeout();}
        require(!frozen.recording().isArmed(),"freeze completion disarms on release, prepare, realtime transition or idle timeout");
        require(frozen.recording().flush(),"freeze tail flush");auto complete=frozen.recording().snapshot();require(complete&&complete->last==12*8192&&std::abs(complete->sample(0,12*8192-1,false)-.21f)<1e-6,"freeze retains complete final capture packet");
        frozen.setNonRealtime(false);host.sample=12*8192;frozen.processBlock(audio,midi);require(frozen.recording().flush()&&frozen.recording().snapshot()->last==12*8192,"unfreeze playback does not resume capture");
    }
    std::cout<<"1.4.7 CHECKS PASSED; view-only wheel/reselection, slider border, move/undo/noise, progress, freeze lifecycle\n";
}
