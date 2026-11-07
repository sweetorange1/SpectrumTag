#pragma once
#include "shared/RenderCache.h"
static void revision1421Checks(const juce::File& dir){
    using namespace spectrumtag;
    std::shared_ptr<const RecordingSnapshot> original,wet,legacy;
    juce::String saved;PrintEdit edit;edit.start=.3;edit.duration=2.4;edit.low=.2f;edit.high=.8f;edit.rows=129;edit.cols=120;edit.compensation=.75f;
    auto mask=std::make_shared<std::vector<float>>(edit.rows*edit.cols,0.f);
    for(int r=35;r<80;++r)for(int c=35;c<65;++c)(*mask)[r*edit.cols+c]=1.f;
    (*mask)[123]=.1234567f;edit.mask=mask;
    {RecordingEngine engine;juce::AudioBuffer<float> silence(1,144000);silence.clear();engine.setArmed(true);engine.capture(silence,0,48000,true);engine.setArmed(false);require(engine.flush(),"1421 silence source");original=engine.snapshot();
        auto old=edit;old.fillVersion=0;engine.add(old);require(engine.flush(30000),"1421 legacy fill");legacy=engine.snapshot();
        const auto start=juce::Time::getMillisecondCounterHiRes();engine.add(edit,edit.id);require(engine.flush(30000),"1421 target fill");wet=engine.snapshot();
        std::cout<<"TARGET_FILL_MS="<<juce::Time::getMillisecondCounterHiRes()-start<<"\n";saved=engine.save();require(saved.isNotEmpty(),"1421 recording state saved");}
    auto deviation=[](const RecordingSnapshot& s){double sum=0,square=0;int count=0;for(double t=.5;t<2.5;t+=.023)for(int k=450;k<1600;++k){if(t>1.0&&t<1.8&&k>750&&k<1400)continue;const auto* f=s.frameAt(t);if(!f)continue;const double db=f->level[k]*96./255.-96;sum+=db;square+=db*db;++count;}return std::sqrt(square/count-std::pow(sum/count,2));};
    const double oldDeviation=deviation(*legacy),newDeviation=deviation(*wet);
    std::cout<<"FILL_DB_STD_OLD="<<oldDeviation<<" NEW="<<newDeviation<<"\n";
    // Flat bright rectangle, excluding its internal black cutout and borders.
    auto brightDeviation=[](const RecordingSnapshot& s){double sum=0,sq=0;int count=0;for(double t=.5;t<.9;t+=.021){const auto* f=s.frameAt(t);for(int k=600;k<1400;++k){const double db=f->level[k]*96./255.-96;sum+=db;sq+=db*db;++count;}}return std::sqrt(sq/count-std::pow(sum/count,2));};
    const auto before=brightDeviation(*legacy),after=brightDeviation(*wet);std::cout<<"BRIGHT_DB_STD_OLD="<<before<<" NEW="<<after<<"\n";require(after<before*.7,"1421 target synthesis reduces random bright-region holes");
    for(int n=0;n<144000;++n){require(std::isfinite(wet->sample(0,n)),"1421 finite fill");if(n<14400||n>=129600)require(wet->sample(0,n)==0,"1421 fill strictly inside image time");}
    RecordingEngine restored;const auto began=juce::Time::getMillisecondCounterHiRes();restored.restore(saved,true);require(restored.flush(30000),"1421 reopen project");
    require(restored.cacheHits()==1&&restored.printPasses()==0&&restored.analysisPasses()==0,"1421 project reopen skips Print and spectrum DSP");
    std::cout<<"REOPEN_MS="<<juce::Time::getMillisecondCounterHiRes()-began<<" print="<<restored.printPasses()<<" analyse="<<restored.analysisPasses()<<"\n";
    auto recalled=restored.snapshot();require(sameEdits(restored.edits(),std::make_shared<const std::vector<PrintEdit>>(wet->edits)),"1421 lossless masks and edit version");
    for(int n=0;n<144000;++n)require(recalled->sample(0,n)==wet->sample(0,n),"1421 cache audio bit exact");
    for(size_t t=0;t<wet->spectrum.size();++t)for(size_t f=0;f<wet->spectrum[t]->frames.size();++f)require(wet->spectrum[t]->frames[f].level==recalled->spectrum[t]->frames[f].level,"1421 spectrum cache exact");
    auto recreated=std::make_shared<RecordingSnapshot>(*original);recreated->sourceId=juce::Uuid().toString();for(auto& p:recreated->original)if(p)p=std::make_shared<AudioPage>(*p);
    RecordingEngine araSource;araSource.restoreDocument({recreated,std::make_shared<const std::vector<PrintEdit>>(wet->edits),true});require(araSource.flush(30000)&&araSource.cacheHits()==1&&araSource.printPasses()==0,"1421 ARA source cache survives new host pointers and IDs");
    {RecordingEngine view(true);view.present(wet);require(view.flush(30000),"1421 composed ARA spectrum persisted");}
    RecordingEngine reopenedView(true);auto timeline=std::make_shared<RecordingSnapshot>(*wet);timeline->sourceId=juce::Uuid().toString();reopenedView.present(timeline);require(reopenedView.flush(30000)&&reopenedView.cacheHits()==1&&reopenedView.analysisPasses()==0,"1421 ARA timeline cache skips repeated FFT");
    RenderCache cache;const auto key=cache.key(*wet,false);require(RenderCache::file(key).replaceWithText("damaged derived cache"),"1421 corrupt owned fixture cache");
    RecordingEngine damaged;damaged.restore(saved,true);require(damaged.flush(30000)&&damaged.errorCode()==0&&damaged.cacheHits()==0&&damaged.printPasses()>0,"1421 bad cache safely rebuilds from original");for(int n=0;n<144000;n+=13)require(damaged.snapshot()->sample(0,n)==wet->sample(0,n),"1421 deterministic target reconstruction");
    auto altered=std::make_shared<RecordingSnapshot>(*recreated);auto page=std::make_shared<AudioPage>(*altered->original[0]);page->samples[0][9]=.2f;altered->original[0]=page;
    RecordingEngine replaced;replaced.restoreDocument({altered,std::make_shared<const std::vector<PrintEdit>>(wet->edits),true});require(replaced.flush(30000)&&replaced.cacheHits()==0&&replaced.printPasses()>0,"1421 changed host source invalidates audio cache");
    restored.remove(edit.id);require(restored.flush(30000),"1421 remove target print");for(int n=0;n<144000;++n)require(restored.snapshot()->sample(0,n)==0,"1421 Delete restores original and removes synthesized noise");
    wav(dir.getChildFile("1421-legacy-fill.wav"),[&]{juce::AudioBuffer<float> b(1,144000);for(int n=0;n<144000;++n)b.setSample(0,n,legacy->sample(0,n));return b;}(),48000);
    wav(dir.getChildFile("1421-target-fill.wav"),[&]{juce::AudioBuffer<float> b(1,144000);for(int n=0;n<144000;++n)b.setSample(0,n,wet->sample(0,n));return b;}(),48000);
    std::cout<<"1421 CACHE AND TARGET FILL CHECKS PASSED\n";
    auto longSource=std::make_shared<RecordingSnapshot>();longSource->sampleRate=48000;longSource->channels=2;longSource->last=48000*30;
    for(int64_t n=0;n<longSource->last;n+=recordingPageSize){auto page=std::make_shared<AudioPage>();for(int i=0;i<recordingPageSize&&n+i<longSource->last;++i){page->valid.set(i);for(int c=0;c<2;++c)page->samples[c][i]=.1f*std::sin((n+i)*(.035+c*.002));}longSource->original.push_back(page);}longSource->rendered=longSource->original;
    RecordingEngine firstLoad;firstLoad.restoreDocument({longSource,std::make_shared<const std::vector<PrintEdit>>(),true});require(firstLoad.flush(60000),"1421 long first-load analysis");const auto analysed=firstLoad.snapshot();
    const int N=4096,hop=N/8;juce::dsp::FFT fft(12);std::vector<float> window(N),data(2*N);float energy=0;for(int i=0;i<N;++i){window[i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/(N-1));energy+=window[i]*window[i];}
    const float normalisation=1/std::sqrt(energy);const auto referenceStart=juce::Time::getMillisecondCounterHiRes();
    for(const auto& tile:analysed->spectrum)for(const auto& frame:tile->frames){const auto centre=llround(frame.time*48000);if(!analysed->contains(centre))continue;std::fill(data.begin(),data.end(),0.f);
        for(int i=0;i<N;++i){float sample=0;for(int c=0;c<2;++c)sample+=analysed->sample(c,centre-N/2+i);data[i]=sample/2*window[i];}fft.performRealOnlyForwardTransform(data.data());
        for(int k=0;k<=N/2;++k){const float db=juce::Decibels::gainToDecibels(std::sqrt(data[2*k]*data[2*k]+data[2*k+1]*data[2*k+1])*normalisation,-96.f);const int level=juce::jlimit(0,255,juce::roundToInt((db+96)*255/96));require(level==frame.level[k],"1421 optimized FFT matches original scalar analysis");}}
    std::cout<<"FIRST_LOAD_30S_STEREO_ANALYSIS_MS="<<firstLoad.analysisMilliseconds()<<" SCALAR_REFERENCE_MS="<<juce::Time::getMillisecondCounterHiRes()-referenceStart<<"\n";
}
