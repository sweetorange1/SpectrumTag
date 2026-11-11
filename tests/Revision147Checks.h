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
    auto* view=componentWithID(*editor,"spectrumView");    auto* box=imageBox(*editor);
    auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));
    auto* speed=dynamic_cast<juce::Slider*>(componentWithID(*editor,"speed"));
    auto* ratio=dynamic_cast<juce::Slider*>(componentWithID(*editor,"amplitudeRatio"));
    auto* language=dynamic_cast<juce::Button*>(componentWithID(*editor,"tooltipLanguage"));
    for(int i=0;i<2;++i){language->onClick();require(language->getButtonText()==juce::String::fromUTF8(p.getEditorState().chinese?"中文":"EN"),"language button names current mode");}
    auto* range=componentWithID(*editor,"frequencyRange");auto* fft=componentWithID(*editor,"fftSize");
    require(componentWithID(*editor,"frequencyRangeLabel")->getX()==componentWithID(*editor,"fftSizeLabel")->getX(),"Range label aligns with FFT label");
    require(range->getX()==fft->getX()&&range->getWidth()==fft->getWidth()&&range->getHeight()==fft->getHeight(),"Range dropdown aligns with FFT dropdown");
    fitSpectrum(*view);tickUI();
    // 图片对象只来自用户拖入：先放一张，再把它摆到测试用的位置。
    dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    box->setNormalisedBounds({.32f,.22f,.23f,.44f});tickUI();
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
    // 打印后没有选中框，点击位置取自图片对象（=打印范围）。
    auto click=[&]{auto point=view->getLocalPoint(box,box->getLocalBounds().getCentre()).toFloat();view->mouseDown(mouseAt(*view,point));view->mouseUp(mouseAt(*view,point));tickUI();};
    // 新模型：点击已打印对象只聚焦（状态3）；修改任何渲染参数都会撤掉这次
    // 打印并退回未打印态（状态1），必须重新打印。
    click();
    require(!box->isVisible(),"focusing a Print shows only its frame");
    auto* rangeControl=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"frequencyRange"));
    const int beforeChoice=rangeControl->getSelectedId();
    rangeControl->setSelectedId(beforeChoice==1?2:1,juce::sendNotificationSync);
    juce::Thread::sleep(220);tickUI();
    require(p.recording().edits()->empty(),"editing a focused object removes its Print");
    require(box->isVisible()&&box->hasImage(),"object returns to the unprinted state");
    rangeControl->setSelectedId(beforeChoice,juce::sendNotificationSync);juce::Thread::sleep(220);tickUI();
    print->onClick();require(p.recording().flush(),"reprint after revert");tickUI();
    require(p.recording().snapshot()->edits.size()==1,"reprint creates a fresh Print");
    png(dir.getChildFile("147-reprinted.png"),paint());
    // 删除聚焦对象：原图与打印一起消失，并恢复缓存的原始频谱。
    click();
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));require(p.recording().flush(),"delete focused object");tickUI();
    auto cleared=p.recording().snapshot();
    require(cleared->edits.empty()&&!box->hasImage(),"Delete destroys the object and its Print");
    for(size_t t=0;t<cleared->spectrum.size();++t)require(cleared->spectrum[t]==cleared->originalSpectrum[t],"148 independent Delete directly restores cached original spectrum tiles");
    for(int64_t i=cleared->first;i<cleared->last;++i)require(cleared->sample(0,i)==cleared->sample(0,i,false),"Delete moved Print removes all noise and restores original samples");
    std::cout<<"147_TIMING immediate_feedback_ms="<<feedbackMs<<" render_ms="<<renderMs<<"\n";
    // Latest UX: full view spans only the recording, retaining internal gaps.
    SpectrumTagAudioProcessor late;Transport lateHost;lateHost.playing=false;late.setPlayHead(&lateHost);late.setRateAndBufferSizeDetails(48000,256);late.prepareToPlay(48000,256);
    juce::AudioBuffer<float> lateAudio(2,48000);lateAudio.clear();late.recording().setArmed(true);late.recording().capture(lateAudio,60,48000,true);late.recording().capture(lateAudio,89,48000,true);late.recording().setArmed(false);require(late.recording().flush(),"late-only capture");
    std::unique_ptr<juce::AudioProcessorEditor> lateEditor(late.createEditor());tickUI();auto* lateView=componentWithID(*lateEditor,"spectrumView");fitSpectrum(*lateView);tickUI();
    require(std::abs((double)lateView->getProperties()["displayStart"]-60)<1e-6&&std::abs((double)lateView->getProperties()["displayEnd"]-90)<1e-6,"fit spans first through last recording sample");
    require((double)lateView->getProperties()["overviewStart"]==60&&(double)lateView->getProperties()["overviewEnd"]==90,"overview uses the same captured range");
    require(late.recording().snapshot()->start()==60&&late.recording().snapshot()->end()==90&&!late.recording().snapshot()->contains(10*48000),"fit neither pads audio nor shifts captured positions");
    for(int i=0;i<3;++i)tickUI();png(dir.getChildFile("147-late-recording-overview.png"),lateEditor->createComponentSnapshot(lateEditor->getLocalBounds()));
    // 没有图片对象时画布显示投放引导；把它缩到左上角，免得挡住下面要点击的频谱位置。
    imageBox(*lateEditor)->setNormalisedBounds({.02f,.02f,.10f,.10f});tickUI();
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
