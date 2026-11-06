#pragma once
inline void araReplacementChecks(){
    using namespace spectrumtag;using namespace spectrumtag::ara;
    auto source=std::make_shared<spectrumtag::RecordingSnapshot>(*revision1416::replacements());source->rendered=source->original;RecordingEngine edited;edited.restoreDocument({source,std::make_shared<const std::vector<PrintEdit>>(),true});check(edited.flush(),"ARA benchmark initial source");
    Composer composer;Composer::Job job;job.key="incremental-check";job.rate=48000;spectrumtag::ara::Span span;span.start=10;span.end=16;span.sourceStart=0;span.frozen=source;job.spans={span};
    auto compose=[&](Composer& c,Composer::Job j){const auto expected=c.request(j);uint64_t id=0;std::shared_ptr<const RecordingSnapshot> out;const auto until=juce::Time::getMillisecondCounter()+15000;do{out=c.result(id);if(id==expected&&!c.busy())return out;juce::Thread::sleep(2);}while((int)(until-juce::Time::getMillisecondCounter())>0);throw std::runtime_error("ARA composition timed out");};
    auto initial=compose(composer,job);RecordingEngine display(true);display.present(initial);check(display.flush(),"ARA baseline display");const auto base=display.snapshot();
    PrintEdit e;e.start=2.5;e.duration=.2;e.rows=513;e.cols=64;e.fftSize=1024;e.low=.01f;e.high=.3f;e.mask=std::make_shared<const std::vector<float>>(e.rows*e.cols,1.f);edited.add(e);check(edited.flush(),"ARA localized Print");job.spans[0].frozen=edited.snapshot();job.edits={e};job.edits[0].start+=10;
    const auto begin=juce::Time::getMillisecondCounterHiRes();auto incremental=compose(composer,job);display.present(incremental);check(display.flush(),"ARA incremental display");const auto cost=juce::Time::getMillisecondCounterHiRes()-begin;
    check(initial->original==incremental->original,"ARA must reuse immutable composed source");int reused=0;for(size_t i=0;i<initial->rendered.size();++i)reused+=initial->rendered[i]==incremental->rendered[i];check(reused>(int)initial->rendered.size()*3/4,"ARA reuses unaffected timeline pages");
    check(display.snapshot()->originalSpectrum==base->originalSpectrum,"ARA must not reanalyse original spectrum on every Print");
    int tiles=0;for(size_t i=0;i<base->spectrum.size();++i)tiles+=display.snapshot()->spectrum[i]==base->spectrum[i];check(tiles>(int)base->spectrum.size()/2,"ARA reuses unaffected spectrum tiles");
    Composer reference;auto full=compose(reference,job);double error=0;for(int64_t n=full->first;n<full->last;++n)error=juce::jmax(error,std::abs((double)full->sample(0,n)-incremental->sample(0,n)));check(error<1.e-7,"ARA incremental samples must match a full composition");
    RecordingEngine fullDisplay(true);fullDisplay.present(full);check(fullDisplay.flush(),"ARA full reference spectrum");const auto raster=fullDisplay.snapshot();for(size_t i=0;i<raster->spectrum.size();++i){const auto& a=raster->spectrum[i]->frames;const auto& b=display.snapshot()->spectrum[i]->frames;check(a.size()==b.size(),"ARA spectrum frame count");for(size_t j=0;j<a.size();++j)check(a[j].level==b[j].level,"ARA incremental frequency bins equal full analysis");}
    // Source-rate conversion and overlapping events must use the same cache rules.
    job.rate=44100;auto overlap=job.spans[0];overlap.start+=.4;overlap.end+=.4;job.spans.push_back(overlap);compose(composer,job);
    e.start=3.1;edited.add(e,e.id);check(edited.flush(),"ARA moved Print");for(auto& s:job.spans)s.frozen=edited.snapshot();auto moved=compose(composer,job);auto movedFull=compose(reference,job);
    error=0;for(int64_t n=moved->first;n<moved->last;++n)error=juce::jmax(error,std::abs((double)moved->sample(0,n)-movedFull->sample(0,n)));check(error<1.e-7,"ARA overlap/resampling incremental audio matches full composition");
    edited.setEdits({});check(edited.flush(),"ARA Delete benchmark");for(auto& s:job.spans)s.frozen=edited.snapshot();job.edits.clear();composer.request(job);composer.cancel();auto clean=compose(composer,job);check(clean->rendered==clean->original,"ARA cancelled job/Delete restores original directly");
    std::cout<<"PASS ARA incremental composition: local_edit_ms="<<cost<<" reused_pages="<<reused<<" reused_tiles="<<tiles<<"; full-reference audio/spectrum, resampling and overlap verified\n"<<std::flush;
}

