#pragma once
namespace revision1416 {
inline std::shared_ptr<const spectrumtag::RecordingSnapshot> replacements(){
    using namespace spectrumtag;RecordingEngine engine;juce::AudioBuffer<float> b(1,6*48000);
    for(int i=0;i<b.getNumSamples();++i)b.setSample(0,i,.03f*std::sin(juce::MathConstants<double>::twoPi*700*i/48000));
    engine.setArmed(true);engine.capture(b,0,48000,true);engine.setArmed(false);need(engine.flush(),"1416 replacement source");const auto source=engine.snapshot();
    PrintEdit e;e.start=.1;e.duration=5.5;e.rows=2049;e.cols=160;e.low=.02f;e.high=.8f;e.compensation=.7f;
    auto mask=std::make_shared<std::vector<float>>(e.rows*e.cols);for(int y=0;y<e.rows;++y)for(int x=0;x<e.cols;++x)(*mask)[(size_t)y*e.cols+x]=((x/8+y/32)%2)?1.f:0.f;e.mask=mask;
    engine.add(e);auto until=juce::Time::getMillisecondCounter()+10000;
    while(engine.renderStage()!=RecordingEngine::RenderStage::printing&&(int)(until-juce::Time::getMillisecondCounter())>0)juce::Thread::sleep(1);
    need(engine.isBusy(),"1416 edits arrive while prior render is active");
    for(int i=0;i<8;++i){e.start=.2+i*.2;e.amount=.3f+i*.03f;e.duration=3-i*.2;engine.add(e,e.id);juce::Thread::sleep(2);}
    e.start=4.4;e.duration=.3;e.compensation=.2f;const auto last=juce::Time::getMillisecondCounterHiRes();engine.add(e,e.id);need(engine.flush(30000),"1416 latest replacement completed");const auto elapsed=juce::Time::getMillisecondCounterHiRes()-last;
    auto actual=engine.snapshot();need(actual->edits.size()==1&&sameAudioEdit(actual->edits[0],e),"1416 only the latest edit is published");
    RecordingEngine reference;const auto direct=juce::Time::getMillisecondCounterHiRes();reference.restoreDocument({source,std::make_shared<const std::vector<PrintEdit>>(std::vector<PrintEdit>{e}),true});need(reference.flush(30000),"1416 independent final render");
    double maxError=0;for(int64_t n=0;n<actual->last;++n)maxError=juce::jmax(maxError,std::abs((double)actual->sample(0,n)-reference.snapshot()->sample(0,n)));need(maxError<1.e-7,"1416 cancelled work must not change final audio");
    std::cout<<"PASS replacement while busy: latest_ms="<<elapsed<<" direct_ms="<<juce::Time::getMillisecondCounterHiRes()-direct<<" error="<<maxError<<"\n"<<std::flush;return source;
}
}
