#pragma once
#include "shared/PrintPreview.h"
static void revision1412Checks(const juce::File& dir) {
    using namespace spectrumtag;
    SpectrumTagAudioProcessor p;p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    set(p,ParameterIDs::frequencyPreset,0);set(p,ParameterIDs::fftScale,0);
    juce::AudioBuffer<float> weak(1,81600),strong(1,91200);std::mt19937 random(1412);std::normal_distribution<float> noise(0,1);
    for(int i=0;i<weak.getNumSamples();++i)weak.setSample(0,i,noise(random)*.00001f);
    for(int i=0;i<strong.getNumSamples();++i)strong.setSample(0,i,noise(random)*.15f);
    p.recording().setArmed(true);p.recording().capture(weak,0,48000,true);p.recording().capture(strong,2.1,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"1412 source and gap captured");
    PrintEdit e;e.start=.25;e.duration=3.5;e.low=.1f;e.high=.8f;e.rows=513;e.fftSize=1024;e.cols=160;e.scaleMode=0;e.displayTop=20000;e.imageMode=2;e.imageAspect=160.f/65;
    auto mask=std::make_shared<std::vector<float>>(513*160,0.f);juce::Image art(juce::Image::RGB,160,65,false);
    for(int y=0;y<65;++y)for(int x=0;x<160;++x){const bool black=y>=22&&y<42;art.setPixelAt(x,64-y,black?juce::Colours::black:juce::Colours::white);}
    for(int y=0;y<513;++y)for(int x=0;x<160;++x)(*mask)[(size_t)y*160+x]=(y>=22.f/65*513&&y<42.f/65*513)?1.f:0.f;
    e.mask=mask;e.imagePath=dir.getChildFile("1412-stripes.png").getFullPathName();png(juce::File(e.imagePath),art);
    p.recording().add(e);require(p.recording().flush(),"1412 original Print");auto original=p.recording().snapshot();
    auto off=printPreview(*original,e,7).rescaled(160,65);e.compensation=.4f;auto medium=printPreview(*original,e,7).rescaled(160,65);e.compensation=.8f;auto high=printPreview(*original,e,7).rescaled(160,65);
    auto brightness=[](const juce::Image& image,int x,int y){return image.getPixelAt(x,y).getBrightness();};
    require(brightness(medium,30,12)>brightness(off,30,12)+.15f&&brightness(high,30,12)>brightness(medium,30,12)+.08f,"1412 weak white region responds progressively to Fill");
    require(high.getPixelAt(30,32)==off.getPixelAt(30,32),"1412 black image interior stays dark");
    require(high.getPixelAt(75,12)==spectrumColour(7,0),"1412 unrecorded timeline gap has no invented noise");
    require(high.getPixelAt(140,12)==off.getPixelAt(140,12),"1412 occupied frequency does not brighten unnecessarily");
    e.invert=true;auto inverse=printPreview(*original,e,7).rescaled(160,65);require(brightness(inverse,30,32)>brightness(high,30,32)+.3f&&brightness(inverse,30,12)<brightness(high,30,12)-.3f,"1412 Invert also reverses fill regions");e.invert=false;
    // Compare against actual DSP output, allowing stochastic STFT variation.
    p.recording().add(e,e.id);require(p.recording().flush(),"1412 actual compensated render");auto rendered=p.recording().snapshot();
    double displayed=0,predicted=0;int count=0;
    for(int x=15;x<45;++x)for(int y=6;y<17;++y){const double time=e.start+(x+.5)*e.duration/e.cols;const auto* frame=rendered->frameAt(time);const float f=frequency::interpolate(1.f-y/64.f,e.low*24000,e.high*24000,e.scaleMode)/24000;const int bin=juce::roundToInt(f*(frame->bins-1));displayed+=spectrumColour(7,frame->level[bin]/255.f).getBrightness();predicted+=brightness(high,x,y);++count;}
    require(std::abs(displayed-predicted)/count<.12,"1412 filled preview tracks actual rendered brightness");
    e.compensation=0;auto reduced=printPreview(*rendered,e,7).rescaled(160,65);require(reduced.getPixelAt(30,12)==off.getPixelAt(30,12),"1412 reducing fill removes previously rendered noise from preview");
    // A later overlapping black edit still suppresses the proposed fill.
    auto overlap=*rendered;auto later=e;later.id=juce::Uuid().toString();later.mask=std::make_shared<const std::vector<float>>(513*160,1.f);overlap.edits.push_back(later);e.compensation=.8f;
    auto covered=printPreview(overlap,e,7).rescaled(160,65);require(covered.getPixelAt(30,12)==spectrumColour(7,0),"1412 overlapping Prints are composed in order");
    // Drive the actual selected-Print slider and inspect the UI pixels.
    auto state=p.getEditorState();state.palette=7;state.chinese=false;state.imagePath=e.imagePath;state.imageVisible=false;p.setEditorState(state);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();auto* view=componentWithID(*editor,"spectrumView");fitSpectrum(*view);tickUI();
    const float width=(float)view->getWidth()-70,height=(float)view->getHeight()-102;
    view->mouseDown(mouseAt(*view,{70+width*.25f,height*.4f}));tickUI();
    auto* fill=dynamic_cast<juce::Slider*>(componentWithID(*editor,"noiseFill"));require(fill&&fill->isEnabled(),"1412 selected Fill control");
    // Selection recalls the Print's display FFT asynchronously. Finish that
    // work before measuring whether a subsequent gesture renders any audio.
    fill->onDragStart();require(p.recording().flush(),"1412 selection settled before Fill drag");
    // Selecting a Print shows the editable source image over it; hide that
    // overlay so the simulated Print preview itself can be measured.
    auto* eye=dynamic_cast<juce::Button*>(componentWithID(*editor,"imageVisibility"));
    if(imageBox(*editor)->isVisible()&&eye)eye->onClick();tickUI();
    const auto revision=p.recording().snapshot()->revision;
    fill->setValue(.1,juce::sendNotificationSync);tickUI();auto lowUI=view->createComponentSnapshot(view->getLocalBounds());
    const int x=juce::roundToInt((float)view->getProperties()["selectedX"]+(float)view->getProperties()["selectedW"]*30.5f/160);
    const int y=juce::roundToInt((float)view->getProperties()["selectedY"]+(float)view->getProperties()["selectedH"]*12.5f/65);
    fill->setValue(.8,juce::sendNotificationSync);tickUI();auto highUI=view->createComponentSnapshot(view->getLocalBounds());
    require(brightness(highUI,x,y)>brightness(lowUI,x,y)+.15f,"1412 visible selected Print preview changes with Noise Fill");
    require(p.recording().snapshot()->revision==revision,"1412 Fill drag never renders audio");
    png(dir.getChildFile("1412-fill-low.png"),lowUI);png(dir.getChildFile("1412-fill-high.png"),highUI);
    fill->setValue(.4,juce::sendNotificationSync);fill->onDragEnd();juce::Thread::sleep(210);tickUI();require(p.recording().flush(),"1412 release renders final Fill");tickUI();
    require(p.recording().edits()->size()==1&&std::abs(p.recording().edits()->front().compensation-.4f)<1e-6,"1412 final Fill replaces selected Print");
    editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),0));require(p.recording().flush(),"1412 undo Fill");tickUI();require(std::abs(p.recording().edits()->front().compensation-.8f)<1e-6,"1412 Fill gesture is one undo");
    editor->keyPressed(juce::KeyPress('Z',juce::ModifierKeys(juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier),0));require(p.recording().flush(),"1412 redo Fill");tickUI();require(std::abs(p.recording().edits()->front().compensation-.4f)<1e-6,"1412 redo restores final Fill");
    // Largest preview size, reporting actual cost instead of a flaky timing gate.
    auto large=e;large.rows=513;large.cols=512;large.mask=std::make_shared<const std::vector<float>>(513*512,0.f);
    double total=0;for(int i=0;i<12;++i){large.compensation=.2f+i*.05f;const auto start=juce::Time::getMillisecondCounterHiRes();auto image=printPreview(*rendered,large,7);total+=juce::Time::getMillisecondCounterHiRes()-start;require(image.isValid(),"1412 full-sized preview");}
    std::cout<<"1.4.12 CHECKS PASSED; fill preview tracks source/mask/gaps/overlaps; drag defers DSP; release/undo/redo verified; brightness_error="<<std::abs(displayed-predicted)/count<<"; 512x512_preview_mean_ms="<<total/12<<"\n";
}
