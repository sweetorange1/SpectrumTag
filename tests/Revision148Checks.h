#pragma once
static void revision148Checks(const juce::File& dir){
    using namespace spectrumtag;
    SpectrumTagAudioProcessor p;Transport host;p.setPlayHead(&host);p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    // 2026-11-12：显示上限下拉框隐藏，新实例默认 22.05 kHz。
    require(*p.getAPVTS().getRawParameterValue(ParameterIDs::frequencyRange)==2,"148 fresh instance defaults to 22.05 kHz");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
    auto* clear=dynamic_cast<juce::Button*>(componentWithID(*editor,"clearRecord"));
    // 图片可见性由三态决定，没有眼睛按钮；右键的新语义（打印 / 退回图片 /
    // 聚焦最近的打印）由 2.0.1 的右键用例覆盖。
    if(!box->hasImage()){dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();}
    require(box->isVisible(),"148 a dropped image is visible without a toggle");
    auto* fit=componentWithID(*view,"fitSpectrum");require(fit&&fit->getRight()>view->getWidth()-12&&fit->getY()<12,"148 Fit icon sits in spectrum top right");
    juce::MidiBuffer midi;juce::AudioBuffer<float> block(2,256);std::mt19937 rng(148);std::uniform_real_distribution<float> noise(-.15f,.15f);
    auto play=[&]{for(int c=0;c<2;++c)for(int i=0;i<256;++i)block.setSample(c,i,noise(rng));p.processBlock(block,midi);host.sample+=256;};
    for(int i=0;i<100;++i)play();tickUI();tickUI();
    require(!p.getSpectrumHistory()->frames.empty(),"148 live display has input history");
    auto live=editor->createComponentSnapshot(editor->getLocalBounds());png(dir.getChildFile("148-live.png"),live);
    p.recording().setArmed(true);juce::AudioBuffer<float> source(2,48000*3);
    for(int c=0;c<2;++c)for(int i=0;i<source.getNumSamples();++i)source.setSample(c,i,.12f*std::sin(i*.19));
    p.recording().capture(source,12,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"148 captured source");tickUI();fitSpectrum(*view);tickUI();
    const auto saved=p.recording().snapshot();
    // A captured tail still waiting in the queue must survive Clear + Undo.
    juce::AudioBuffer<float> tail(2,256);for(int c=0;c<2;++c)for(int i=0;i<256;++i)tail.setSample(c,i,.314f);
    p.recording().setArmed(true);p.recording().capture(tail,15,48000);p.recording().setArmed(false);
    const auto started=juce::Time::getMillisecondCounterHiRes();clear->onClick();
    const auto clearMs=juce::Time::getMillisecondCounterHiRes()-started;
    require(!p.recording().isEnabled()&&!p.recording().isArmed(),"148 Clear returns to live mode immediately");
    require(p.getSpectrumHistory()->frames.empty(),"148 Clear removes live spectrum history as well as recording");
    require(!(bool)view->getProperties()["previewActive"],"148 Clear removes pending simulation");
    auto empty=view->createComponentSnapshot(view->getLocalBounds());
    for(int y=50;y<400;y+=17)for(int x=100;x<view->getWidth()-50;x+=19)require(empty.getPixelAt(x,y)==juce::Colours::black,"148 Clear paints black immediately without old raster");
    // Completion/undo history belongs to processor even if the editor is closed.
    editor.reset();require(p.recording().flush(),"148 Clear worker completes without editor");
    editor.reset(p.createEditor());tickUI();view=componentWithID(*editor,"spectrumView");box=imageBox(*editor);
    require(!(bool)view->getProperties()["recordMode"]&&!p.recording().snapshot(),"148 reopened view remains conventional/live");
    empty=view->createComponentSnapshot(view->getLocalBounds());
    for(int y=50;y<400;y+=17)for(int x=100;x<view->getWidth()-50;x+=19)require(empty.getPixelAt(x,y)==juce::Colours::black,"148 cancelled raster and queued old FFT frames cannot reappear");
    png(dir.getChildFile("148-cleared.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),0));require(p.recording().flush(),"148 undo Clear");tickUI();
    auto restored=p.recording().snapshot();require(restored&&restored->contains(15*48000+255)&&std::abs(restored->sample(0,15*48000+255)-.314f)<1e-7,"148 Undo retains final queued capture samples");
    for(int64_t i=saved->first;i<saved->last;i+=17)require(restored->sample(0,i)==saved->sample(0,i),"148 Undo restores complete original recording");
    fitSpectrum(*view);tickUI();const double end=(double)view->getProperties()["displayEnd"],span=end-(double)view->getProperties()["displayStart"];
    juce::MouseWheelDetails wheel;wheel.deltaY=.2f;view->mouseWheelMove(mouseAt(*view,{400,200}),wheel);
    tickUI();double first=(double)view->getProperties()["displayEnd"],target=(double)view->getProperties()["panTarget"];
    require(std::abs((end-target)/span-.075)<1e-5,"148 wheel travels one quarter of old distance");
    require(first<end&&first>target,"148 wheel eases rather than jumping directly");
    for(int i=0;i<6;++i)tickUI();require(std::abs((double)view->getProperties()["displayEnd"]-target)<span*.001,"148 wheel settles without ongoing drift");
    fitSpectrum(*view);tickUI();require(!(bool)view->getProperties()["panActive"],"148 Fit cancels wheel transition");
    png(dir.getChildFile("148-recorded.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    std::cout<<"148_CLEAR immediate_ms="<<clearMs<<" worker_raster_ms="<<(double)view->getProperties()["rasterMs"]<<"\n";
    // Sustain live requests faster than a raster can finish: completion must progress.
    ViewRenderer renderer;ViewRenderer::Request request;request.width=1600;request.height=900;request.start=0;request.end=10;request.top=20000;
    for(int i=0;i<1000;++i){auto f=std::make_shared<SpectrumFrame>();f->time=i*.01;f->interval=.01;f->bins=2049;for(int k=0;k<2049;++k)f->level[k]=(juce::uint8)((i+k)%256);request.live.push_back(f);}
    uint64_t last=0;int completed=0;double worst=0;const auto until=juce::Time::getMillisecondCounterHiRes()+350;
    while(juce::Time::getMillisecondCounterHiRes()<until){renderer.request(request);if(auto r=renderer.result();r&&r->generation!=last){++completed;last=r->generation;worst=juce::jmax(worst,r->milliseconds);}juce::Thread::sleep(1);}
    require(completed>=3,"148 continuous newer raster requests do not starve completed frames");
    renderer.invalidate();juce::Thread::sleep(40);require(!renderer.result(),"148 invalidate rejects old in-flight raster");
    std::cout<<"148_DISPLAY continuous_completed="<<completed<<" max_raster_ms="<<worst<<" at 1600x900\n";
    RecordingEngine superseded;juce::AudioBuffer<float> longSource(2,48000*8);
    for(int c=0;c<2;++c)for(int i=0;i<longSource.getNumSamples();++i)longSource.setSample(c,i,.02f*std::sin(i*.17));
    superseded.setArmed(true);superseded.capture(longSource,0,48000,true);superseded.setArmed(false);require(superseded.flush(),"148 supersede source");
    PrintEdit stamp;stamp.start=.1;stamp.duration=7;stamp.rows=129;stamp.cols=256;stamp.compensation=.6f;stamp.low=.1f;stamp.high=.8f;
    auto mask=std::make_shared<std::vector<float>>((size_t)stamp.rows*stamp.cols);for(size_t i=0;i<mask->size();++i)(*mask)[i]=(i%7)<4?1.f:0.f;stamp.mask=mask;
    const auto began=juce::Time::getMillisecondCounterHiRes();superseded.add(stamp);
    for(int i=0;i<24;++i){stamp.ratio=.01f*i;superseded.add(stamp,stamp.id);juce::Thread::sleep(2);}
    require(superseded.flush(30000),"148 superseded requests finish");
    require(sameAudioEdit(superseded.snapshot()->edits.front(),stamp)&&superseded.renderProgress()==1&&superseded.renderStage()==RecordingEngine::RenderStage::ready,"148 only latest edit completes with ready progress");
    const auto preDelete=superseded.snapshot();const auto deletion=juce::Time::getMillisecondCounterHiRes();superseded.remove(stamp.id);require(superseded.flush(),"148 independent delete");
    auto unprinted=superseded.snapshot();
    for(size_t i=0;i<unprinted->spectrum.size();++i)require(unprinted->spectrum[i]==preDelete->originalSpectrum[i],"148 Delete reuses pre-Print spectrum without recomputing FFT");
    for(int64_t i=0;i<8*48000;i+=19)require(unprinted->sample(0,i)==unprinted->sample(0,i,false),"148 Delete removes all noise and restores original audio");
    std::cout<<"148_SUPERSEDE total_ms="<<deletion-began<<" delete_restore_ms="<<juce::Time::getMillisecondCounterHiRes()-deletion<<"\n";
    std::cout<<"1.4.8 CHECKS PASSED; Clear/live/history/tail/Undo, smooth wheel, three image states, Fit icon, 20 kHz default and renderer progress\n";
}
