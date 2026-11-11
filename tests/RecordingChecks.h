#pragma once
#include "shared/Recording.h"
static juce::MouseEvent mouseAt(juce::Component& c,juce::Point<float> point,bool right=false) {
    const auto now=juce::Time::getCurrentTime();return {juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(right?juce::ModifierKeys::rightButtonModifier:juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&c,&c,now,point,now,1,false};
}
static void fitSpectrum(juce::Component& view){auto* button=dynamic_cast<juce::Button*>(componentWithID(view,"fitSpectrum"));require(button!=nullptr,"Fit spectrum icon exists");button->onClick();}
static void tickUI(){juce::Thread::sleep(80);juce::Timer::callPendingTimersSynchronously();}
static void recordingChecks(const juce::File& dir) {
    using namespace spectrumtag;
    AuditionTransport time;
    require(std::abs(time.advance(true,true,10,256,48000)-10)<1e-9,"transport starts at host time");
    time.seek(10.75);double host=10+256.0/48000;
    require(std::abs(time.advance(true,true,host,512,48000)-10.75)<1e-9,"local seek starts at next block");
    host+=512.0/48000;
    require(std::abs(time.advance(true,true,host,128,48000)-(10.75+512.0/48000))<1e-9,"variable block retains audition offset");
    require(std::abs(time.advance(false,true,3,256,48000)-3)<1e-9&&!time.isLocal(),"Stop cancels audition and follows DAW");
    time.seek(10.5);time.advance(false,true,3,256,48000);
    time.advance(false,true,3,256,48000);
    require(std::abs(time.advance(true,true,3,256,48000)-10.5)<1e-9,"stopped selection waits for Play");
    require(std::abs(time.advance(true,true,2,256,48000)-2)<1e-9&&!time.isLocal(),"DAW loop cancels local offset");
    time.seek(10.9);time.advance(true,true,2+256.0/48000,256,48000);
    require(time.isLocal(),"second local audition");
    time.advance(true,false,0,256,48000);require(time.isLocal(),"missing timestamp is not a seek");
    time.advance(true,true,12,256,48000); // first timestamp after a missing one is not compared
    time.advance(true,true,8,256,48000);require(!time.isLocal(),"later host jump resets audition");
    // Original audio with a non-zero DAW origin; Record is opt-in.
    SpectrumTagAudioProcessor p;Transport transport;transport.sample=480000;p.setPlayHead(&transport);
    p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);set(p,ParameterIDs::fftSize,2);
    juce::MidiBuffer midi;juce::AudioBuffer<float> block(2,256);block.clear();p.processBlock(block,midi);
    require(!p.recording().isEnabled()&&!p.recording().snapshot(),"live mode creates no recorded clip");
    p.recording().setArmed(true);
    std::mt19937 rng(16283);std::uniform_real_distribution<float> random(-.22f,.22f);
    juce::AudioBuffer<float> original(2,144000);
    for(int ch=0;ch<2;++ch)for(int i=0;i<original.getNumSamples();++i)original.setSample(ch,i,random(rng));
    for(int offset=0;offset<original.getNumSamples();offset+=256){
        const int count=juce::jmin(256,original.getNumSamples()-offset);block.setSize(2,count);
        for(int c=0;c<2;++c)block.copyFrom(c,0,original,c,offset,count);
        transport.sample=480000+offset;p.processBlock(block,midi);
    }
    p.recording().setArmed(false);require(p.recording().flush(),"capture flush");
    auto captured=p.recording().snapshot();require(captured&&!captured->empty(),"capture publishes audio");
    require(std::abs(captured->start()-10)<1e-9&&std::abs(captured->end()-13)<1e-9,"recording preserves host origin and length");
    for(int i=0;i<144000;++i)require(captured->sample(0,480000+i,false)==original.getSample(0,i),"original recording is sample exact");
    PrintEdit edit;edit.start=10.5;edit.duration=1.5;edit.low=0;edit.high=1;edit.rows=65;edit.cols=128;
    auto mask=std::make_shared<std::vector<float>>(65*128,1.0f);edit.mask=mask;
    const auto id=edit.id;p.recording().add(edit);require(p.recording().flush(),"Print render flush");
    auto printed=p.recording().snapshot();require(printed->edits.size()==1,"one editable Print");
    double before=0,after=0;for(int i=528000;i<552000;++i){before+=std::pow(printed->sample(0,i,false),2);after+=std::pow(printed->sample(0,i),2);}
    require(after<before*1e-8,"recorded full-band Print suppresses interior samples");
    require(printed->frameAt(11.2)!=nullptr,"rendered spectrum available");
    // Overlap deletion is equivalent to rendering the surviving edit alone.
    PrintEdit other=edit;other.id=juce::Uuid().toString();other.start=11;other.duration=1;other.low=.2f;other.high=.4f;other.ratio=.1f;
    p.recording().add(other);require(p.recording().flush(),"overlap render");
    p.recording().remove(id);require(p.recording().flush(),"delete first overlap");auto remaining=p.recording().snapshot();
    require(remaining->edits.size()==1&&remaining->edits[0].id==other.id,"Delete preserves other Print");
    p.recording().remove(other.id);require(p.recording().flush(),"delete final Print");auto clean=p.recording().snapshot();
    for(int i=0;i<144000;++i)require(clean->sample(0,480000+i)==original.getSample(0,i),"Delete restores original bit for bit");
    p.recording().add(other);require(p.recording().flush(),"surviving Print reference");auto alone=p.recording().snapshot();
    for(int i=0;i<144000;++i)require(std::abs(alone->sample(0,480000+i)-remaining->sample(0,480000+i))<1e-7f,"overlap deletion matches surviving edit only");
    // Asymmetric frequency mask catches upside-down raster mapping.
    p.recording().remove(other.id);require(p.recording().flush(),"prepare orientation check");
    PrintEdit upper=edit;upper.id=juce::Uuid().toString();auto upperMask=std::make_shared<std::vector<float>>(65*128,0.0f);
    for(int row=40;row<65;++row)for(int col=0;col<128;++col)(*upperMask)[(size_t)row*128+col]=1;upper.mask=upperMask;
    p.recording().add(upper);require(p.recording().flush(),"upper frequency mask render");
    const auto asymmetric=p.recording().snapshot();const auto* originalFrame=captured->frameAt(11.2);const auto* newFrame=asymmetric->frameAt(11.2);
    require(originalFrame&&newFrame,"asymmetric test spectral frames");
    double lowDifference=0,highAttenuation=0;for(int k=100;k<300;++k)lowDifference+=std::abs((int)newFrame->level[(size_t)k]-(int)originalFrame->level[(size_t)k]);
    for(int k=1650;k<1850;++k)highAttenuation+=(int)originalFrame->level[(size_t)k]-(int)newFrame->level[(size_t)k];
    require(lowDifference/200<5&&highAttenuation/200>100,"upper mask affects high frequencies, not low frequencies");
    p.recording().remove(upper.id);p.recording().add(other);require(p.recording().flush(),"restore edit for project check");
    // Immutable cache/project recall includes audio and edit masks.
    juce::MemoryBlock state;p.getStateInformation(state);SpectrumTagAudioProcessor loaded;
    loaded.setStateInformation(state.getData(),(int)state.getSize());require(loaded.recording().flush(),"recording reload flush");
    auto restored=loaded.recording().snapshot();require(restored&&restored->edits.size()==1,"project recalls audio and Print");
    for(int i=0;i<144000;i+=127)require(std::abs(restored->sample(0,480000+i)-alone->sample(0,480000+i))<1e-6f,"restored rendering agrees");
    // Cached playback uses the existing latency. Local seek cannot change playhead.
    transport.playing=true;transport.sample=1000000;p.audition().seek(11.2);
    for(int j=0;j<8;++j){block.setSize(2,256);block.clear();p.processBlock(block,midi);transport.sample+=256;}
    const double at=p.audition().position();const auto index=(int64_t)llround(at*48000)-p.getLatencySamples();
    for(int i=0;i<256;++i)require(std::abs(block.getSample(0,i)-alone->sample(0,index+i))<1e-6f,"internal audition reads latency aligned recording");
    transport.playing=false;transport.sample=240000;block.clear();p.processBlock(block,midi);
    require(std::abs(p.audition().position()-5)<1e-9,"processor Stop matches host cursor");
    // UI tests: actual Delete-key event, overview click and whole-UI scaling.
    set(p,ParameterIDs::frequencyPreset,0); // Fixed test coordinates below use Linear.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");require(view!=nullptr,"recorded spectrum component");
    fitSpectrum(*view);tickUI();
    // 点击已打印对象前不能有“未打印对象”：否则聚焦会先把它自动打印进频谱。
    imageBox(*editor)->clearImage();
    imageBox(*editor)->setNormalisedBounds({.84f,.06f,.10f,.10f});tickUI();
    const int width=view->getWidth()-70,height=view->getHeight()-102;
    const float x=70+width*(11.5f-10)/3,y=height*.70f;
    view->mouseDown(mouseAt(*view,{x,y}));tickUI();
    auto* remove=dynamic_cast<juce::Button*>(componentWithID(*editor,"removePrint"));require(remove&&remove->isEnabled(),"click selects printed region");
    require(p.getEditorState().placementId==p.recording().edits()->front().id,"click focuses the printed region");
    require(view->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),"Delete key handled by spectrum");require(p.recording().flush(),"Delete key render");tickUI();
    require(p.recording().snapshot()->edits.empty(),"Delete key removed selected Print");
    // Exercise actual UI Print. Deleting the object destroyed its source image,
    // so drop a fresh one; the (hidden) Print entry still drives the same path.
    dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    fitSpectrum(*view);tickUI();
    imageBox(*editor)->setNormalisedBounds({.80f,.38f,.08f,.15f});
    auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));require(print!=nullptr,"Print button");
    // 打印会销毁源图，因此尺寸/比例必须在打印前记录。
    const float previewWidth=(float)imageBox(*editor)->getWidth();
    const auto imageAspect=imageBox(*editor)->aspect();
    print->onClick();
    require(p.recording().flush(),"UI Print render");tickUI();auto uiPrint=p.recording().snapshot();
    require(uiPrint->edits.size()==1&&!imageBox(*editor)->isVisible(),"recorded Print hides source image and shows stamped result at its location");
    const auto placed=uiPrint->edits.front();
    const double previewStart=10+(double)imageBox(*editor)->getX()/width*3;
    require(std::abs(previewStart-placed.start)<1e-5,"Print starts exactly under preview time coordinate");
    require(std::abs(placed.duration-(double)imageBox(*editor)->getWidth()/width*3)<1e-6,"Print duration matches displayed image and viewport");
    auto* speedSlider=dynamic_cast<juce::Slider*>(componentWithID(*editor,"speed"));require(speedSlider!=nullptr,"Speed control");
    // 聚焦刚打印的对象：状态3（图片与边框重新出现）。
    const auto printCentre=view->getLocalPoint(imageBox(*editor),imageBox(*editor)->getLocalBounds().getCentre()).toFloat();
    view->mouseDown(mouseAt(*view,printCentre));view->mouseUp(mouseAt(*view,printCentre));tickUI();
    // 状态3 只画范围和 Delete 提示，不显示源图。
    require(!imageBox(*editor)->isVisible(),"focusing a Print shows only its frame");
    const auto priorRevision=p.recording().snapshot()->revision;
    speedSlider->onDragStart();
    for(int i=0;i<20;++i){speedSlider->setValue(1+(i+1)/20.f,juce::sendNotificationSync);tickUI();}
    require(p.recording().snapshot()->revision==priorRevision,"Speed drag never renders audio before release");
    require(std::abs(imageBox(*editor)->getWidth()-previewWidth)<2&&std::abs((double)imageBox(*editor)->getWidth()/imageBox(*editor)->getHeight()-imageAspect)<.01,"Speed preserves image dimensions and aspect");
    speedSlider->onDragEnd();
    for(int i=0;i<300&&!p.recording().edits()->empty();++i){juce::Thread::sleep(10);tickUI();}
    require(p.recording().edits()->empty(),"Speed release reverts the focused Print to unprinted");
    require(imageBox(*editor)->isVisible()&&imageBox(*editor)->hasImage(),"object returns to the unprinted state");
    require(editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys::ctrlModifier,0)),"Ctrl Z handled");require(p.recording().flush(),"undo speed revert");tickUI();
    require(p.recording().snapshot()->edits.size()==1,"one undo restores the Print and its Speed");
    // 删除聚焦对象后重新开始，验证“只允许一个未打印对象”的拖入规则。
    require(editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),"Delete key removes the focused object");
    require(p.recording().flush(),"delete focused object");tickUI();
    require(p.recording().edits()->empty()&&!imageBox(*editor)->hasImage(),"Delete destroys both the Print and its source image");
    dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    require(imageBox(*editor)->hasImage()&&p.recording().edits()->empty(),"a fresh drop becomes the single unprinted object");
    // 只允许一个未打印对象：再拖入一张会先把当前未打印图片打印进频谱。
    dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    require(p.recording().edits()->size()==1&&imageBox(*editor)->hasImage()&&imageBox(*editor)->isVisible(),"dropping a second image prints the previous unprinted one");
    png(dir.getChildFile("recorded-print-selected.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    view->mouseDown(mouseAt(*view,{70+width*.25f,(float)view->getHeight()-50}));
    transport.playing=true;transport.sample=240000;block.clear();p.processBlock(block,midi);
    require(std::abs(p.audition().position()-10.75)<.02&&p.audition().isLocal(),"overview click selects captured time");
    auto* scale=dynamic_cast<juce::ComboBox*>(componentWithID(*editor,"uiScale"));require(scale&&scale->getNumItems()==8,"eight scale presets");
    const auto imageBounds=imageBox(*editor)->getBounds();const auto logicBounds=view->getBounds();
    for(int percent:{75,100,125,150,175,200,225,250}){
        scale->setSelectedId(percent,juce::sendNotificationSync);
        require(editor->getWidth()==juce::roundToInt(1240*percent/100.0)&&editor->getHeight()==juce::roundToInt(680*percent/100.0),"preset scales entire editor");
        require(view->getBounds()==logicBounds&&imageBox(*editor)->getBounds()==imageBounds,"UI scale does not change Print geometry/time");
    }
    editor->getConstrainer()->setBoundsForComponent(editor.get(),{0,0,1401,768},false,false,true,true);
    require(std::abs((double)editor->getWidth()/editor->getHeight()-1240.0/680)<.003,"corner constrains aspect ratio");
    require(scale->getText().contains("113"),"free resize shows actual percentage");
    png(dir.getChildFile("recorded-ui-113-percent.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    scale->setSelectedId(75,juce::sendNotificationSync);png(dir.getChildFile("recorded-ui-75-percent.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    scale->setSelectedId(250,juce::sendNotificationSync);png(dir.getChildFile("recorded-ui-250-percent.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    scale->setSelectedId(100,juce::sendNotificationSync);
    auto* fill=dynamic_cast<juce::Slider*>(componentWithID(*editor,"noiseFill"));require(fill&&fill->isEnabled(),"recorded audio enables Noise Fill control");
    // 聚焦已渲染对象后修改 Noise Fill：新模型会撤掉这次打印、退回未打印态；
    // 重新打印后才把新值写进 Print。
    fitSpectrum(*view);tickUI();
    imageBox(*editor)->clearImage();tickUI();
    const auto selectedEdit=p.recording().snapshot()->edits.front();
    const float centreX=70+(float)((selectedEdit.start+selectedEdit.duration*.5-10)/3)*width;
    view->mouseDown(mouseAt(*view,{centreX,height*.45f}));tickUI();
    fill->onDragStart();fill->setValue(.4,juce::sendNotificationSync);juce::Thread::sleep(200);fill->onDragEnd();
    for(int i=0;i<300&&!p.recording().edits()->empty();++i){juce::Thread::sleep(10);tickUI();}
    require(p.recording().edits()->empty(),"focused Noise Fill edit reverts the Print to unprinted");
    auto* printEntry=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));require(printEntry!=nullptr,"print entry");
    printEntry->onClick();require(p.recording().flush(),"reprint with adjusted Noise Fill");tickUI();
    require(std::abs(p.recording().snapshot()->edits.front().compensation-.4f)<1e-6,"selected Print stores adjusted compensation");
    auto savedDocument=p.recording().snapshot();
    auto* clear=dynamic_cast<juce::Button*>(componentWithID(*editor,"clearRecord"));require(clear!=nullptr,"Clear Record control");clear->onClick();tickUI();require(p.recording().flush(),"clear record flush");tickUI();
    require(!p.recording().isEnabled()&&!p.recording().snapshot(),"Clear Record returns to unrecorded live mode");
    editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),0));require(p.recording().flush(),"undo clear record");tickUI();
    require(p.recording().isEnabled()&&p.recording().snapshot()->edits.size()==savedDocument->edits.size(),"undo restores captured source and edits");
    for(int64_t i=savedDocument->first;i<savedDocument->last;i+=31)require(p.recording().snapshot()->sample(0,i)==savedDocument->sample(0,i),"undo Clear Record restores noise and audio exactly");
    p.startPrint(std::vector<float>(33*20,1),33,20,0,1,10);transport.playing=true;block.clear();p.processBlock(block,midi);
    juce::Thread::sleep(1100);p.pollTransportTimeout();require(!p.isPrintRunning()&&!p.isTransportAdvancing(),"suspended host callbacks also release Print");
    std::cout<<"RECORDING CHECKS PASSED; full-band suppression="<<10*std::log10(juce::jmax(1e-30,after/before))<<" dB; Delete restores original exactly\n";
}
