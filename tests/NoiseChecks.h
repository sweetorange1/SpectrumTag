#pragma once
static void noiseChecks(const juce::File& dir){
    using namespace spectrumtag;
    RecordingEngine engine;juce::AudioBuffer<float> silence(2,144000);silence.clear();
    engine.capture(silence,0,48000);require(engine.flush()&&!engine.snapshot(),"Noise cannot create a recording without Record or import");
    const auto file=dir.getChildFile("noise-fill-source.wav");wav(file,silence,48000);
    engine.importAudio(file,7);require(engine.flush(),"import silence");auto original=engine.snapshot();
    require(original&&original->start()==7&&original->end()==10,"import aligns audio at host cursor");
    PrintEdit edit;edit.start=7.4;edit.duration=1.2;edit.low=.25f;edit.high=.75f;edit.rows=65;edit.cols=120;
    auto mask=std::make_shared<std::vector<float>>(65*120,0.0f);for(int row=0;row<65;++row)for(int col=40;col<80;++col)(*mask)[(size_t)row*120+col]=1;edit.mask=mask;
    engine.add(edit);require(engine.flush(),"noise off render");auto off=engine.snapshot();
    for(int i=336000;i<480000;++i)require(off->sample(0,i)==0,"zero compensation leaves silence silent");
    engine.setCompensation(edit.id,.5f);require(engine.flush(),"noise fill render");auto filled=engine.snapshot();
    double energy=0;int populated=0;
    const int64_t first=llround(edit.start*48000),last=llround((edit.start+edit.duration)*48000);
    for(int64_t i=336000;i<480000;++i){const float value=filled->sample(0,i);require(std::isfinite(value),"noise output finite");if(i<first||i>=last)require(value==off->sample(0,i),"compensation changes zero samples outside image time bounds");else {energy+=value*value;populated+=(value!=0);}}
    require(energy>1e-4&&populated>10000,"compensation fills previously empty spectral area");
    double darkEnergy=0;for(int i=(int)(7.92*48000);i<(int)(8.08*48000);++i)darkEnergy+=std::pow(filled->sample(0,i),2);
    require(darkEnergy<energy*1e-8,"noise preserves black image interior");
    const auto* frame=filled->frameAt(7.6);require(frame!=nullptr,"compensated spectrum published");
    double inside=0,outside=0;for(int k=700;k<1100;++k)inside+=frame->level[(size_t)k];for(int k=100;k<300;++k)outside+=frame->level[(size_t)k];for(int k=1850;k<2050&&k<frame->bins;++k)outside+=frame->level[(size_t)k];
    require(inside/400-outside/400>65,"noise is concentrated in the image frequency band");
    engine.setCompensation(edit.id,.75f);require(engine.flush(),"noise amount update");auto louder=engine.snapshot();double louderEnergy=0;for(int64_t i=first;i<last;++i)louderEnergy+=std::pow(louder->sample(0,i),2);
    require(louderEnergy>energy*8&&louderEnergy<energy*14,"amount changes compensation level predictably");
    // A later overlapping Print must not spread compensation beyond the image.
    PrintEdit overlap=edit;overlap.id=juce::Uuid().toString();overlap.start=7.2;overlap.duration=1.7;overlap.low=.3f;overlap.high=.6f;
    engine.add(overlap);require(engine.flush(),"overlapping filter after noise");auto overlapped=engine.snapshot();for(int64_t i=336000;i<480000;++i)if(i<first||i>=last)require(overlapped->sample(0,i)==0,"overlapping edit cannot spread noise outside compensated image");
    const auto saved=engine.save();require(saved.isNotEmpty(),"compensated recording saved");RecordingEngine loaded;loaded.restore(saved,true);require(loaded.flush(),"compensated cache loaded");auto recalled=loaded.snapshot();require(recalled&&recalled->edits.size()==2&&recalled->edits[0].compensation==.75f,"compensation settings recalled");for(int64_t i=first;i<last;i+=31)require(recalled->sample(0,i)==overlapped->sample(0,i),"noise is deterministic after project recall");
    engine.importAudio(dir.getChildFile("missing-source.wav"),0);require(engine.flush()&&engine.snapshot()->sourceId==overlapped->sourceId,"failed import preserves existing recording");
    engine.remove(edit.id);engine.remove(overlap.id);require(engine.flush(),"Delete noise edits");auto clean=engine.snapshot();for(int i=336000;i<480000;++i)require(clean->sample(0,i)==0,"Delete removes compensation too");
    // Existing broadband energy should need far less compensation than silence.
    juce::AudioBuffer<float> strong(2,144000);std::mt19937 rng(493);std::uniform_real_distribution<float> random(-.5f,.5f);for(int c=0;c<2;++c)for(int i=0;i<144000;++i)strong.setSample(c,i,random(rng));
    engine.setArmed(true);engine.capture(strong,7,48000);engine.setArmed(false);require(engine.flush(),"capture strong reference");
    engine.add(edit);require(engine.flush(),"strong reference Print without noise");auto strongOff=engine.snapshot();engine.setCompensation(edit.id,.5f);require(engine.flush(),"strong reference compensation");auto strongOn=engine.snapshot();double added=0;for(int64_t i=first;i<last;++i)added+=std::pow(strongOn->sample(0,i)-strongOff->sample(0,i),2);
    require(added<energy*.1,"already strong frequencies receive much less compensation");
    std::cout<<"NOISE CHECKS PASSED; outside-time samples unchanged; occupied-band added energy ratio="<<added/energy<<"\n";
}
