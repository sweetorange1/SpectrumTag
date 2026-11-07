#pragma once
// Exercise the user-visible path: decode a file, click Print, hide the source,
// and inspect the completed spectrum while the host is stopped.
static void revision1427Checks(const juce::File& dir){
    dir.createDirectory();
    juce::Image art(juce::Image::RGB,160,240,true);
    {juce::Graphics g(art);g.fillAll(juce::Colours::white);g.setColour(juce::Colours::black);g.fillRect(40,60,80,120);}
    const auto imageFile=dir.getChildFile("display-pattern.png");png(imageFile,art);
    for(int inputCase:{0,1,2}){
        const bool oppositePhase=inputCase==2;
        SpectrumTagAudioProcessor p;Transport host;host.playing=false;p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
        juce::AudioBuffer<float> input(2,48000*4);input.clear();
        if(oppositePhase){std::mt19937 random(1427);std::uniform_real_distribution<float> noise(-.12f,.12f);for(int i=0;i<input.getNumSamples();++i){const float n=noise(random);input.setSample(0,i,n);input.setSample(1,i,-n);}}
        if(inputCase==1){std::mt19937 random(2727);std::uniform_real_distribution<float> noise(-.035f,.035f);for(int i=0;i<input.getNumSamples();++i)for(int c=0;c<2;++c){const double t=i/48000.;input.setSample(c,i,noise(random)+.08f*std::sin(juce::MathConstants<double>::twoPi*(220+55*c)*t)+.04f*std::sin(juce::MathConstants<double>::twoPi*(3000+100*c)*t));}}
        p.recording().setArmed(true);p.recording().capture(input,0,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"1427 capture");
        auto state=p.getEditorState();state.imagePath=imageFile.getFullPathName();state.imageVisible=true;state.palette=7;state.viewValid=false;p.setEditorState(state);
        set(p,ParameterIDs::noiseFill,inputCase==0?.75f:.25f);set(p,ParameterIDs::frequencyPreset,3);set(p,ParameterIDs::fftScale,0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
        auto wait=[&](auto condition,const char* message){const auto until=juce::Time::getMillisecondCounter()+20000;do{juce::Timer::callPendingTimersSynchronously();if(condition())return;juce::Thread::sleep(5);}while((int)(until-juce::Time::getMillisecondCounter())>0);throw std::runtime_error(message);};
        auto ready=[&]{auto s=p.recording().snapshot();return s&&!p.recording().isBusy()&&(int)view->getProperties()["rasterRevision"]==s->revision&&!(bool)view->getProperties()["previewActive"];};
        wait(ready,"1427 source raster");fitSpectrum(*view);wait(ready,"1427 Fit raster");
        box->setNormalisedBounds({.2f,.17f,.2f,.55f});
        auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));print->onClick();
        wait([&]{return p.recording().edits()->size()==1&&ready();},"1427 completed Print raster must arrive without playback or reopening");
        require(!box->isVisible(),"1427 source hidden after Print");
        auto shot=view->createComponentSnapshot(view->getLocalBounds());
        const auto bounds=juce::Rectangle<float>((float)view->getProperties()["selectedX"],(float)view->getProperties()["selectedY"],(float)view->getProperties()["selectedW"],(float)view->getProperties()["selectedH"]);
        double bright=0,dark=0;int count=0;
        for(int y=35;y<65;++y)for(int x=10;x<18;++x){const int py=juce::roundToInt(bounds.getY()+bounds.getHeight()*y/100);bright+=shot.getPixelAt(juce::roundToInt(bounds.getX()+bounds.getWidth()*x/100),py).getBrightness();dark+=shot.getPixelAt(juce::roundToInt(bounds.getX()+bounds.getWidth()*.5f),py).getBrightness();++count;}
        const auto label=inputCase==0?"silence":inputCase==1?"normal-stereo":"opposite-phase";png(dir.getChildFile(juce::String(label)+"-printed.png"),shot);
        const auto wet=p.recording().snapshot();double delta=0;for(int64_t i=wet->first;i<wet->last;i+=23)delta+=std::abs(wet->sample(0,i)-wet->sample(0,i,false));
        std::cout<<"1427 "<<label<<" audio_delta="<<delta<<" visible_bright="<<bright/count<<" dark="<<dark/count<<"\n"<<std::flush;
        require(delta>.01,"1427 Print changes exported channel samples");
        require((bright-dark)/count>.15,"1427 completed spectrum must visibly show the same Print as exported audio");
        auto* diagnosticEditor=dynamic_cast<SpectrumTagAudioProcessorEditor*>(editor.get());require(diagnosticEditor!=nullptr,"1427 diagnostics editor");
        const auto before=p.recording().snapshot();const auto passes=p.recording().printPasses();const auto requested=view->getProperties()["rasterApplied"];
        const auto report=diagnosticEditor->diagnosticReport();const auto json=juce::JSON::toString(report);
        require(report["prints"].size()==1&&(double)report["prints"][0]["audioChangeRms"]>0&&(double)report["prints"][0]["spectrumMeanAbsoluteChange"]>0,"1427 diagnostic identifies changed audio and spectrum");
        require((bool)report["viewProbe"]["waterfallMatchesViewSource"]&&report["viewProbe"]["printRasterProbes"].size()==1,"1427 diagnostic records real raster source");
        require(!json.contains(imageFile.getFileName())&&!json.contains(imageFile.getFullPathName())&&!json.contains(before->sourceId)&&!json.contains(before->edits.front().id),"1427 diagnostic excludes source paths names and identifiers");
        require(p.recording().snapshot()==before&&p.recording().printPasses()==passes&&view->getProperties()["rasterApplied"]==requested,"1427 diagnostic does not trigger rendering or alter audio");
        require(spectrumtag::diagnostics::write(dir.getChildFile(juce::String(label)+"-diagnostics.json"),report),"1427 diagnostic export");
        require(componentWithID(*editor,"exportDiagnostics")!=nullptr,"1427 export button available");
        if(oppositePhase){
            // The ARA presentation path uses the same already-rendered pages.
            spectrumtag::RecordingEngine araView(true);araView.present(wet);require(araView.flush(),"1427 ARA display ready");
            const double t=wet->edits.front().start+wet->edits.front().duration*.1;
            require(araView.snapshot()->frameAt(t)->level==wet->frameAt(t)->level,"1427 ARA and recorded display agree");
            // Simulate a 1.4.26 derived cache with obsolete, cancelled pixels.
            // Correct audio must be reused; only its two spectra are rebuilt.
            const auto saved=p.recording().save();auto stale=*wet;
            for(auto* tiles:{&stale.spectrum,&stale.originalSpectrum})for(auto& tile:*tiles){auto empty=std::make_shared<spectrumtag::RecordingSnapshot::Tile>(*tile);for(auto& f:empty->frames)f.level.fill(0);tile=empty;}
            spectrumtag::RenderCache cache;const auto key=cache.key(stale,false);require(cache.save(key,stale,[]{return false;}),"1427 legacy cache fixture");
            const auto file=spectrumtag::RenderCache::file(key);juce::MemoryBlock compressed;require(file.loadFileAsData(compressed),"1427 read cache");
            juce::MemoryInputStream raw(compressed.getData(),compressed.getSize()-32,false);juce::GZIPDecompressorInputStream unzip(raw);juce::MemoryOutputStream body;body.writeFromInputStream(unzip,-1);
            juce::MemoryOutputStream old;{juce::GZIPCompressorOutputStream zip(old,1);zip.writeInt(0x53524331);zip.write((const char*)body.getData()+4,body.getDataSize()-4);zip.flush();}
            const auto hash=juce::SHA256(old.getData(),old.getDataSize()).getRawData();old.write(hash.getData(),hash.getSize());require(file.replaceWithData(old.getData(),old.getDataSize()),"1427 write legacy cache");
            spectrumtag::RecordingEngine restored;restored.restore(saved,true);require(restored.flush(30000),"1427 migrate display cache");
            require(restored.cacheHits()==1&&restored.printPasses()==0&&restored.analysisPasses()>0,"1427 old display cache refresh skips audio Print");
            require(restored.snapshot()->frameAt(t)->level==wet->frameAt(t)->level,"1427 old cache regains visible Print");
            for(int64_t i=wet->first;i<wet->last;i+=23)for(int c=0;c<2;++c)require(restored.snapshot()->sample(c,i)==wet->sample(c,i),"1427 cache migration leaves audio bit exact");
            spectrumtag::RecordingEngine again;again.restore(saved,true);require(again.flush()&&again.cacheHits()==1&&again.printPasses()==0&&again.analysisPasses()==0,"1427 subsequent reopen needs no render or FFT");
        }
    }
    spectrumtag::diagnostics::History history;for(int i=0;i<140;++i)history.add(i);require(history.json().size()==120&&(int)history.json()[0]==20,"1427 diagnostic history stays bounded");
    SpectrumTagAudioProcessor live;Transport host;live.setPlayHead(&host);live.setRateAndBufferSizeDetails(48000,256);live.prepareToPlay(48000,256);live.notifyEditorAttached();
    juce::MidiBuffer midi;for(int block=0;block<60;++block){juce::AudioBuffer<float> b(2,256);for(int i=0;i<256;++i){const float value=.1f*std::sin((host.sample+i)*juce::MathConstants<double>::twoPi*6000/48000);b.setSample(0,i,value);b.setSample(1,i,-value);}live.processBlock(b,midi);host.sample+=256;}
    std::vector<spectrumtag::SpectrumFrame> frames;live.drainSpectrumFrames(frames);require(!frames.empty()&&frames.back().level[512]>100,"1427 realtime display preserves opposite-polarity stereo");live.notifyEditorDetached();
    std::cout<<"PASS 1.4.27 stopped Print visibility, stereo power, ARA presentation, live display and display-only cache migration\n"<<std::flush;
}
