#include "Recording.h"
#include "TimeScale.h"
#include "RenderCache.h"
#include "TargetFill.h"
#include <limits>
#include <thread>
namespace spectrumtag {
bool PrintEdit::valid() const {
    return std::isfinite(start)&&std::isfinite(duration)&&duration>0&&duration<=900
        &&std::abs(start)<1.0e9&&std::isfinite(low)&&std::isfinite(high)&&low>=0&&high<=1&&low<high
        &&std::isfinite(compensation)&&compensation>=0&&compensation<=1&&std::isfinite(ratio)&&ratio>=0&&ratio<=.33f&&(fftSize==1024||fftSize==2048||fftSize==4096)
        &&fillVersion>=0&&fillVersion<=1&&rows>0&&rows<=2049&&cols>0&&cols<=4096&&mask&&mask->size()==(size_t)rows*cols;
}
float RecordingSnapshot::sample(int channel,int64_t absolute,bool processed) const {
    const auto index=absolute-origin;if(index<0)return 0;
    const auto page=(size_t)(index/recordingPageSize);const auto& pages=processed?rendered:original;
    if(page>=pages.size()||!pages[page])return 0;
    return pages[page]->samples[(size_t)juce::jlimit(0,channels-1,channel)][(size_t)(index%recordingPageSize)];
}
bool RecordingSnapshot::contains(int64_t absolute) const {
    const auto index=absolute-origin;if(index<0)return false;
    const auto page=(size_t)(index/recordingPageSize);
    return page<original.size()&&original[page]&&original[page]->valid[(size_t)(index%recordingPageSize)];
}
const SpectrumFrame* RecordingSnapshot::frameAt(double seconds,bool processed) const {
    // The retained boundary tile can extend beyond a shortened standalone
    // timeline. Its old bins are not part of the current audio extent.
    if(!std::isfinite(seconds)||seconds<start()||seconds>=end())return nullptr;
    const auto& tiles=processed?spectrum:originalSpectrum;
    const int64_t relative=llround((seconds*sampleRate-origin)/(fftSize/8))*(fftSize/8);
    if(relative<0)return nullptr;
    const size_t tile=(size_t)(relative/tileSize),index=(size_t)((relative%tileSize)/(fftSize/8));
    if(tile>=tiles.size()||!tiles[tile]||index>=tiles[tile]->frames.size())return nullptr;
    return &tiles[tile]->frames[index];
}
void AuditionTransport::reset(){initialised=false;previousPlaying=false;previousHadTime=false;offset=0;local=false;consumed=request.load();}
double AuditionTransport::advance(bool playing,bool hasTime,double host,int count,double sr) {
    sr=juce::jmax(1.0,sr);
    if(followRequested.exchange(false)){offset=0;local=false;previousPlaying=false;initialised=false;consumed=followConsumed.load();}
    if(!hasTime)host=initialised?previousHost+(previousPlaying?previousDuration:0):0;
    const bool stop=initialised&&previousPlaying&&!playing;
    const double expected=previousHost+(previousPlaying?previousDuration:0);
    const bool jump=initialised&&previousHadTime&&hasTime&&(previousPlaying==playing)&&std::abs(host-expected)>juce::jmax(1.5/sr,0.00001);
    if(stop||jump){offset=0;local=false;}
    const int consumedBefore=consumed;const int revisionNow=request.load();
    if(revisionNow!=consumed){offset=requested.load()-host;local=true;consumed=revisionNow;}
    // A stopped click stays pending. A subsequent Play must not advance it by
    // the amount of time the user spent waiting with the transport stopped.
    if(playing&&!previousPlaying&&local.load()&&revisionNow==consumedBefore)offset=shown.load()-host;
    const double cursor=host+(local.load()?offset:0);
    shown=cursor;hostShown=host;previousHost=host;previousDuration=count/sr;
    previousPlaying=playing;previousHadTime=previousHadTime||hasTime;initialised=true;return cursor;
}
RecordingEngine::RecordingEngine(bool external):juce::Thread(external?"SpectrumTag ARA view":"SpectrumTag recording"),externalView(external),renderCache(std::make_unique<RenderCache>()),packets(external?0:queueSize) {startThread();}
RecordingEngine::~RecordingEngine(){signalThreadShouldExit();notify();stopThread(-1);}
void RecordingEngine::enqueue(std::function<void()> fn){
    {std::lock_guard<std::mutex> lock(commandsMutex);commands.push_back(std::move(fn));pending.fetch_add(1);}notify();
}
void RecordingEngine::capture(const juce::AudioBuffer<float>& buffer,double time,double rate,bool offline){
    if(!armed.load()||!std::isfinite(time)||std::abs(time)>=1.0e9||!std::isfinite(rate)||rate<8000||rate>384000)return;
    const int epoch=captureEpoch.load();
    for(int offset=0;offset<buffer.getNumSamples();offset+=packetSize){
        int a,n,b,m;fifo.prepareToWrite(1,a,n,b,m);
        const auto waitStart=juce::Time::getMillisecondCounter();
        while(n==0&&offline&&armed.load()&&!threadShouldExit()
              &&juce::Time::getMillisecondCounter()-waitStart<30000){
            notify();juce::Thread::sleep(1);fifo.prepareToWrite(1,a,n,b,m);
        }
        if(!armed.load())break;
        if(n==0){error=1;armed=false;break;}
        auto& p=packets[(size_t)a];p.time=time+offset/rate;p.rate=rate;
        p.epoch=epoch;p.channels=juce::jlimit(1,2,buffer.getNumChannels());p.count=juce::jmin(packetSize,buffer.getNumSamples()-offset);
        for(int c=0;c<p.channels;++c)std::copy_n(buffer.getReadPointer(c,offset),p.count,p.data[(size_t)c].data());
        fifo.finishedWrite(1);
    }
    notify();
}
void RecordingEngine::dirty(int64_t a,int64_t b){dirtyFirst=juce::jmin(dirtyFirst,a);dirtyLast=juce::jmax(dirtyLast,b);changed=true;}
void RecordingEngine::ingest(const Packet& p){
    if(p.epoch!=captureEpoch.load())return;
    const int64_t begin=llround(p.time*p.rate),end=begin+p.count;
    if(source.empty()){work.sampleRate=p.rate;work.channels=p.channels;work.origin=(int64_t)std::floor((double)begin/recordingPageSize)*recordingPageSize;work.first=begin;work.last=begin;}
    if(work.sampleRate!=p.rate||work.channels!=p.channels){error=3;armed=false;return;}
    const int64_t newFirst=juce::jmin(work.first,begin),newLast=juce::jmax(work.last,end);
    if(newLast-newFirst>juce::jmin(maxSamples,(int64_t)(900*p.rate))){error=2;armed=false;return;}
    if(begin<work.origin){
        const int64_t next=(int64_t)std::floor((double)begin/recordingPageSize)*recordingPageSize;
        source.insert(source.begin(),(size_t)((work.origin-next)/recordingPageSize),nullptr);
        work.origin=next;work.rendered.clear();work.spectrum.clear();work.originalSpectrum.clear();work.originalPeaks.clear();dirty(newFirst,newLast);
    }
    source.resize((size_t)((newLast-work.origin+recordingPageSize-1)/recordingPageSize));
    for(int i=0;i<p.count;){
        const auto relative=begin+i-work.origin;const size_t page=(size_t)(relative/recordingPageSize);
        if(!source[page])source[page]=std::make_shared<AudioPage>();
        else if(!source[page].unique())source[page]=std::make_shared<AudioPage>(*source[page]);
        const int at=(int)(relative%recordingPageSize),count=juce::jmin(p.count-i,recordingPageSize-at);
        for(int c=0;c<p.channels;++c)for(int j=0;j<count;++j){const float value=p.data[(size_t)c][(size_t)(i+j)];source[page]->samples[(size_t)c][(size_t)(at+j)]=std::isfinite(value)?value:0;}
        for(int j=0;j<count;++j)source[page]->valid.set((size_t)(at+j));i+=count;
    }
    work.first=newFirst;work.last=newLast;dirty(begin,end);
    // Capturing replaces source samples; only invalidate affected baseline tiles.
    for(int64_t t=juce::jmax<int64_t>(0,(begin-work.origin-work.fftSize)/RecordingSnapshot::tileSize);
        t<(int64_t)work.originalSpectrum.size()&&work.origin+t*RecordingSnapshot::tileSize<=end+work.fftSize;++t)work.originalSpectrum[(size_t)t].reset();
}
void RecordingEngine::setEdits(std::vector<PrintEdit> next){
    if(!enabled.load())return;
    next.erase(std::remove_if(next.begin(),next.end(),[](const auto& e){return !e.valid();}),next.end());
    if(filePrintExtension.load())if(auto s=snapshot()){
        double first=0,last=(s->fileSourceLast>0?s->fileSourceLast:s->last)/s->sampleRate;
        for(const auto& e:next){first=juce::jmin(first,e.start);last=juce::jmax(last,e.start+e.duration);}
        if(last-first>juce::jmin((double)maxSamples/s->sampleRate,900.)){error=2;return;}
    }
    if(externalView){
        auto replacement=std::make_shared<const std::vector<PrintEdit>>(next);
        if(sameEdits(edits(),replacement))return;
        std::atomic_store(&desiredEdits,replacement);
        if(onExternalEdits)onExternalEdits(next);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(commandsMutex);
        auto replacement=std::make_shared<const std::vector<PrintEdit>>(std::move(next));
        if(sameEdits(edits(),replacement))return;
        std::atomic_store(&desiredEdits,std::move(replacement));
        editRevision.fetch_add(1);progress=0;stage=RenderStage::queued;
        if(!editsQueued){editsQueued=true;commands.push_back([this]{applyDesiredEdits();});pending.fetch_add(1);}
    }
    notify();
}
void RecordingEngine::applyDesiredEdits(){
    std::shared_ptr<const std::vector<PrintEdit>> next;
    {std::lock_guard<std::mutex> lock(commandsMutex);next=edits();editsQueued=false;}
    const auto mark=[this](const PrintEdit& e){dirty(llround(e.start*work.sampleRate)-e.fftSize,llround((e.start+e.duration)*work.sampleRate)+e.fftSize);};
    for(const auto& e:work.edits)if(std::none_of(next->begin(),next->end(),[&](const auto& n){return sameAudioEdit(e,n);}))mark(e);
    for(const auto& e:*next)if(std::none_of(work.edits.begin(),work.edits.end(),[&](const auto& n){return sameAudioEdit(e,n);}))mark(e);
    work.edits=*next;changed=true;
}
void RecordingEngine::restoreDocument(RecordingDocument state){
    armed=false;captureEpoch.fetch_add(1);enabled=state.enabled;editRevision.fetch_add(1);
    const auto next=state.edits?state.edits:std::make_shared<const std::vector<PrintEdit>>();
    std::atomic_store(&desiredEdits,next);
    enqueue([this,state=std::move(state),next]{
        restoreCacheRequested=true;
        work=state.source?*state.source:RecordingSnapshot{};source.clear();
        if(state.source)for(size_t p=work.fileSourceLast>0?(size_t)(-work.origin/recordingPageSize):0;p<state.source->original.size();++p)
            source.push_back(std::const_pointer_cast<AudioPage>(state.source->original[p]));
        if(work.fileSourceLast>0){
            source.resize((size_t)((work.fileSourceLast+recordingPageSize-1)/recordingPageSize));
            const auto tail=(size_t)(work.fileSourceLast%recordingPageSize);
            if(tail&&!source.empty()&&source.back()){
                auto page=std::make_shared<AudioPage>(*source.back());
                for(size_t i=tail;i<recordingPageSize;++i){page->valid.reset(i);for(auto& channel:page->samples)channel[i]=0;}
                source.back()=std::move(page);
            }
        }
        work.edits=*next;work.rendered.clear();work.spectrum.clear();work.peaks.clear();error=0;
        if(work.empty()){if(auto old=std::atomic_exchange(&published,std::shared_ptr<const RecordingSnapshot>{}))retired.push_back(std::move(old));}
        dirty(work.first,work.last);
    });
}
std::shared_ptr<RecordingEngine::ClearResult> RecordingEngine::clearKeepingUndo(){
    auto result=std::make_shared<ClearResult>();
    armed=false;const bool wasEnabled=enabled.exchange(false);clearing=true;editRevision.fetch_add(1);
    const auto previousEdits=edits();
    std::atomic_store(&desiredEdits,std::make_shared<const std::vector<PrintEdit>>());
    enqueue([this,result,previousEdits,wasEnabled]{
        // run() drains captured packets first; retain their last samples for Undo.
        auto saved=std::make_shared<RecordingSnapshot>(work);
        if(!filePrintExtension.load())saved->original.assign(source.begin(),source.end());saved->edits=*previousEdits;
        result->before={saved->empty()?nullptr:saved,previousEdits,wasEnabled};
        captureEpoch.fetch_add(1);source.clear();work=RecordingSnapshot{};
        if(auto old=std::atomic_exchange(&published,std::shared_ptr<const RecordingSnapshot>{}))retired.push_back(std::move(old));
        dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;changed=false;error=0;progress=1;stage=RenderStage::ready;clearing=false;
        result->ready=true;
    });
    return result;
}
void RecordingEngine::add(PrintEdit edit,juce::String replaceId){
    if(!enabled.load()||!edit.valid())return;
    auto next=*edits();
    if(replaceId.isNotEmpty())for(auto& old:next)if(old.id==replaceId){edit.id=old.id;old=std::move(edit);setEdits(std::move(next));return;}
    next.push_back(std::move(edit));setEdits(std::move(next));
}
void RecordingEngine::remove(juce::String id){auto next=*edits();next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& e){return e.id==id;}),next.end());setEdits(std::move(next));}
void RecordingEngine::setCompensation(juce::String id,float amount){auto next=*edits();for(auto& e:next)if(e.id==id)e.compensation=juce::jlimit(0.0f,1.0f,amount);setEdits(std::move(next));}
void RecordingEngine::importAudio(const juce::File& file,double hostStart,double targetRate){
    if(!std::isfinite(hostStart)||std::abs(hostStart)>=1.0e9)return;
    armed=false;captureEpoch.fetch_add(1);
    enqueue([this,file,hostStart,targetRate]{
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader||reader->lengthInSamples<1||reader->lengthInSamples>juce::jmin(maxSamples,(int64_t)(reader->sampleRate*900))||reader->sampleRate<8000||reader->sampleRate>384000){error=5;return;}
        // Decode completely before replacing the active recording. Failed imports
        // leave the existing source and edits available.
        RecordingSnapshot loaded;loaded.sampleRate=targetRate>=8000&&targetRate<=384000?targetRate:reader->sampleRate;loaded.channels=juce::jlimit(1,2,(int)reader->numChannels);loaded.fftSize=requestedFft.load();
        const int64_t length=llround(reader->lengthInSamples*loaded.sampleRate/reader->sampleRate);
        if(length<1||length>maxSamples){error=5;return;}
        loaded.first=llround(hostStart*loaded.sampleRate);loaded.last=loaded.first+length;
        loaded.origin=(int64_t)std::floor((double)loaded.first/recordingPageSize)*recordingPageSize;
        std::vector<std::shared_ptr<AudioPage>> pages((size_t)((loaded.last-loaded.origin+recordingPageSize-1)/recordingPageSize));
        constexpr int readSize=65536;
        juce::AudioBuffer<float> block(loaded.channels,readSize);
        juce::AudioFormatReaderSource readerSource(reader.get(),false);
        juce::ResamplingAudioSource resampler(&readerSource,false,loaded.channels);
        const bool resample=loaded.sampleRate!=reader->sampleRate;
        if(resample){resampler.setResamplingRatio(reader->sampleRate/loaded.sampleRate);resampler.prepareToPlay(readSize,loaded.sampleRate);}
        for(int64_t position=0;position<length;position+=readSize){
            if(threadShouldExit()||clearing.load())return;const int count=(int)juce::jmin<int64_t>(readSize,length-position);
            if(resample)resampler.getNextAudioBlock({&block,0,count});
            else if(!reader->read(&block,0,count,position,true,true)){error=5;return;}
            for(int i=0;i<count;){const auto relative=loaded.first+position+i-loaded.origin;const size_t page=(size_t)(relative/recordingPageSize),at=(size_t)(relative%recordingPageSize);const int amount=juce::jmin(count-i,recordingPageSize-(int)at);
                if(!pages[page])pages[page]=std::make_shared<AudioPage>();auto& target=*pages[page];
                if(amount==recordingPageSize)target.valid.set();else for(int n=0;n<amount;++n)target.valid.set(at+n);
                for(int c=0;c<loaded.channels;++c){const auto* input=block.getReadPointer(c,i);for(int n=0;n<amount;++n)target.samples[c][at+n]=std::isfinite(input[n])?input[n]:0;}i+=amount;
            }
        }
        work=std::move(loaded);source=std::move(pages);restoreCacheRequested=true;std::atomic_store(&desiredEdits,std::make_shared<const std::vector<PrintEdit>>(work.edits));enabled=true;error=0;dirty(work.first,work.last);
    });
}
void RecordingEngine::setDisplayFft(int value){value=value<=1024?1024:value<=2048?2048:4096;if(requestedFft.exchange(value)!=value)notify();}
bool RecordingEngine::flush(int timeout){const auto target=flushRequested.fetch_add(1)+1;notify();const auto deadline=juce::Time::getMillisecondCounter()+timeout;while(flushDone.load()<target){if((int)(deadline-juce::Time::getMillisecondCounter())<=0)return false;juce::Thread::sleep(2);}return true;}
void RecordingEngine::run(){
    double lastPublish=0;
    while(!threadShouldExit()){
        const auto flushTarget=flushRequested.load();
        busy=true;
        int a,n,b,m;fifo.prepareToRead(fifo.getNumReady(),a,n,b,m);
        for(int i=0;i<n;++i)ingest(packets[(size_t)(a+i)]);for(int i=0;i<m;++i)ingest(packets[(size_t)(b+i)]);fifo.finishedRead(n+m);
        std::vector<std::function<void()>> jobs;{std::lock_guard<std::mutex> lock(commandsMutex);jobs.swap(commands);}
        for(auto& job:jobs){job();pending.fetch_sub(1);}
        const int fft=requestedFft.load();if(work.fftSize!=fft){work.fftSize=fft;work.spectrum.clear();work.originalSpectrum.clear();work.originalPeaks.clear();dirty(work.first,work.last);}
        // Drain fast offline input before spending time on its display FFT.
        const auto now=juce::Time::getMillisecondCounterHiRes();
        const bool urgent=!armed.load()||flushTarget!=flushDone.load()||!jobs.empty();
        if(changed&&!clearing.load()&&(fifo.getNumReady()<queueSize/4||!armed.load())&&(urgent||now-lastPublish>=33.0)){publish();lastPublish=now;}
        retired.erase(std::remove_if(retired.begin(),retired.end(),[](const auto& p){return p.use_count()==1;}),retired.end());
        busy=false;
        // Persist completed derived data on this worker, never in processBlock or
        // the editor. A subsequent gesture cancels the write before publication.
        if(!armed.load()&&!changed&&pending.load()==0&&fifo.getNumReady()==0
            &&(flushTarget!=flushDone.load()||persistRestoredResult)){
            if(auto s=snapshot();s&&!s->empty()&&s->revision!=persistedRevision){
                const auto generation=editRevision.load();
                const auto cancelled=[this,generation]{return threadShouldExit()||clearing.load()||generation!=editRevision.load()||armed.load()||pending.load()>0||changed.load();};
                const auto key=renderCache->key(*s,externalView);
                if(!cancelled()){renderCache->save(key,*s,cancelled);if(!cancelled()){persistedRevision=s->revision;persistRestoredResult=false;}}
            }
        }
        if(fifo.getNumReady()==0&&pending.load()==0&&!changed)flushDone=flushTarget;
        if(fifo.getNumReady()==0&&pending.load()==0)wait(30);
    }
}
void RecordingEngine::apply(const PrintEdit& edit,std::vector<std::shared_ptr<AudioPage>>& output,int64_t left,int64_t right){
    ++printPassCount;
    const int N=edit.fftSize,hop=N/16;const double sr=work.sampleRate;
    const int64_t start=llround(edit.start*sr),end=llround((edit.start+edit.duration)*sr);
    const int64_t firstCentre=juce::jmax(start,work.first-N/2),lastCentre=juce::jmin(end,work.last+N/2);
    if(lastCentre<=firstCentre)return;
    const int64_t beginFrame=(int64_t)std::ceil((double)(firstCentre-N/2-work.origin)/hop)*hop+work.origin;
    const int64_t lastFrame=lastCentre-N/2;
    const int64_t sliceStart=juce::jmax(left,beginFrame),sliceEnd=juce::jmin(right,lastFrame+N+1);
    if(sliceEnd<=sliceStart)return;
    const auto count=(size_t)(sliceEnd-sliceStart);
    juce::dsp::FFT fft((int)std::log2(N));std::vector<float> window((size_t)N),norm((size_t)hop,0),data((size_t)2*N),delta(count),input(count),noiseData((size_t)2*N),noiseDelta(count);
    for(int i=0;i<N;++i){window[(size_t)i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/(N-1));norm[(size_t)(i%hop)]+=window[(size_t)i]*window[(size_t)i];}
    const float floorGain=edit.compensation>0?juce::Decibels::decibelsToGain(-66.0f+42.0f*edit.compensation):0;
    const bool legacyNoise=floorGain>0&&edit.fillVersion==0;
    float windowEnergy=0;for(float value:window)windowEnergy+=value*value;
    std::vector<int> rows((size_t)N/2+1,-1);std::vector<float> edges(rows.size(),0);
    for(int k=0;k<=N/2;++k){const float frequency=2.0f*k/N;if(frequency>=edit.low&&frequency<=edit.high){
        rows[k]=juce::jlimit(0,edit.rows-1,(int)std::round(((frequency-edit.low)/(edit.high-edit.low))*(edit.rows-1)));
        edges[k]=juce::jlimit(0.0f,1.0f,juce::jmin(frequency-edit.low,edit.high-frequency)*N*.25f);
    }}
    const auto seed=(uint64_t)edit.id.hashCode64();
    auto white=[seed](int channel,int64_t sample){
        uint64_t h=(uint64_t)sample+seed+0x9e3779b97f4a7c15ULL*(channel+1);
        h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;h=(h^(h>>27))*0x94d049bb133111ebULL;h^=h>>31;
        return ((float)(h>>40)/8388608.0f-1.0f)*1.7320508f; // unit RMS, deterministic across overlapping frames
    };
    auto read=[&](int c,int64_t index){if(index<work.origin)return 0.0f;const auto page=(size_t)((index-work.origin)/recordingPageSize);return page<output.size()&&output[page]?output[page]->samples[(size_t)c][(size_t)((index-work.origin)%recordingPageSize)]:0.0f;};
    for(int c=0;c<work.channels;++c){
        if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
        std::fill(noiseDelta.begin(),noiseDelta.end(),0.0f);std::fill(delta.begin(),delta.end(),0.0f);for(size_t i=0;i<count;++i){if((i&4095)==0&&(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load()))return;input[i]=read(c,sliceStart+(int64_t)i);}
        for(int64_t frame=beginFrame;frame<lastFrame;frame+=hop){
            if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
            const double t=(frame+N*.5-start)/(edit.duration*sr);const int col=juce::jlimit(0,edit.cols-1,(int)(t*edit.cols));
            std::fill(data.begin(),data.end(),0.0f);
            for(int i=0;i<N;++i){const auto index=frame+i;data[(size_t)i]=(index>=sliceStart&&index<sliceEnd?input[(size_t)(index-sliceStart)]:read(c,index))*window[(size_t)i];}
            fft.performRealOnlyForwardTransform(data.data());
            if(legacyNoise){std::fill(noiseData.begin(),noiseData.end(),0.0f);for(int i=0;i<N;++i)noiseData[(size_t)i]=white(c,frame+i)*window[(size_t)i];fft.performRealOnlyForwardTransform(noiseData.data());}
            for(int k=0;k<=N/2;++k){
                float difference=0,fill=0;const int row=rows[k];
                if(row>=0){
                    const float mask=(*edit.mask)[(size_t)row*edit.cols+col];const float affected=edit.invert?1-mask:mask;difference=(edit.ratio-1)*affected;
                    if(legacyNoise){
                        const float re=data[(size_t)2*k],im=data[(size_t)2*k+1];
                        const float energy=(re*re+im*im)/windowEnergy;
                        const float deficit=std::sqrt(juce::jmax(0.0f,1-energy/(floorGain*floorGain)));
                        // The outer frequency edge is tapered INWARDS by two bins.
                        const float edge=edges[k];
                        fill=floorGain*(1-affected)*deficit*edge;
                    }
                }
                data[(size_t)2*k]*=difference;data[(size_t)2*k+1]*=difference;
                if(legacyNoise){noiseData[(size_t)2*k]*=fill;noiseData[(size_t)2*k+1]*=fill;}
            }
            fft.performRealOnlyInverseTransform(data.data());if(legacyNoise)fft.performRealOnlyInverseTransform(noiseData.data());
            renderDone+=(uint64_t)N*(legacyNoise?2:1);
            if((frame-beginFrame)/hop%16==0)reportProgress(.85f*(float)renderDone/(float)juce::jmax<uint64_t>(1,renderUnits));
            for(int i=0;i<N;++i){const auto index=frame+i;if(index>=sliceStart&&index<sliceEnd){const auto at=(size_t)(index-sliceStart);const float weight=window[(size_t)i]/norm[(size_t)(i%hop)];delta[at]+=data[(size_t)i]*weight;if(legacyNoise)noiseDelta[at]+=noiseData[(size_t)i]*weight;}}
        }
        if(floorGain>0&&!legacyNoise){
            const auto cancelled=[&]{return threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load();};
            if(!targetFill(edit,sr,work.origin,beginFrame,lastFrame,sliceStart,sliceEnd,window,norm,rows,edges,
                [&](int64_t sample){return sample>=sliceStart&&sample<sliceEnd?input[(size_t)(sample-sliceStart)]:read(c,sample);},
                [&](int64_t sample){return white(0,sample);},[&](int64_t sample){return work.contains(sample);},cancelled,
                [&](uint64_t amount){renderDone+=amount;reportProgress(juce::jmin(.84f,.85f*(float)renderDone/(float)juce::jmax<uint64_t>(1,renderUnits)));},noiseDelta))return;
        }
        for(size_t i=0;i<count;++i){if((i&4095)==0&&(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load()))return;const int64_t index=sliceStart+(int64_t)i-work.origin;if(index<0)continue;const size_t page=(size_t)(index/recordingPageSize),at=(size_t)(index%recordingPageSize);if(page<output.size()&&output[page]&&output[page]->valid[at]){
            const int64_t absolute=sliceStart+(int64_t)i;
            // Strict time containment: compensation is exactly zero outside the
            // image, with a 5 ms fade entirely inside its start/end boundaries.
            const float envelope=(absolute>=start&&absolute<end)?juce::jlimit(0.0f,1.0f,(float)juce::jmin(absolute-start,end-1-absolute)/(float)juce::jmax(1.0,juce::jmin(sr*.005,(end-start)*.1))):0;
            output[page]->samples[(size_t)c][at]=input[i]+delta[i]+noiseDelta[i]*std::sin(envelope*juce::MathConstants<float>::halfPi);
        }}
    }
}
void RecordingEngine::analyse(RecordingSnapshot& snapshot,int64_t left,int64_t right){
    ++analysisPassCount;
    const auto analysisStart=juce::Time::getMillisecondCounterHiRes();
    const int N=snapshot.fftSize,hop=N/8;
    const size_t total=(size_t)((snapshot.last-snapshot.origin+RecordingSnapshot::tileSize-1)/RecordingSnapshot::tileSize);
    const bool all=snapshot.spectrum.empty();snapshot.spectrum.resize(total);
    std::vector<float> window((size_t)N);float energy=0;
    for(int i=0;i<N;++i){window[(size_t)i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/(N-1));energy+=window[(size_t)i]*window[(size_t)i];}
    const float normalisation=1/std::sqrt(energy);
    std::vector<size_t> tasks;
    for(size_t tile=0;tile<total;++tile){
        const auto begin=snapshot.origin+(int64_t)tile*RecordingSnapshot::tileSize;
        if(!all&&snapshot.spectrum[tile]&&begin>right+N)break;
        if(!all&&snapshot.spectrum[tile]&&begin+RecordingSnapshot::tileSize<left-N)continue;
        tasks.push_back(tile);
    }
    std::atomic<size_t> nextTask{0},completed{0};
    const auto analyseTiles=[&]{
      juce::dsp::FFT fft((int)std::log2(N));std::vector<float> data((size_t)2*N),mono((size_t)RecordingSnapshot::tileSize+N);
      for(;;){const auto task=nextTask.fetch_add(1);if(task>=tasks.size())break;const auto tile=tasks[task];
        if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
        const auto begin=snapshot.origin+(int64_t)tile*RecordingSnapshot::tileSize;
        // Read and mix immutable pages once per tile, rather than doing two
        // page lookups per sample for each of the eight overlapping FFTs.
        std::fill(mono.begin(),mono.end(),0.f);
        const auto monoStart=begin-N/2;
        for(int64_t absolute=juce::jmax(snapshot.origin,monoStart);absolute<monoStart+(int64_t)mono.size();){
            const auto relative=absolute-snapshot.origin;const size_t page=(size_t)(relative/recordingPageSize),at=(size_t)(relative%recordingPageSize);
            const size_t count=juce::jmin((size_t)recordingPageSize-at,(size_t)(monoStart+mono.size()-absolute));
            if(page<snapshot.rendered.size()&&snapshot.rendered[page]){const auto& audio=*snapshot.rendered[page];auto* dst=mono.data()+(absolute-monoStart);
                if(snapshot.channels==1)std::copy_n(audio.samples[0].data()+at,count,dst);
                else for(size_t i=0;i<count;++i)dst[i]=(audio.samples[0][at+i]+audio.samples[1][at+i])*.5f;}
            absolute+=(int64_t)count;
        }
        auto result=std::make_shared<RecordingSnapshot::Tile>();
        result->frames.reserve(RecordingSnapshot::tileSize/hop);
        for(int offset=0;offset<RecordingSnapshot::tileSize;offset+=hop){
            if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
            const auto centre=begin+offset;SpectrumFrame frame;frame.time=centre/snapshot.sampleRate;frame.sampleRate=snapshot.sampleRate;frame.interval=hop/snapshot.sampleRate;frame.bins=N/2+1;
            if(snapshot.contains(centre)){
                std::fill(data.begin(),data.end(),0.0f);
                for(int i=0;i<N;++i)data[(size_t)i]=mono[(size_t)offset+i]*window[(size_t)i];
                fft.performRealOnlyForwardTransform(data.data());
                for(int k=0;k<frame.bins;++k){const float re=data[(size_t)2*k],im=data[(size_t)2*k+1];const float db=juce::Decibels::gainToDecibels(std::sqrt(re*re+im*im)*normalisation,-96.0f);frame.level[(size_t)k]=(juce::uint8)juce::jlimit(0,255,juce::roundToInt((db+96)*255/96));}
            }
            result->frames.push_back(frame);
        }
        snapshot.spectrum[tile]=std::move(result);
        const auto done=completed.fetch_add(1)+1;
        if(stage.load()==RenderStage::spectrum)reportProgress(.85f+.13f*(float)done/(float)juce::jmax<size_t>(1,tasks.size()));
      }
    };
    // Bound first-load parallelism; short parameter edits stay on one worker.
    const auto threads=tasks.size()<32?1u:juce::jlimit(1u,4u,std::thread::hardware_concurrency()/2);
    std::vector<std::thread> helpers;for(unsigned i=1;i<threads;++i)helpers.emplace_back(analyseTiles);
    analyseTiles();for(auto& helper:helpers)helper.join();
    if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
    snapshot.peaks.resize(snapshot.rendered.size()*16);
    for(size_t page=0;page<snapshot.rendered.size();++page){
        if(threadShouldExit()||clearing.load()||progressGeneration!=editRevision.load())return;
        const auto begin=snapshot.origin+(int64_t)page*recordingPageSize;
        if(!all&&(begin>right||begin+recordingPageSize<left))continue;
        for(int section=0;section<16;++section){float low=0,high=0;if(snapshot.rendered[page])for(int i=section*128;i<(section+1)*128;++i)for(int c=0;c<snapshot.channels;++c){const float v=snapshot.rendered[page]->samples[(size_t)c][(size_t)i];low=juce::jmin(low,v);high=juce::jmax(high,v);}snapshot.peaks[page*16+section]={low,high};}
    }
    analysisTimeMs=analysisTimeMs.load()+juce::Time::getMillisecondCounterHiRes()-analysisStart;
}
void RecordingEngine::cancelPresentation(){
    if(!externalView)return;editRevision.fetch_add(1);
    enqueue([this]{changed=false;dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;});
}
void RecordingEngine::present(std::shared_ptr<const RecordingSnapshot> snapshot){
    jassert(externalView);enabled=true;editRevision.fetch_add(1);
    std::atomic_store(&desiredEdits,std::make_shared<const std::vector<PrintEdit>>(snapshot?snapshot->edits:std::vector<PrintEdit>{}));
    enqueue([this,snapshot]{
        const auto previous=this->snapshot();work=snapshot?*snapshot:RecordingSnapshot{};
        dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;changed=true;
        if(!snapshot){if(auto old=std::atomic_exchange(&published,std::shared_ptr<const RecordingSnapshot>{}))retired.push_back(std::move(old));return;}
        work.fftSize=requestedFft.load();
        const bool reusable=previous&&previous->sourceId==work.sourceId&&previous->sampleRate==work.sampleRate&&previous->channels==work.channels
            &&previous->origin==work.origin&&previous->first==work.first&&previous->last==work.last&&previous->fftSize==work.fftSize;
        if(!reusable){restoreCacheRequested=true;dirty(work.first,work.last);return;}
        work.originalSpectrum=previous->originalSpectrum;work.originalPeaks=previous->originalPeaks;work.spectrum=previous->spectrum;work.peaks=previous->peaks;
        for(size_t page=0;page<work.original.size();++page){const bool raw=page>=previous->original.size()||work.original[page]!=previous->original[page];
            const bool wet=page>=previous->rendered.size()||work.rendered[page]!=previous->rendered[page];
            if(!raw&&!wet)continue;const int64_t a=work.origin+(int64_t)page*recordingPageSize,b=a+recordingPageSize;dirty(a,b);
            if(raw)for(int64_t t=juce::jmax<int64_t>(0,(a-work.origin-work.fftSize)/RecordingSnapshot::tileSize);t<(int64_t)work.originalSpectrum.size()&&work.origin+t*RecordingSnapshot::tileSize<=b+work.fftSize;++t)work.originalSpectrum[(size_t)t].reset();
        }
    });
}
void RecordingEngine::prepareFilePrintExtent(){
    if(!filePrintExtension.load()||externalView||work.empty())return;
    if(work.fileSourceLast<=0)work.fileSourceLast=work.last;
    int64_t begin=0,end=work.fileSourceLast;
    const auto capacity=juce::jmin(maxSamples,(int64_t)(900*work.sampleRate));
    for(const auto& edit:work.edits){
        const auto first=juce::jmin(begin,llround(edit.start*work.sampleRate)),last=juce::jmax(end,llround((edit.start+edit.duration)*work.sampleRate));
        if(last-first<=capacity){begin=first;end=last;}
    }
    const auto origin=(int64_t)std::floor((double)begin/recordingPageSize)*recordingPageSize;
    if(work.origin!=origin){work.rendered.clear();work.spectrum.clear();work.originalSpectrum.clear();work.originalPeaks.clear();work.peaks.clear();dirty(begin,end);}
    work.first=begin;work.last=end;work.origin=origin;
    const size_t pages=(size_t)((end-work.origin+recordingPageSize-1)/recordingPageSize);
    work.original.assign(pages,nullptr);
    const size_t prefix=(size_t)(-origin/recordingPageSize);
    for(size_t p=0;p<source.size();++p)work.original[prefix+p]=source[p];
    // Create silent source coverage only inside Print intervals. Source pages
    // and real captured gaps are never mutated; Delete derives the shorter tail.
    for(const auto& edit:work.edits){
        auto first=juce::jmax(begin,llround(edit.start*work.sampleRate));
        const auto last=juce::jmin(end,llround((edit.start+edit.duration)*work.sampleRate));
        for(auto at=first;at<last;){
            if(at>=0&&at<work.fileSourceLast){at=juce::jmin(last,work.fileSourceLast);continue;}
            const auto relative=at-work.origin;const size_t page=(size_t)(relative/recordingPageSize),offset=(size_t)(relative%recordingPageSize);
            const auto count=(size_t)juce::jmin<int64_t>(recordingPageSize-offset,juce::jmin(last,at<0?int64_t(0):last)-at);
            auto silent=work.original[page]?std::make_shared<AudioPage>(*work.original[page]):std::make_shared<AudioPage>();
            for(size_t i=offset;i<offset+count;++i)silent->valid.set(i);
            work.original[page]=std::move(silent);at+=(int64_t)count;
        }
    }
    work.rendered.resize(pages);work.peaks.resize(pages*16);
    // Trim obsolete derived data after shortening/deleting a tail Print.
    const size_t tiles=(size_t)((end-work.origin+RecordingSnapshot::tileSize-1)/RecordingSnapshot::tileSize);
    if(work.spectrum.size()>tiles)work.spectrum.resize(tiles);
    if(work.originalSpectrum.size()>tiles)work.originalSpectrum.resize(tiles);
    if(work.originalPeaks.size()>pages*16)work.originalPeaks.resize(pages*16);
}
void RecordingEngine::publish(){
    const auto generation=editRevision.load();progressGeneration=generation;reportProgress(0);reportStage(RenderStage::preparing);
    changed=false;if(work.empty()){dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;persistRestoredResult=false;return;}
    if(!externalView)work.original.assign(source.begin(),source.end());
    prepareFilePrintExtent();
    if(std::exchange(restoreCacheRequested,false)&&!armed.load()){
        persistRestoredResult=true;
        const auto key=renderCache->key(work,externalView);
        if(renderCache->load(key,work)){
            if(generation!=editRevision.load())return;
            ++cacheHitCount;work.revision=++revision;persistedRevision=work.revision;persistRestoredResult=false;
            if(auto old=std::atomic_exchange(&published,std::make_shared<const RecordingSnapshot>(work)))retired.push_back(std::move(old));
            dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;reportProgress(1);reportStage(RenderStage::ready);return;
        }
    }
    if(externalView){
        reportStage(RenderStage::spectrum);
        const bool rawReady=!work.originalSpectrum.empty()&&std::all_of(work.originalSpectrum.begin(),work.originalSpectrum.end(),[](const auto& tile){return tile!=nullptr;});
        if(!rawReady){
            auto clean=work;clean.rendered=work.original;clean.spectrum=work.originalSpectrum;clean.peaks=work.originalPeaks;
            analyse(clean,dirtyFirst,dirtyLast);if(generation!=editRevision.load())return;
            work.originalSpectrum=std::move(clean.spectrum);work.originalPeaks=std::move(clean.peaks);
        }
        if(work.original==work.rendered){work.spectrum=work.originalSpectrum;work.peaks=work.originalPeaks;}
        else if(dirtyFirst<=dirtyLast||work.spectrum.empty())analyse(work,dirtyFirst,dirtyLast);
        if(generation!=editRevision.load())return;
        work.revision=++revision;
        auto next=std::make_shared<const RecordingSnapshot>(work);
        if(auto old=std::atomic_exchange(&published,next))retired.push_back(std::move(old));
        dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;reportProgress(1);reportStage(RenderStage::ready);return;
    }
    if(dirtyFirst>dirtyLast){
        work.revision=++revision;
        auto next=std::make_shared<const RecordingSnapshot>(work);
        if(auto old=std::atomic_exchange(&published,next))retired.push_back(std::move(old));
        reportProgress(1.f);reportStage(RenderStage::ready);return;
    }
    int64_t left=juce::jlimit(work.first,work.last,dirtyFirst),right=juce::jlimit(work.first,work.last,dirtyLast);
    // Rebuild the connected group of overlapping edits from ORIGINAL pages.
    // Removing one edit never erases another and never filters an old result twice.
    bool expanded=true;while(expanded){expanded=false;for(const auto& e:work.edits){const int64_t a=llround(e.start*work.sampleRate)-e.fftSize,b=llround((e.start+e.duration)*work.sampleRate)+e.fftSize;if(a<=right&&b>=left){const auto l=juce::jmax(work.first,juce::jmin(left,a)),r=juce::jmin(work.last,juce::jmax(right,b));expanded|=l!=left||r!=right;left=l;right=r;}}}
    work.rendered.resize(work.original.size());
    const size_t firstPage=(size_t)juce::jmax<int64_t>(0,(left-work.origin)/recordingPageSize);
    const size_t lastPage=juce::jmin(work.original.size(),(size_t)juce::jmax<int64_t>(0,(right-work.origin+recordingPageSize-1)/recordingPageSize));
    left=work.origin+(int64_t)firstPage*recordingPageSize;right=work.origin+(int64_t)lastPage*recordingPageSize;
    std::vector<std::shared_ptr<AudioPage>> output(work.original.size());
    for(size_t page=firstPage;page<lastPage;++page)if(work.original[page])output[page]=std::make_shared<AudioPage>(*work.original[page]);
    int relevant=0;bool hasNoise=false;renderUnits=renderDone=0;
    auto units=[&](const PrintEdit& e){const int N=e.fftSize,hop=N/16;
        const int64_t a=juce::jmax((int64_t)llround(e.start*work.sampleRate),work.first-N/2),b=juce::jmin((int64_t)llround((e.start+e.duration)*work.sampleRate),work.last+N/2);
        const int64_t begin=(int64_t)std::ceil((double)(a-N/2-work.origin)/hop)*hop+work.origin;
        return (uint64_t)juce::jmax<int64_t>(0,(b-N/2-begin+hop-1)/hop)*N*work.channels;
    };
    uint64_t baselineUnits=0;
    for(const auto& edit:work.edits)if(edit.start*work.sampleRate-edit.fftSize<=right&&(edit.start+edit.duration)*work.sampleRate+edit.fftSize>=left){++relevant;hasNoise|=edit.compensation>0;const auto n=units(edit);baselineUnits+=n;renderUnits+=n*(edit.compensation>0?(edit.fillVersion==0?2:7):1);}
    if(hasNoise&&relevant>1)renderUnits+=baselineUnits;
    const size_t tileCount=(size_t)((work.last-work.origin+RecordingSnapshot::tileSize-1)/RecordingSnapshot::tileSize);
    const size_t tileFirst=(size_t)juce::jmax<int64_t>(0,(left-work.origin-work.fftSize)/RecordingSnapshot::tileSize);
    const size_t tileLast=juce::jmin(tileCount,(size_t)juce::jmax<int64_t>(0,(right-work.origin+work.fftSize)/RecordingSnapshot::tileSize+1));
    bool originalReady=work.originalSpectrum.size()>=tileLast&&work.originalPeaks.size()>=lastPage*16;
    if(originalReady)for(size_t t=tileFirst;t<tileLast;++t)if(!work.originalSpectrum[t]){originalReady=false;break;}
    // Preserve the pre-Print spectrum alongside the existing original audio pages.
    // Shared immutable tiles cost extra memory only where printing changes them.
    if(!work.edits.empty()&&!originalReady){
        RecordingSnapshot clean=work;clean.rendered=work.original;clean.spectrum=work.originalSpectrum;clean.peaks=work.originalPeaks;
        analyse(clean,left,right);if(generation!=editRevision.load())return;work.originalSpectrum=std::move(clean.spectrum);work.originalPeaks=std::move(clean.peaks);
        originalReady=true;
    }
    const int64_t displayLeft=work.origin+(int64_t)tileFirst*RecordingSnapshot::tileSize-work.fftSize;
    const int64_t displayRight=work.origin+(int64_t)tileLast*RecordingSnapshot::tileSize+work.fftSize;
    const bool otherPrintInTiles=std::any_of(work.edits.begin(),work.edits.end(),[&](const auto& e){
        return e.start*work.sampleRate-e.fftSize<=displayRight&&(e.start+e.duration)*work.sampleRate+e.fftSize>=displayLeft;
    });
    if(relevant==0&&originalReady&&!otherPrintInTiles){
        for(size_t page=firstPage;page<lastPage;++page)work.rendered[page]=work.original[page];
        work.spectrum.resize(tileCount);for(size_t t=tileFirst;t<tileLast;++t)work.spectrum[t]=work.originalSpectrum[t];
        work.peaks.resize(work.rendered.size()*16);
        for(size_t i=firstPage*16;i<lastPage*16;++i)work.peaks[i]=work.originalPeaks[i];
        if(work.edits.empty()&&work.originalSpectrum.size()==tileCount&&work.originalPeaks.size()==work.rendered.size()*16
            &&std::all_of(work.originalSpectrum.begin(),work.originalSpectrum.end(),[](const auto& t){return t!=nullptr;})){
            work.rendered=work.original;work.spectrum=work.originalSpectrum;work.peaks=work.originalPeaks;
        }
        if(generation!=editRevision.load())return;
        work.revision=++revision;auto next=std::make_shared<const RecordingSnapshot>(work);
        if(auto old=std::atomic_exchange(&published,next))retired.push_back(std::move(old));
        dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;reportProgress(1.f);reportStage(RenderStage::ready);return;
    }
    reportStage(RenderStage::printing);
    for(const auto& edit:work.edits){
        if(generation!=editRevision.load())return;
        if(edit.start*work.sampleRate-edit.fftSize<=right&&(edit.start+edit.duration)*work.sampleRate+edit.fftSize>=left)apply(edit,output,left,right);
    }
    if(generation!=editRevision.load())return;
    if(hasNoise&&relevant>1){
        // A later overlapping filter can ring across an earlier noise boundary.
        // Compare against an attenuation-only render and keep added noise strictly
        // inside the union of the compensated image time intervals.
        std::vector<std::shared_ptr<AudioPage>> baseline(work.original.size());
        for(size_t page=firstPage;page<lastPage;++page)if(work.original[page])baseline[page]=std::make_shared<AudioPage>(*work.original[page]);
        for(auto edit:work.edits)if(edit.start*work.sampleRate-edit.fftSize<=right&&(edit.start+edit.duration)*work.sampleRate+edit.fftSize>=left){edit.compensation=0;apply(edit,baseline,left,right);}
        for(size_t page=firstPage;page<lastPage;++page)if(output[page])for(int i=0;i<recordingPageSize;++i){
            if(i==0&&(threadShouldExit()||clearing.load()||generation!=editRevision.load()))return;
            const auto sample=work.origin+(int64_t)page*recordingPageSize+i;bool inside=false;
            for(const auto& edit:work.edits)if(edit.compensation>0&&sample>=llround(edit.start*work.sampleRate)&&sample<llround((edit.start+edit.duration)*work.sampleRate)){inside=true;break;}
            if(!inside)for(int c=0;c<work.channels;++c)output[page]->samples[(size_t)c][(size_t)i]=baseline[page]->samples[(size_t)c][(size_t)i];
        }
    }
    for(size_t page=firstPage;page<lastPage;++page)work.rendered[page]=output[page];
    reportStage(RenderStage::spectrum);reportProgress(.85f);work.revision=++revision;analyse(work,left,right);
    if(work.edits.empty()){work.rendered=work.original;work.originalSpectrum=work.spectrum;work.originalPeaks=work.peaks;}
    // A completed old parameter render must not overwrite a newer request.
    if(generation!=editRevision.load())return;
    auto next=std::make_shared<const RecordingSnapshot>(work);
    if(auto old=std::atomic_exchange(&published,next))retired.push_back(std::move(old));
    dirtyFirst=INT64_MAX;dirtyLast=INT64_MIN;reportProgress(1.f);reportStage(RenderStage::ready);
}
// The DAW state references an immutable cache file. Save does not overwrite an
// earlier project's audio, and never runs in processBlock.
juce::String RecordingEngine::save(){
    std::lock_guard<std::mutex> saveLock(saveMutex);
    if(!flush(120000))return {};
    const auto snap=snapshot();if(!snap||snap->empty())return {};
    if(savedRevision==snap->revision&&juce::File(savedPath).existsAsFile())return savedPath;
    auto folder=Preferences::file().getParentDirectory().getChildFile("Recordings");
    if(folder.createDirectory().failed()){error=4;return {};}
    const auto path=folder.getChildFile(juce::Uuid().toString()+".sptrec");
    auto stream=path.createOutputStream();if(!stream){error=4;return {};}
    stream->writeInt(0x53505435);stream->writeDouble(snap->sampleRate);stream->writeInt(snap->channels);stream->writeInt(snap->fftSize);
    const auto sourceLast=snap->fileSourceLast>0?snap->fileSourceLast:snap->last;
    const auto sourceOrigin=snap->fileSourceLast>0?int64_t(0):snap->origin,sourceFirst=snap->fileSourceLast>0?int64_t(0):snap->first;
    const size_t sourcePages=(size_t)((sourceLast-sourceOrigin+recordingPageSize-1)/recordingPageSize),prefix=(size_t)((sourceOrigin-snap->origin)/recordingPageSize);
    stream->writeInt64(sourceOrigin);stream->writeInt64(sourceFirst);stream->writeInt64(sourceLast);stream->writeInt((int)sourcePages);
    for(size_t p=0;p<sourcePages;++p){const auto& page=snap->original[prefix+p];stream->writeBool(page!=nullptr);if(page){for(int i=0;i<recordingPageSize;i+=8){unsigned bits=0;for(int j=0;j<8;++j)if(page->valid[(size_t)(i+j)]&&sourceOrigin+(int64_t)p*recordingPageSize+i+j<sourceLast)bits|=1u<<j;stream->writeByte((char)bits);}for(int c=0;c<snap->channels;++c)RenderCache::writeFloats(*stream,page->samples[(size_t)c].data(),recordingPageSize);}}
    stream->writeInt((int)snap->edits.size());for(const auto& e:snap->edits){stream->writeString(e.id);stream->writeString(e.imagePath);stream->writeDouble(e.start);stream->writeDouble(e.duration);stream->writeFloat(e.low);stream->writeFloat(e.high);stream->writeFloat(e.ratio);stream->writeFloat(e.compensation);stream->writeInt(e.fftSize);stream->writeInt(e.rows);stream->writeInt(e.cols);stream->writeBool(e.invert);stream->writeFloat(e.speed);stream->writeFloat(e.amount);stream->writeFloat(e.displayTop);stream->writeFloat(e.imageAspect);stream->writeDouble(e.viewZoom);stream->writeInt(e.imageMode);stream->writeInt(e.scaleMode);stream->writeInt(e.fillVersion);for(float v:*e.mask)stream->writeFloat(v);}
    stream->flush();if(stream->getStatus().failed()){error=4;return {};}
    savedRevision=snap->revision;savedPath=path.getFullPathName();return savedPath;
}
bool RecordingEngine::load(const juce::String& path){
    auto input=juce::File(path).createInputStream();if(!input||input->getTotalLength()>1073741824)return false;const int format=input->readInt();if(format<0x53505431||format>0x53505435)return false;
    RecordingSnapshot fresh;fresh.sampleRate=input->readDouble();fresh.channels=input->readInt();fresh.fftSize=input->readInt();fresh.origin=input->readInt64();fresh.first=input->readInt64();fresh.last=input->readInt64();const int pages=input->readInt();
    if(!std::isfinite(fresh.sampleRate)||fresh.sampleRate<8000||fresh.sampleRate>384000||fresh.channels<1||fresh.channels>2||pages<1||pages>(maxSamples/recordingPageSize+2)||fresh.last<=fresh.first||fresh.first<fresh.origin||fresh.last-fresh.origin>(int64_t)pages*recordingPageSize)return false;
    if(fresh.fftSize!=1024&&fresh.fftSize!=2048&&fresh.fftSize!=4096)return false;
    std::vector<std::shared_ptr<AudioPage>> incoming((size_t)pages);
    for(auto& page:incoming){if(input->isExhausted())return false;if(input->readBool()){if(input->getTotalLength()-input->getPosition()<recordingPageSize/8+fresh.channels*recordingPageSize*4)return false;page=std::make_shared<AudioPage>();for(int i=0;i<recordingPageSize;i+=8){const auto bits=(unsigned char)input->readByte();for(int j=0;j<8;++j)page->valid[(size_t)(i+j)]=(bits&(1u<<j))!=0;}for(int c=0;c<fresh.channels;++c)if(!RenderCache::readFloats(*input,page->samples[(size_t)c].data(),recordingPageSize))return false;}}
    if(input->getTotalLength()-input->getPosition()<4)return false;
    const int edits=input->readInt();if(edits<0||edits>1024)return false;
    for(int i=0;i<edits;++i){PrintEdit e;e.id=input->readString();e.imagePath=input->readString();e.start=input->readDouble();e.duration=input->readDouble();e.low=input->readFloat();e.high=input->readFloat();e.ratio=input->readFloat();if(format>=0x53505432)e.compensation=input->readFloat();e.fftSize=input->readInt();e.rows=input->readInt();e.cols=input->readInt();e.invert=input->readBool();if(format>=0x53505433){e.speed=input->readFloat();e.amount=input->readFloat();e.displayTop=input->readFloat();e.imageAspect=input->readFloat();e.viewZoom=input->readDouble();e.imageMode=input->readInt();e.scaleMode=input->readInt();if(!std::isfinite(e.speed)||e.speed<.1f||e.speed>4||!std::isfinite(e.amount)||e.amount<0||e.amount>1||!std::isfinite(e.displayTop)||e.displayTop<21||e.displayTop>48000||!std::isfinite(e.imageAspect)||e.imageAspect<=0||!std::isfinite(e.viewZoom)||e.viewZoom<=0||e.imageMode<0||e.imageMode>3||e.scaleMode<0||e.scaleMode>6)return false;}e.fillVersion=format>=0x53505435?input->readInt():0;if(e.rows<1||e.rows>2049||e.cols<1||e.cols>4096||(int64_t)e.rows*e.cols*(format>=0x53505435?4:1)>input->getTotalLength()-input->getPosition())return false;auto mask=std::make_shared<std::vector<float>>((size_t)e.rows*e.cols);for(float& v:*mask){v=format>=0x53505435?input->readFloat():(unsigned char)input->readByte()/255.0f;if(!std::isfinite(v)||v<0||v>1)return false;}e.mask=mask;if(!e.valid())return false;
        // Recorded view has always used a fixed 468-pixel logical plot. SPT4
        // changes timing metadata only; keep original audio, mask and duration.
        if(format<0x53505434)timing::migrateLegacy(e.scaleMode,468,e.speed,e.viewZoom);
        // Convert the retired name only after the older timing migration.
        // Its frequency transform is identical to Log; preserve physical audio.
        if(e.scaleMode==frequency::superVision)e.scaleMode=frequency::logarithmic;
        fresh.edits.push_back(std::move(e));}
    work=std::move(fresh);source=std::move(incoming);restoreCacheRequested=true;std::atomic_store(&desiredEdits,std::make_shared<const std::vector<PrintEdit>>(work.edits));dirty(work.first,work.last);return true;
}
void RecordingEngine::restore(const juce::String& path,bool wasEnabled){
    armed=false;captureEpoch.fetch_add(1);enabled=wasEnabled;editRevision.fetch_add(1);std::atomic_store(&desiredEdits,std::make_shared<const std::vector<PrintEdit>>());
    enqueue([this,path]{work={};source.clear();if(auto old=std::atomic_exchange(&published,std::shared_ptr<const RecordingSnapshot>{}))retired.push_back(std::move(old));error=0;if(path.isNotEmpty()&&!load(path))error=4;});
}
}
