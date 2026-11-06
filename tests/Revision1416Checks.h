#pragma once
// One behavioural UI check is run against both the realtime and ARA processors.
namespace revision1416 {
inline void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline juce::Component* find(juce::Component& c,const juce::String& id){if(c.getComponentID()==id)return &c;for(auto* child:c.getChildren())if(auto* p=find(*child,id))return p;return nullptr;}
inline ImageBoxComponent* box(juce::Component& c){if(auto* b=dynamic_cast<ImageBoxComponent*>(&c))return b;for(auto* child:c.getChildren())if(auto* b=box(*child))return b;return nullptr;}
inline void ui(SpectrumTagAudioProcessor& p,const juce::File& dir,const std::function<void()>& poll,const std::function<int()>& hostSeeks={}){
    using namespace spectrumtag;
    dir.createDirectory();
    auto wait=[&](auto condition,const char* why){auto until=juce::Time::getMillisecondCounter()+20000;do{poll();juce::Timer::callPendingTimersSynchronously();if(condition())return;juce::Thread::sleep(5);}while((int)(until-juce::Time::getMillisecondCounter())>0);throw std::runtime_error(why);};
    auto parameter=[&](const juce::String& id,float v){auto* a=p.getAPVTS().getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));};
    parameter(ParameterIDs::speed,1);parameter(ParameterIDs::frequencyPreset,0);parameter(ParameterIDs::fftScale,0);parameter(ParameterIDs::fftSize,0);
    parameter(ParameterIDs::imageMode,2);parameter(ParameterIDs::imageAmount,.5f);parameter(ParameterIDs::amplitudeRatio,0);parameter(ParameterIDs::noiseFill,0);parameter(ParameterIDs::invert,0);
    p.rememberARAFrequencyChoice();
    auto source=p.recording().snapshot();need(source!=nullptr,"1416 test requires loaded audio");
    juce::Image gradient(juce::Image::RGB,128,96,false);for(int y=0;y<96;++y)for(int x=0;x<128;++x)gradient.setPixelAt(x,y,juce::Colour::greyLevel(x/127.f));
    const auto path=dir.getChildFile("1416-gradient.png");{auto stream=path.createOutputStream();need(stream!=nullptr,"1416 PNG output");stream->setPosition(0);stream->truncate();juce::PNGImageFormat format;format.writeImageToStream(gradient,*stream);}
    PrintEdit e;e.start=source->start()+.02;e.duration=.075;e.low=0;e.high=.25f;e.fftSize=1024;e.rows=513;e.cols=128;e.imageMode=2;e.scaleMode=0;e.displayTop=20000;e.imageAspect=128.f/96;e.imagePath=path.getFullPathName();
    e.viewZoom=120/(60*e.duration);auto mask=std::make_shared<std::vector<float>>(e.rows*e.cols);for(int y=0;y<e.rows;++y)for(int x=0;x<e.cols;++x)(*mask)[(size_t)y*e.cols+x]=x<64?1.f:0.f;e.mask=mask;
    p.recording().setEdits({e});wait([&]{auto s=p.recording().snapshot();return !p.recording().isBusy()&&s&&s->edits.size()==1&&s->edits[0].id==e.id;},"1416 initial Print");
    auto state=p.getEditorState();state.uiScale=1;state.imagePath=e.imagePath;state.imageVisible=false;state.selectedPrint=e.id;state.chinese=false;state.palette=7;state.viewValid=true;state.viewFit=false;state.viewFollow=false;state.viewZoom=e.viewZoom;state.viewEnd=e.start+e.duration*.5+(830/(60*e.viewZoom))*.5;p.setEditorState(state);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto* view=find(*editor,"spectrumView");auto* amount=dynamic_cast<juce::Slider*>(find(*editor,"imageAmount"));auto* image=box(*editor);need(view&&amount&&image,"1416 preview controls");
    wait([&]{return image->conversionReady()&&!p.recording().isBusy()&&p.recording().snapshot()->fftSize==1024&&(int)view->getProperties()["rasterRevision"]==p.recording().snapshot()->revision;},"1416 initial raster");
    auto click=[&](float x,float y,juce::ModifierKeys mods=juce::ModifierKeys::leftButtonModifier){
        const auto now=juce::Time::getCurrentTime();const juce::Point<float> point{x,y};
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),point,mods,1,0,0,0,0,view,view,now,point,now,1,false);
        view->mouseDown(event);view->mouseUp(event);
    };
    view->createComponentSnapshot(view->getLocalBounds());
    const float printX=(float)(int)view->getProperties()["selectedX"]+(int)view->getProperties()["selectedW"]*.5f;
    const float printY=(float)view->getHeight()-115;
    click(printX,8); // Deselect on valid audio, outside the Print frequency band.
    auto* scale=dynamic_cast<juce::ComboBox*>(find(*editor,"fftScale"));need(scale!=nullptr,"1417 scale combo");
    scale->setSelectedId(5,juce::sendNotificationSync);
    wait([&]{return p.getFrequencyScale()==4;},"1417 set different viewing scale");
    const int seeks=hostSeeks?hostSeeks():0;
    auto& transport=p.audition();transport.reset();transport.advance(false,true,source->end()+1,128,48000);
    const double position=transport.position();
    const auto beforeSelect=p.recording().snapshot();
    click(printX,printY);
    need(scale->getSelectedId()==1&&p.getFrequencyScale()==0&&(int)view->getProperties()["scaleMode"]==0,"1417 first Print click must immediately restore combo AND axis");
    transport.advance(false,true,position,128,48000);
    need(!transport.isLocal()&&transport.position()==position&&(!hostSeeks||hostSeeks()==seeks),"1417 selecting Print must not seek");
    view->createComponentSnapshot(view->getLocalBounds());
    const float selectedX=(int)view->getProperties()["selectedX"]+(int)view->getProperties()["selectedW"]*.5f;
    const float selectedY=(int)view->getProperties()["selectedY"]+(int)view->getProperties()["selectedH"]*.5f;
    click(selectedX,selectedY);transport.advance(false,true,position,128,48000);
    need(!transport.isLocal()&&transport.position()==position&&(!hostSeeks||hostSeeks()==seeks),"1417 clicking selected Print again must not seek");
    need(p.recording().snapshot()==beforeSelect,"1417 selection restores display without rendering audio");
    click(selectedX,8);transport.advance(false,true,position,128,48000);
    need(hostSeeks?hostSeeks()==seeks+1:transport.isLocal(),"1417 only non-Print spectrum seeks");
    click(selectedX,selectedY);
    std::cout<<"PASS 1.4.17 Print first-click scale recall, selection/reselection do not seek, empty spectrum seeks\n"<<std::flush;
    const auto audio=p.recording().snapshot();amount->onDragStart();amount->setValue(.2,juce::sendNotificationSync);
    wait([&]{return image->conversionReady();},"1416 low B/W mask");auto low=view->createComponentSnapshot(view->getLocalBounds());
    amount->setValue(.8,juce::sendNotificationSync);wait([&]{return image->conversionReady();},"1416 high B/W mask");auto high=view->createComponentSnapshot(view->getLocalBounds());
    auto bounds=juce::Rectangle<int>((int)view->getProperties()["selectedX"],(int)view->getProperties()["selectedY"],(int)view->getProperties()["selectedW"],(int)view->getProperties()["selectedH"]).reduced(4).withTrimmedTop(18).getIntersection(view->getLocalBounds());
    int changed=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x)if(std::abs(low.getPixelAt(x,y).getBrightness()-high.getPixelAt(x,y).getBrightness())>.08f)++changed;
    need(changed>20,"1416 selected Print B/W preview must visibly change during drag");need(p.recording().snapshot()==audio,"1416 B/W drag must defer audio rendering");
    amount->onDragEnd();wait([&]{auto s=p.recording().snapshot();return s&&!p.recording().isBusy()&&s->edits.size()==1&&std::abs(s->edits[0].amount-.8f)<.0001f&&!(bool)view->getProperties()["previewActive"];},"1416 final B/W render and preview retirement");
    // 1.4.18: real resize gestures, shared by realtime, ARA and standalone.
    auto paintBounds=[&]{view->createComponentSnapshot(view->getLocalBounds());auto& props=view->getProperties();return juce::Rectangle<float>((float)props["selectedX"],(float)props["selectedY"],(float)props["selectedW"],(float)props["selectedH"]);};
    auto renderFinished=[&]{auto s=p.recording().snapshot();return s&&!p.recording().isBusy()&&s->edits.size()==1&&!(bool)view->getProperties()["previewActive"];};
    auto sameGeometry=[](const PrintEdit& a,const PrintEdit& b){return a.id==b.id&&std::abs(a.start-b.start)<1.e-6&&std::abs(a.duration-b.duration)<1.e-6&&std::abs(a.low-b.low)<1.e-5&&std::abs(a.high-b.high)<1.e-5;};
    // The older selection fixture used an arbitrary display zoom. Give this
    // fixture the same native aspect invariant as a Print created in the UI.
    {auto fixture=p.recording().edits()->front();const auto b=paintBounds();
        fixture.viewZoom*=fixture.imageAspect*b.getHeight()/b.getWidth();
        p.recording().setEdits({fixture});wait(renderFinished,"1418 native-aspect fixture");
        click(b.getCentreX(),b.getCentreY());}
    for(int mode:{0,4}){
        scale->setSelectedId(mode==0?1:5,juce::sendNotificationSync);
        wait([&]{return renderFinished()&&p.recording().snapshot()->edits.front().scaleMode==mode;},"1418 resize frequency mode ready");
        for(float factor:{.6f,1.3f}){
            const auto before=*p.recording().edits();const auto beforeAudio=p.recording().snapshot();
            const auto old=before.front();const auto b=paintBounds();
            const auto start=b.getBottomRight()-juce::Point<float>(4,4);
            const auto now=juce::Time::getCurrentTime();
            auto mouse=[&](juce::Point<float> pt){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),pt,juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,view,view,now,start,now,1,pt!=start);};
            const int oldSeeks=hostSeeks?hostSeeks():0;
            view->mouseDown(mouse(start));
            const auto anchor=paintBounds();
            const auto end=start+juce::Point<float>(anchor.getWidth()*(factor-1),anchor.getHeight()*(factor-1));
            for(int step=1;step<=5;++step){view->mouseDrag(mouse(start+(end-start)*(step/5.f)));poll();juce::Timer::callPendingTimersSynchronously();}
            const auto resized=paintBounds();
            {auto out=dir.getChildFile("1418-resize-preview.png").createOutputStream();juce::PNGImageFormat png;if(out){out->setPosition(0);out->truncate();png.writeImageToStream(view->createComponentSnapshot(view->getLocalBounds()),*out);}}
            need(std::abs(resized.getWidth()/resized.getHeight()-old.imageAspect)<.015f,"1418 proportional resize preview and border");
            need(std::abs(resized.getX()-anchor.getX())<.02f&&std::abs(resized.getY()-anchor.getY())<.02f,"1418 resize must retain top-left anchor");
            need(std::abs(resized.getHeight()/anchor.getHeight()-factor)<.015f,"1418 corner must shrink AND enlarge Print");
            need(p.recording().snapshot()==beforeAudio&&sameGeometry(p.recording().edits()->front(),old),"1418 drag must not render audio or mutate desired Print");
            need(!hostSeeks||hostSeeks()==oldSeeks,"1418 resize must not seek host");
            view->mouseUp(mouse(end));
            wait([&]{return renderFinished()&&std::abs(p.recording().snapshot()->edits.front().duration-old.duration*factor)<1.e-5;},"1418 resize final render");
            const auto after=p.recording().snapshot()->edits.front();
            need(std::abs(after.start-old.start)<1.e-6&&std::abs(after.high-old.high)<1.e-5&&std::abs(after.low-old.low)>1.e-4,"1418 resize changes duration and frequency coverage, not anchor");
            need(after.mask&&after.mask->size()==(size_t)after.rows*after.cols&&after.id==old.id,"1418 resize replaces same Print with valid full mask");
            const auto done=paintBounds();need(std::abs(done.getWidth()/done.getHeight()-old.imageAspect)<.015f,"1418 final border keeps aspect");
            editor->keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier,0));
            wait([&]{return renderFinished()&&sameGeometry(p.recording().snapshot()->edits.front(),old);},"1418 one Undo restores complete resize");
            editor->keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier,0));
            wait([&]{return renderFinished()&&sameGeometry(p.recording().snapshot()->edits.front(),after);},"1418 Redo restores complete resize");
        }
    }
    std::cout<<"PASS 1.4.18 selected Print shrink/enlarge in Linear and Mel: proportional preview/border, fixed anchor, deferred DSP, replacement, Undo/Redo, no seek\n"<<std::flush;
    auto* previous=dynamic_cast<juce::Button*>(find(*editor,"previousPrint"));auto* next=dynamic_cast<juce::Button*>(find(*editor,"nextPrint"));need(previous&&next,"1417 Print navigation controls");
    wait([&]{return !previous->isEnabled()&&!next->isEnabled();},"1417 one selected Print has no neighbours");
    auto first=p.recording().edits()->front(),second=first;second.id=juce::Uuid().toString();second.start=source->end()-.09;
    p.recording().add(second);wait([&]{return !p.recording().isBusy()&&p.recording().snapshot()->edits.size()==2&&next->isEnabled();},"1417 second Print added");
    const int navigationSeeks=hostSeeks?hostSeeks():0;
    transport.reset();transport.advance(false,true,source->end()+1,128,48000);const double navigationPosition=transport.position();
    next->onClick();need(p.getEditorState().selectedPrint==second.id,"1417 Next selects later Print");
    wait([&]{return previous->isEnabled()&&!next->isEnabled();},"1417 Next boundary disabled");
    previous->onClick();need(p.getEditorState().selectedPrint==first.id,"1417 Previous selects earlier Print");
    transport.advance(false,true,navigationPosition,128,48000);need(!transport.isLocal()&&(!hostSeeks||hostSeeks()==navigationSeeks),"1417 Print navigation never seeks");
    p.recording().remove(second.id);wait([&]{return !p.recording().isBusy()&&p.recording().snapshot()->edits.size()==1;},"1417 remove navigation fixture");
    std::cout<<"PASS 1.4.17 previous/next Print, boundaries disabled, navigation does not seek\n"<<std::flush;
    const auto previousRevision=p.recording().snapshot()->revision;
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    wait([&]{auto s=p.recording().snapshot();return s&&s->edits.empty()&&!p.recording().isBusy()&&!(bool)view->getProperties()["previewActive"]&&(int)view->getProperties()["rasterRevision"]==s->revision&&s->revision!=previousRevision;},"1416 Delete must replace the raster without reopening");
    need(!previous->isEnabled()&&!next->isEnabled(),"1417 no Prints disables both navigation buttons");
    auto clean=view->createComponentSnapshot(view->getLocalBounds());editor.reset();editor.reset(p.createEditor());view=find(*editor,"spectrumView");
    wait([&]{return (int)view->getProperties()["rasterRevision"]==p.recording().snapshot()->revision;},"1416 reopened clean raster");auto reopened=view->createComponentSnapshot(view->getLocalBounds());
    int mismatches=0;for(int y=20;y<view->getHeight()-120;++y)for(int x=80;x<view->getWidth()-45;++x)if(clean.getPixelAt(x,y)!=reopened.getPixelAt(x,y))++mismatches;
    need(mismatches==0,"1416 deleted plot must match reopened plot, no stale print");
    auto* fit=dynamic_cast<juce::Button*>(find(*editor,"fitSpectrum"));fit->onClick();auto* speed=dynamic_cast<juce::Slider*>(find(*editor,"speed"));speed->setValue(.1,juce::sendNotificationSync);
    wait([&]{return std::abs((double)view->getProperties()["displayEnd"]-(double)view->getProperties()["displayStart"]-(view->getWidth()-70)/6.)<.02;},"1416 Speed after Fit must use the same absolute time unit");
    auto overlaps=[&]{return (double)view->getProperties()["displayEnd"]>source->start()&&(double)view->getProperties()["displayStart"]<source->end();};
    need(overlaps(),"1417 Speed after Fit must keep recorded audio visible");
    for(float delta:{-10.f,10.f,-3.f,3.f}){
        const auto now=juce::Time::getCurrentTime();juce::Point<float> point{(float)view->getWidth()*.7f,50};
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys::shiftModifier,1,0,0,0,0,view,view,now,point,now,1,false);
        juce::MouseWheelDetails wheel{};wheel.deltaY=delta;view->mouseWheelMove(event,wheel);poll();juce::Timer::callPendingTimersSynchronously();
        need(overlaps(),"1417 modified-wheel zoom must retain recorded range");
    }
    // Reopen a view saved by an older build with the negative, empty range from the report.
    editor.reset();auto displaced=p.getEditorState();displaced.viewValid=true;displaced.viewFit=false;displaced.viewFollow=false;displaced.viewEnd=-39.9;displaced.viewZoom=1;displaced.selectedPrint={};p.setEditorState(displaced);
    editor.reset(p.createEditor());view=find(*editor,"spectrumView");wait(overlaps,"1417 restore must recover an off-audio saved viewport");
    std::cout<<"PASS 1.4.17 zoom keeps audio visible; old off-range view recovered\n"<<std::flush;
    std::cout<<"PASS 1.4.16 shared UI: B/W pixels="<<changed<<", deferred DSP, Delete/reopen equality, absolute Speed after Fit\n"<<std::flush;
}
}
