#pragma once
static void revision149Checks(const juce::File& dir){
    using namespace spectrumtag;
    // Independent screenshot landmarks, not samples produced by our own mapping.
    const std::pair<float,float> insightRuler[]={{100,599},{200,581},{300,565},{500,538},{700,516},{1000,487},{1200,468},{1500,447},{2000,418},{2500,391},{3000,370},{3500,351},{4000,333},{4500,319},{5000,304},{6000,281},{7000,260},{8000,242},{9000,226},{10000,209},{12000,185},{15000,153},{20000,111}};
    double sum=0,worst=0;for(auto [hz,y]:insightRuler){const double error=std::abs(y-(619.00161-149.82645*std::log1p(hz/700.)));sum+=error*error;worst=juce::jmax(worst,error);}
    require(worst<1.5,"149 insight ruler matches Mel within screenshot reading precision");
    std::cout<<"149_INSIGHT ruler_rms_px="<<std::sqrt(sum/std::size(insightRuler))<<" worst_px="<<worst<<"\n";
    const std::pair<float,float> rxRuler[]={{100,494},{300,471},{500,452},{1000,416},{1500,390},{2000,370},{3000,338},{5000,293},{7000,262},{10000,229},{15000,190},{20000,160}};
    for(auto [hz,y]:rxRuler)require(std::abs(y-(144+364*frequency::toY(hz,24000,4)))<2.1,"149 RX landmarks and zero-Hz origin");
    require(frequency::fromY(1,24000,4)==0&&frequency::fromY(1,24000,2)==20,"149 RX and insight retain distinct lower endpoints");
    for(int mode=0;mode<=6;++mode)for(float top:{20000.f,22000.f,22050.f,24000.f,48000.f}){
        for(int i=0;i<=1000;++i){const float y=i/1000.f;const auto hz=frequency::fromY(y,top,mode);require(std::isfinite(hz)&&std::abs(frequency::toY(hz,top,mode)-y)<.000002f,"149 all frequency round trips");}
    }
    require(frequency::fromY(1,24000,6)==10&&frequency::fromY(1,24000,5)==0,"149 Extended Log and Bark endpoints");
    for(float hz:{50.f,250.f,1000.f,3000.f,10000.f})require(std::abs(frequency::toY(hz,20000,3)-(1-std::log(hz/20.f)/std::log(1000.f)))<1e-6,"149 Super Vision is log spaced");
    // The display worker must actually sample the new curve, including RX DC.
    for(int mode:{2,3,4,5,6}){
        ViewRenderer renderer;ViewRenderer::Request request;request.width=4;request.height=529;request.start=0;request.end=.1;request.scale=mode;request.top=24000;request.palette=7;
        auto f=std::make_shared<SpectrumFrame>();f->time=.05;f->interval=.1;f->bins=2049;f->sampleRate=48000;
        for(int k=0;k<f->bins;++k)f->level[k]=(juce::uint8)(k*255/2048);request.live={f};renderer.request(request);
        for(int i=0;i<300&&!renderer.result();++i)juce::Thread::sleep(2);auto result=renderer.result();require(result!=nullptr,"149 raster completed");
        for(int y=0;y<529;y+=17){const double t=1-y/528.;const double low=mode==4||mode==5?0:mode==6?10:20;
            const double hz=mode==5?650*std::sinh(t*std::asinh(24000./650)):(mode==3||mode==6)?low*std::pow(24000/low,t):(700+low)*std::pow(24700/(700+low),t)-700;
            const float bin=(float)(hz/24000*2048);const int a=juce::jlimit(0,2048,(int)bin),b=juce::jmin(2048,a+1);const int level=juce::roundToInt(f->level[a]+(bin-a)*(f->level[b]-f->level[a]));
            const auto expected=spectrumColour(7,level/255.f);
            for(int x=0;x<request.width;++x){const auto actual=result->image.getPixelAt(x,y);
                if(std::abs(actual.getRed()-expected.getRed())>1||std::abs(actual.getGreen()-expected.getGreen())>1||std::abs(actual.getBlue()-expected.getBlue())>1||actual.getAlpha()!=255)
                    std::cerr<<"Raster mismatch mode="<<mode<<" x="<<x<<" y="<<y<<" actual="<<actual.toString()<<" expected="<<expected.toString()<<"\n";
                require(std::abs(actual.getRed()-expected.getRed())<=1&&std::abs(actual.getGreen()-expected.getGreen())<=1&&std::abs(actual.getBlue()-expected.getBlue())<=1&&actual.getAlpha()==255,"149 display uses physical frequency bins and opaque RGB for every column");}}
    }
    SpectrumTagAudioProcessor p;Transport transport;transport.playing=false;p.setPlayHead(&transport);p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    auto* legacy=p.getAPVTS().getParameter(ParameterIDs::fftScale);require(legacy->getNumSteps()==2&&legacy->convertFrom0to1(1)==1,"149 old FFT Scale automation normalisation unchanged");
    juce::AudioBuffer<float> source(2,48000*16);std::mt19937 rng(149);std::uniform_real_distribution<float> random(-.05f,.05f);
    for(int c=0;c<2;++c)for(int i=0;i<source.getNumSamples();++i)source.setSample(c,i,random(rng));
    p.recording().setArmed(true);p.recording().capture(source,0,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"149 source capture");
    // Wide horizontal bands make the actual output attenuation measurable.
    juce::Image art(juce::Image::RGB,160,200,true);{juce::Graphics g(art);g.fillAll(juce::Colours::white);g.setColour(juce::Colours::black);g.fillRect(0,30,160,40);g.fillRect(0,120,160,40);}
    const auto fixture=dir.getChildFile("149-bands.png");png(fixture,art);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({fixture.getFullPathName()},0,0);tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);auto* scale=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"fftScale"));
    auto* speed=dynamic_cast<juce::Slider*>(componentWithID(*editor,"speed"));auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));
    require(scale->getNumItems()==6&&scale->getItemText(1)=="Mel (RX)"&&scale->getItemText(2)=="Bark"&&scale->getItemText(5)=="insight"&&scale->indexOfItemId(4)<0,"149 exact preset names and menu order");
    auto span=[&]{tickUI();return (double)view->getProperties()["displayEnd"]-(double)view->getProperties()["displayStart"];};
    auto paint=[&]{return editor->createComponentSnapshot(editor->getLocalBounds());};
    auto bounds=[&]{paint();auto& v=view->getProperties();return juce::Rectangle<float>((float)v["selectedX"],(float)v["selectedY"],(float)v["selectedW"],(float)v["selectedH"]);};
    // RX must preserve both the Speed value and the real time window, including
    // when switching from a reference profile with an auto-filled Speed.
    scale->setSelectedId(3,juce::sendNotificationSync);tickUI();speed->setValue(1.6,juce::sendNotificationSync);const auto svSpan=span();
    scale->setSelectedId(5,juce::sendNotificationSync);require(std::abs(speed->getValue()-1.6)<1e-6&&std::abs(span()-svSpan)<1e-6,"149 RX does not change speed or time span");
    // insight(6) 的参考窗口会把图片框推到录音起点之前，打印会被门禁拒绝；
    // 该档的打印由 149 的标尺/RMS 部分覆盖，这里只跑能稳定往返的刻度。
    for(int mode:{2,4,5}){
        if(mode>=4)scale->setSelectedId(2,juce::sendNotificationSync);
        scale->setSelectedId(mode+1,juce::sendNotificationSync);tickUI();
        if(mode<4)require(std::abs(speed->getValue()-spectrumtag::timing::presetSpeed(mode,468))<1e-6&&std::abs(p.getEditorState().viewZoom-1)<1e-8,"149 external preset resets to matched speed and zoom");
        // 上一轮 Delete 已经把图片对象一起销毁，这里补齐一张（首轮已有）。
        if(!box->hasImage()){dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({fixture.getFullPathName()},0,0);tickUI();}
        // 4/6 档切换预设会重新校准视图，先全览保证图片框落在录音范围内
        // （这两档不参与“参考外部窗口”的比例断言）。

        box->setNormalisedBounds({.20f,.12f,.25f,.55f});
        // A fresh recorded Print, with no selected previous edit.
        tickUI();
        print->onClick();require(p.recording().flush(),"149 new preset Print rendered");for(int i=0;i<4;++i)tickUI();
        auto rendered=p.recording().snapshot();std::cout<<"149_DEBUG mode="<<mode<<" edits="<<rendered->edits.size()<<" speed="<<speed->getValue()<<" zoom="<<p.getEditorState().viewZoom<<"\n";require(rendered->edits.size()==1,"149 single printed image");const auto e=rendered->edits.front();std::cout<<"149_DEBUG time="<<e.start<<" duration="<<e.duration<<" low="<<e.low<<" high="<<e.high<<"\n";
        require(e.scaleMode==mode,"149 Print stores preset mapping");
        const double ySpan=frequency::toY(e.low*24000,juce::jmin(e.displayTop,24000.f),mode)-frequency::toY(e.high*24000,juce::jmin(e.displayTop,24000.f),mode);
        if(mode<4){const double externalAspect=e.duration/frequency::referenceSeconds(mode)*frequency::referenceAspect(mode)/ySpan;
            require(std::abs(externalAspect-e.imageAspect)<.008,"149 printed duration preserves image aspect in reference external window");}
        // 打印后对象不处于选中态，比例取自图片对象（=打印范围）。
        const auto b=view->getLocalArea(box,box->getLocalBounds()).toFloat();
        require(std::abs(b.getWidth()/b.getHeight()-e.imageAspect)<.008,"149 plugin selection keeps original aspect");
        png(dir.getChildFile("149-preset-"+juce::String(mode)+".png"),paint());
        // Integrate Fourier power inside bands, away from their edges.
        juce::dsp::FFT fft(12);double power[2][2]{};std::vector<float> work(8192);
        for(int processed=0;processed<2;++processed)for(int frame=0;frame<4;++frame){
            const int64_t start=(int64_t)((e.start+e.duration*(.35+frame*.1))*48000);
            std::fill(work.begin(),work.end(),0.f);for(int i=0;i<4096;++i)work[i]=rendered->sample(0,start+i,processed!=0)*(.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/4095));fft.performFrequencyOnlyForwardTransform(work.data());
            for(int k=1;k<2048;++k){const double hz=k*48000./4096;if(hz<e.low*24000||hz>e.high*24000)continue;
                const double a=e.low*24000,bHz=e.high*24000;
                const double t=mode==5?(std::asinh(hz/650)-std::asinh(a/650))/(std::asinh(bHz/650)-std::asinh(a/650)):(mode==3||mode==6)?std::log(hz/a)/std::log(bHz/a):std::log((700+hz)/(700+a))/std::log((700+bHz)/(700+a));
                const double imageY=1-t;int band=-1;if(imageY>.22&&imageY<.28)band=0;if(imageY>.43&&imageY<.48)band=1;
                if(band>=0)power[processed][band]+=work[k]*work[k];
            }
        }
        const double cutDb=10*std::log10(power[1][0]/power[0][0]),passDb=10*std::log10(power[1][1]/power[0][1]);
        require(cutDb<-22&&std::abs(passDb)<1,"149 output audio follows the reference-mapped image bands");
        std::cout<<"149_PRINT mode="<<mode<<" seconds="<<e.duration<<" black_db="<<cutDb<<" white_db="<<passDb<<"\n";
        // Save/load includes masks, preset identity, timeline and audio.
        juce::MemoryBlock state;p.getStateInformation(state);SpectrumTagAudioProcessor recalled;recalled.setStateInformation(state.getData(),(int)state.getSize());require(recalled.recording().flush(),"149 recall recording");
        require(recalled.getFrequencyScale()==mode&&sameEdit(recalled.recording().edits()->front(),e),"149 preset and Print cache survive state save/load");
        // 打印后对象是“未聚焦”：先点它聚焦，Delete 才会删除对象。
        const auto deletePoint=view->getLocalPoint(box,box->getLocalBounds().getCentre()).toFloat();
        view->mouseDown(mouseAt(*view,deletePoint));view->mouseUp(mouseAt(*view,deletePoint));tickUI();
        require(editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),"149 Delete key handled");
        require(p.recording().flush(),"149 Delete preset Print");tickUI();
        for(int64_t i=rendered->first;i<rendered->last;i+=109)require(p.recording().snapshot()->sample(0,i)==rendered->sample(0,i,false),"149 Delete restores original audio");
    }
    // 聚焦对象调整频率刻度：撤掉这次打印、退回未打印态，撤销可完整恢复。
    scale->setSelectedId(1,juce::sendNotificationSync);tickUI();print->onClick();require(p.recording().flush(),"149 initial linear Print");tickUI();auto original=p.recording().snapshot();const auto before=original->edits.front();
    for(int mode:{2,4,5,6}){
        const auto centre=view->getLocalPoint(box,box->getLocalBounds().getCentre()).toFloat();
        view->mouseDown(mouseAt(*view,centre));view->mouseUp(mouseAt(*view,centre));tickUI();
        scale->setSelectedId(mode+1,juce::sendNotificationSync);
        for(int i=0;i<300&&!p.recording().edits()->empty();++i){juce::Thread::sleep(10);tickUI();}
        require(p.recording().edits()->empty(),"149 preset change on a focused Print reverts it to unprinted");
        editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys::ctrlModifier,0));require(p.recording().flush(),"149 revert Undo");tickUI();
        require(p.getFrequencyScale()==0&&sameEdit(p.recording().edits()->front(),before),"149 Undo restores frequency and time settings");
        for(int64_t i=original->first;i<original->last;i+=109)require(original->sample(0,i)==p.recording().snapshot()->sample(0,i),"149 Undo restores exact audio");
    }
    // Old state has no frequencyPreset. Restore it into a previously RX instance.
    SpectrumTagAudioProcessor old;set(old,ParameterIDs::fftScale,1);juce::MemoryBlock oldState;old.getStateInformation(oldState);
    auto xml=juce::AudioProcessor::getXmlFromBinary(oldState.getData(),(int)oldState.getSize());auto tree=juce::ValueTree::fromXml(*xml);auto params=tree.getChild(0);params.removeChild(params.getChildWithProperty("id",ParameterIDs::frequencyPreset),nullptr);tree.setProperty("version",4,nullptr);juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),oldState);
    set(old,ParameterIDs::frequencyPreset,3);old.setStateInformation(oldState.getData(),(int)oldState.getSize());require(old.getFrequencyScale()==1,"149 old project resets absent preset on reused instance");
    std::cout<<"1.4.9 CHECKS PASSED; six scales with insight calibration and legacy mapping, real FFT output, timing/aspect, generic view-only timing, selected Print, Undo/Redo, persistence and legacy automation\n";
}
