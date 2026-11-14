#pragma once
static void revision1411Checks(const juce::File& dir) {
    using namespace spectrumtag;
    {
        SpectrumTagAudioProcessor fresh;require(fresh.getFrequencyScale()==4,"1411 fresh instance defaults to Mel RX");
        auto* choice=fresh.getAPVTS().getParameter(ParameterIDs::frequencyPreset);require(choice->convertFrom0to1(choice->getDefaultValue())==3,"1411 host parameter default is Mel RX");
        std::unique_ptr<juce::AudioProcessorEditor> ui(fresh.createEditor());tickUI();auto* scale=dynamic_cast<juce::ComboBox*>(componentWithID(*ui,"fftScale"));require(scale&&scale->getSelectedId()==5&&scale->getText()=="Mel (RX)","1411 new editor displays Mel RX");
    }

    SpectrumTagAudioProcessor old;old.setRateAndBufferSizeDetails(48000,256);old.prepareToPlay(48000,256);
    juce::AudioBuffer<float> input(2,48000);for(int c=0;c<2;++c)for(int i=0;i<48000;++i)input.setSample(c,i,.08f*std::sin(i*.127f));
    old.recording().setArmed(true);old.recording().capture(input,0,48000,true);old.recording().setArmed(false);require(old.recording().flush(),"1411 compatibility source");
    PrintEdit e;e.start=.15;e.duration=.5;e.rows=513;e.cols=16;e.fftSize=1024;e.low=.05f;e.high=.9f;e.scaleMode=3;e.speed=3.3f;e.viewZoom=1.2;e.mask=std::make_shared<const std::vector<float>>(513*16,1.f);e.imagePath=dir.getChildFile("transparent-detail.png").getFullPathName();
    old.recording().add(e);require(old.recording().flush(),"1411 legacy Print fixture");auto before=old.recording().snapshot();
    set(old,ParameterIDs::frequencyPreset,2);set(old,ParameterIDs::speed,3.3f);auto state=old.getEditorState();state.selectedPrint=e.id;state.imagePath=e.imagePath;state.viewValid=true;state.viewFollow=false;state.viewEnd=1;state.viewZoom=e.viewZoom;old.setEditorState(state);
    juce::MemoryBlock saved;old.getStateInformation(saved);SpectrumTagAudioProcessor recalled;recalled.setRateAndBufferSizeDetails(48000,256);recalled.prepareToPlay(48000,256);recalled.setStateInformation(saved.getData(),(int)saved.getSize());require(recalled.recording().flush(),"1411 legacy cache recall");
    require(recalled.getFrequencyScale()==1&&recalled.recording().edits()->front().scaleMode==1,"1411 retired preset resolves to Log in project and Print");
    require(sameAudioEdit(before->edits.front(),recalled.recording().edits()->front()),"1411 removed name preserves physical Print metadata");
    for(int64_t i=0;i<48000;i+=13)require(before->sample(0,i)==recalled.recording().snapshot()->sample(0,i),"1411 old Print output audio unchanged");
    auto* parameter=dynamic_cast<juce::AudioParameterChoice*>(recalled.getAPVTS().getParameter(ParameterIDs::frequencyPreset));require(parameter&&parameter->choices.size()==6&&parameter->choices[2]=="Log","1411 automation slots remain stable");for(const auto& name:parameter->choices)require(!name.containsIgnoreCase("Super"),"1411 host choices remove retired name");
    std::unique_ptr<juce::AudioProcessorEditor> editor(recalled.createEditor());tickUI();auto* combo=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"fftScale"));require(combo->getNumItems()==6&&combo->getSelectedId()==2&&combo->indexOfItemId(4)<0,"1411 old project shows Log in six-option menu");require(std::abs(*recalled.getAPVTS().getRawParameterValue(ParameterIDs::speed)-3.3f)<1e-6,"1411 no implicit time calibration on recall");
    // Exact B/W remains threshold-based: a fine change can be invisible, but
    // crossing an actual input level must change the corresponding pixels.
    juce::Image gray(juce::Image::RGB,256,64,false);{juce::Image::BitmapData b(gray,juce::Image::BitmapData::writeOnly);for(int y=0;y<64;++y)for(int x=0;x<256;++x)b.setPixelColour(x,y,juce::Colour((juce::uint8)x,(juce::uint8)x,(juce::uint8)x));}
    auto a=image::binarize(gray,{2,.5f}),b=image::binarize(gray,{2,.501f}),c=image::binarize(gray,{2,.503f});
    require(a.getPixelAt(128,0).getAlpha()==0&&b.getPixelAt(128,0).getAlpha()==0&&c.getPixelAt(128,0).getAlpha()==255,"1411 tiny amount steps do not invent grayscale detail");
    PreviewWorker worker;auto big=gray.rescaled(2048,2048);uint64_t wanted=0;bool duringDrag=false;const auto deadline=juce::Time::getMillisecondCounter()+5000;
    while(juce::Time::getMillisecondCounter()<deadline){wanted=worker.request(big,1,.3f+(wanted%300)*.001f);juce::Thread::sleep(1);if(auto result=worker.result()){duringDrag=true;require(result->generation<=wanted&&result->source==big&&result->settings.mode==1,"1411 intermediate frame belongs to the current image");break;}}
    require(duringDrag,"1411 completed conversion is published while requests continue");
    wanted=worker.request(gray,2,.503f);std::shared_ptr<const PreviewWorker::Result> final;
    for(int i=0;i<1000;++i){final=worker.result();if(final&&final->generation==wanted)break;juce::Thread::sleep(2);}
    require(final&&final->generation==wanted&&final->source==gray&&final->mask.getPixelAt(128,0).getAlpha()==255,"1411 final conversion reaches exact amount and new source");
    // Exercise the original ImageBox consumer, not only the worker.
    const auto fixture=dir.getChildFile("1411-gray.png");png(fixture,gray);ImageBoxComponent box;require(box.loadImageFromFile(fixture),"1411 preview fixture load");box.setConversion(2,.503f,false);for(int i=0;i<500&&!box.conversionReady();++i){juce::Thread::sleep(2);box.refreshConversion();}require(box.conversionReady()&&box.getPreviewImage().getPixelAt(128,0)==juce::Colours::black,"1411 source preview updates at final micro-adjustment");
    std::cout<<"1.4.11 CHECKS PASSED; Mel RX default, six modes, legacy Log fallback and unchanged audio, continuous B/W preview, exact threshold result\n";
}
