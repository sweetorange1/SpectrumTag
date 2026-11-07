#include "ARAIntegration.h"
#include <cmath>
#include <limits>
namespace spectrumtag::ara {
namespace {
constexpr int64_t maxSourceSamples=64000000;
constexpr double maxViewDuration=900.;
template<class T> void erasePointer(std::vector<T*>& v,T* p){v.erase(std::remove(v.begin(),v.end(),p),v.end());}
std::vector<Region*> trackRegions(const std::vector<Region*>& regions){
    std::vector<Region*> result;std::set<juce::ARARegionSequence*> sequences;
    for(auto* r:regions){if(auto* seq=r->getRegionSequence<juce::ARARegionSequence>())sequences.insert(seq);else result.push_back(r);}
    for(auto* seq:sequences)for(auto* r:seq->getPlaybackRegions<Region>())result.push_back(r);
    std::sort(result.begin(),result.end(),[](auto* a,auto* b){return a->key<b->key;});
    result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
std::shared_ptr<const std::vector<PrintEdit>> immutableEdits(const std::vector<PrintEdit>& edits){return std::make_shared<const std::vector<PrintEdit>>(edits);}
}
Modification::Modification(juce::ARAAudioSource* source,ARA::ARAAudioModificationHostRef ref,const juce::ARAAudioModification* clone)
 :juce::ARAAudioModification(source,ref,clone){if(clone){const auto* old=static_cast<const Modification*>(clone);edits=old->edits;viewFrequencyScale=old->viewFrequencyScale;}}

// Crop at the target Nyquist without stretching high-frequency image rows downwards.
bool convertFrequencyRange(PrintEdit& e,double from,double to)
{
    const float low=e.low*(float)(from/to),high=e.high*(float)(from/to);
    if(low>=1.f||high<=low)return false;
    const float top=juce::jmin(1.f,high);
    if(top<high&&e.mask&&e.rows>1){
        auto cropped=std::make_shared<std::vector<float>>((size_t)e.rows*e.cols);
        for(int row=0;row<e.rows;++row){
            const float oldRow=row*(top-low)/(high-low);
            const int a=juce::jlimit(0,e.rows-1,(int)std::floor(oldRow)),b=juce::jmin(e.rows-1,a+1);
            const float mix=oldRow-a;
            for(int col=0;col<e.cols;++col){
                const float x=(*e.mask)[(size_t)a*e.cols+col],y=(*e.mask)[(size_t)b*e.cols+col];
                (*cropped)[(size_t)row*e.cols+col]=x+(y-x)*mix;
            }
        }
        e.mask=std::move(cropped);
    }
    e.low=low;e.high=top;return true;
}
float readSample(const RecordingSnapshot& s,int channel,double seconds,double outputRate,bool processed)noexcept{
    const double at=seconds*s.sampleRate;
    const auto nearest=(int64_t)llround(at);
    if(outputRate>=s.sampleRate&&std::abs(at-nearest)<1.e-7)return s.sample(channel,nearest,processed);
    // Windowed sinc prevents aliasing when a host's rate differs from the source.
    const double cutoff=juce::jmin(1.,outputRate/s.sampleRate),pi=juce::MathConstants<double>::pi;
    constexpr int radius=16;const auto centre=(int64_t)std::floor(at);double sum=0,weight=0;
    for(int k=-radius+1;k<=radius;++k){const auto n=centre+k;const double d=at-n;
        if(std::abs(d)>=radius)continue;
        const double x=pi*d*cutoff;
        const double w=cutoff*(std::abs(x)<1.e-10?1:std::sin(x)/x)*(.5+.5*std::cos(pi*d/radius));
        weight+=w;sum+=w*s.sample(channel,n,processed);
    }
    return weight!=0?(float)(sum/weight):0.f;
}
void renderSpans(const std::vector<Span>& spans,juce::AudioBuffer<float>& out,double start,double rate)noexcept{
    out.clear();if(!std::isfinite(start)||rate<8000)return;
    for(const auto& span:spans){
        const auto audio=span.frozen?span.frozen:span.runtime->audio();if(!audio)continue;
        const int first=(int)juce::jlimit(0.,(double)out.getNumSamples(),std::ceil((span.start-start)*rate-1.e-7));
        const int last=(int)juce::jlimit(0.,(double)out.getNumSamples(),std::ceil((span.end-start)*rate-1.e-7));
        for(int i=first;i<last;++i){const double t=span.sourceTime(start+i/rate);
            for(int c=0;c<out.getNumChannels();++c){float value=0;
                if(out.getNumChannels()==1&&audio->channels==2)value=(readSample(*audio,0,t,rate)+readSample(*audio,1,t,rate))*.5f;
                else value=readSample(*audio,juce::jmin(c,audio->channels-1),t,rate);
                out.addSample(c,i,value);
            }
        }
    }
}
Playback::Playback(Controller& c):juce::ARAPlaybackRenderer(c.getDocumentController()),owner(c){owner.players.push_back(this);}
Playback::~Playback(){erasePointer(owner.players,this);}
void Playback::addPlaybackRegion(ARA::ARAPlaybackRegionRef ref)noexcept{juce::ARAPlaybackRenderer::addPlaybackRegion(ref);rebuild();}
void Playback::removePlaybackRegion(ARA::ARAPlaybackRegionRef ref)noexcept{juce::ARAPlaybackRenderer::removePlaybackRegion(ref);rebuild();}
void Playback::prepareToPlay(double sr,int n,int c,juce::AudioProcessor::ProcessingPrecision p,AlwaysNonRealtime a){
    juce::ARAPlaybackRenderer::prepareToPlay(sr,n,c,p,a);rate=sr;
    // Region plans are published only by model callbacks, never by the audio thread.
}
void Playback::rebuild(){
    auto next=std::make_shared<std::vector<Span>>();
    for(auto* region:getPlaybackRegions<Region>())if(auto s=owner.span(*region))next->push_back(*s);
    if(auto old=std::atomic_exchange(&plan,std::shared_ptr<const std::vector<Span>>(next)))retiredPlans.push_back(std::move(old));
    retirePlans();
}
void Playback::retirePlans(){retiredPlans.erase(std::remove_if(retiredPlans.begin(),retiredPlans.end(),[](const auto& p){return p.use_count()==1;}),retiredPlans.end());}
bool Playback::processBlock(juce::AudioBuffer<float>& buffer,juce::AudioProcessor::Realtime rt,const juce::AudioPlayHead::PositionInfo& p)noexcept{
    if(rt==juce::AudioProcessor::Realtime::yes&&!p.getIsPlaying()){buffer.clear();return true;}
    double time=0;if(auto s=p.getTimeInSamples())time=*s/rate;else if(auto s=p.getTimeInSeconds())time=*s;else{buffer.clear();return true;}
    const auto current=std::atomic_load(&plan);
    if(rt==juce::AudioProcessor::Realtime::no)for(const auto& span:*current)
        if(auto engine=std::atomic_load(&span.runtime->engine))while(engine->isBusy()&&span.runtime->active.load())engine->flush(1000);
    renderSpans(*current,buffer,time,rate);return true;
}
Composer::Composer():juce::Thread("SpectrumTag ARA overview"){startThread();}
Composer::~Composer(){signalThreadShouldExit();notify();stopThread(-1);}
uint64_t Composer::request(Job job){const auto id=++generation;job.id=id;{std::lock_guard<std::mutex> l(mutex);pending=std::move(job);failure.clear();}working=true;notify();return id;}
uint64_t Composer::cancel(){std::lock_guard<std::mutex> l(mutex);const auto id=++generation;pending.reset();working=false;return id;}
std::shared_ptr<const RecordingSnapshot> Composer::result(uint64_t& id)const{std::lock_guard<std::mutex> l(mutex);id=completedId;return completed;}
juce::String Composer::error()const{std::lock_guard<std::mutex> l(mutex);return failure;}
void Composer::run(){std::optional<Job> previousJob;std::shared_ptr<const RecordingSnapshot> previous;while(!threadShouldExit()){
    std::optional<Job> job;{std::lock_guard<std::mutex> l(mutex);job.swap(pending);}if(!job){wait(40);continue;}
    auto out=std::make_shared<RecordingSnapshot>();out->sourceId=job->key;out->sampleRate=job->rate;out->edits=job->edits;out->channels=1;
    double begin=std::numeric_limits<double>::max(),end=-begin;
    for(const auto& s:job->spans)if(s.frozen){begin=juce::jmin(begin,s.start);end=juce::jmax(end,s.end);out->channels=juce::jmax(out->channels,s.frozen->channels);}
    if(end<=begin){std::lock_guard<std::mutex> l(mutex);if(job->id==generation){completed={};completedId=job->id;working=false;}continue;}
    if(end-begin>maxViewDuration||(end-begin)*job->rate>maxSourceSamples){
        std::lock_guard<std::mutex> l(mutex);if(job->id==generation){failure="Selection exceeds the 15-minute / 64M-sample view limit. Select fewer events.";working=false;}continue;
    }
    out->first=llround(begin*job->rate);out->last=llround(end*job->rate);out->origin=(int64_t)std::floor((double)out->first/recordingPageSize)*recordingPageSize;
    const size_t count=(size_t)((out->last-out->origin+recordingPageSize-1)/recordingPageSize);
    // Reuse the timeline's immutable original pages. A Print changes only wet
    // source pages; event moves, rate changes and source replacement take the
    // full path. Every overlapping event is mixed again on each affected page.
    bool compatible=previous&&previousJob&&previous->sourceId==out->sourceId&&previous->sampleRate==out->sampleRate
        &&previous->channels==out->channels&&previous->first==out->first&&previous->last==out->last&&previousJob->spans.size()==job->spans.size();
    if(compatible)for(size_t i=0;i<job->spans.size();++i){const auto& a=job->spans[i];const auto& b=previousJob->spans[i];
        if(a.start!=b.start||a.end!=b.end||a.sourceStart!=b.sourceStart||!a.frozen||!b.frozen||a.frozen->sampleRate!=b.frozen->sampleRate
            ||a.frozen->origin!=b.frozen->origin||a.frozen->original!=b.frozen->original){compatible=false;break;}}
    std::vector<bool> dirty(count,!compatible);bool edited=false;
    for(size_t i=0;i<job->spans.size();++i){const auto& span=job->spans[i];if(!span.frozen)continue;const auto& audio=*span.frozen;edited|=audio.original!=audio.rendered;
        if(!compatible)continue;const auto& old=*previousJob->spans[i].frozen;
        for(size_t page=0;page<audio.rendered.size();++page)if(page>=old.rendered.size()||audio.rendered[page]!=old.rendered[page]){
            const double a=span.start+(audio.origin+(int64_t)page*recordingPageSize-32)/audio.sampleRate-span.sourceStart;
            const double b=a+(recordingPageSize+64)/audio.sampleRate;
            const int64_t first=(int64_t)std::floor((juce::jmax(a,span.start)*job->rate-out->origin)/recordingPageSize);
            const int64_t last=(int64_t)std::floor((juce::jmin(b,span.end)*job->rate-out->origin)/recordingPageSize);
            for(int64_t n=juce::jmax<int64_t>(0,first);n<=last&&n<(int64_t)count;++n)dirty[(size_t)n]=true;
        }
    }
    std::vector<std::shared_ptr<AudioPage>> raw(count),wet(count);
    if(compatible){out->original=previous->original;out->rendered=previous->rendered;}
    for(const auto& s:job->spans){if(!s.frozen)continue;const auto& audio=*s.frozen;
        const int64_t a=juce::jmax(out->first,(int64_t)std::ceil(s.start*job->rate-1.e-7)),b=juce::jmin(out->last,(int64_t)std::ceil(s.end*job->rate-1.e-7));
        for(int64_t n=a;n<b;){if((threadShouldExit()||job->id!=generation))break;
            const size_t page=(size_t)((n-out->origin)/recordingPageSize);
            const int64_t end=juce::jmin(b,out->origin+(int64_t)(page+1)*recordingPageSize);
            if(!dirty[page]||(compatible&&!edited)){n=end;continue;}
            for(;n<end;++n){const double t=s.sourceTime(n/job->rate);if(!audio.contains((int64_t)std::floor(t*audio.sampleRate+1.e-7)))continue;
                const size_t at=(size_t)((n-out->origin)%recordingPageSize);
                if(!wet[page])wet[page]=std::make_shared<AudioPage>();wet[page]->valid.set(at);
                if(!compatible){if(!raw[page])raw[page]=std::make_shared<AudioPage>();raw[page]->valid.set(at);}
                for(int c=0;c<out->channels;++c){if(!compatible)raw[page]->samples[c][at]+=readSample(audio,c,t,job->rate,false);wet[page]->samples[c][at]+=readSample(audio,c,t,job->rate,true);}
            }
        }
        if(threadShouldExit()||job->id!=generation)break;
    }
    if(job->id!=generation||threadShouldExit())continue;
    if(!compatible){out->original.assign(raw.begin(),raw.end());out->rendered.assign(wet.begin(),wet.end());}
    else if(edited)for(size_t page=0;page<count;++page)if(dirty[page])out->rendered[page]=wet[page];
    if(!edited)out->rendered=out->original;
    previous=out;previousJob=*job;
    {std::lock_guard<std::mutex> l(mutex);if(job->id==generation){completed=out;completedId=job->id;working=false;}}
}}
Controller::Controller(const ARA::PlugIn::PlugInEntry* e,const ARA::ARADocumentControllerHostInstance* h):juce::ARADocumentControllerSpecialisation(e,h){startTimerHz(25);}
Controller::~Controller(){stopTimer();}
juce::ARAAudioSource* Controller::doCreateAudioSource(juce::ARADocument* d,ARA::ARAAudioSourceHostRef h){return new Source(d,h);}
juce::ARAAudioModification* Controller::doCreateAudioModification(juce::ARAAudioSource* s,ARA::ARAAudioModificationHostRef h,const juce::ARAAudioModification* c){return new Modification(s,h,c);}
juce::ARAPlaybackRegion* Controller::doCreatePlaybackRegion(juce::ARAAudioModification* m,ARA::ARAPlaybackRegionHostRef h){return new Region(m,h);}
juce::ARAPlaybackRenderer* Controller::doCreatePlaybackRenderer(){return new Playback(*this);}
juce::ARAEditorView* Controller::doCreateEditorView(){return new View(*this);}
void Controller::invalidate(Source& s){if(s.raw)retiredAudio.push_back(s.raw);s.raw.reset();for(auto* m:s.getAudioModifications<Modification>()){
    std::atomic_store(&m->runtime->raw,std::shared_ptr<const RecordingSnapshot>{});
    if(auto e=std::atomic_load(&m->runtime->engine))e->restoreDocument({});
}}
bool Controller::cache(Source& source){
    if(source.raw)return true;
    if(!source.isSampleAccessEnabled()||source.isDeactivatedForUndoHistory()){source.error="Waiting for ARA audio access. Click Load to retry.";return false;}
    const auto length=source.getSampleCount();const double rate=source.getSampleRate();const int channels=source.getChannelCount();
    if(length<=0||length>maxSourceSamples||rate<8000||rate>384000||channels<1||channels>2){source.error="This build supports mono/stereo sources up to 64M samples at 8-384 kHz.";return false;}
    try{
        juce::ARAAudioSourceReader reader(&source);if(!reader.isValid()){source.error="Cannot read this ARA source. Click Load to retry.";return false;}
        auto out=std::make_shared<RecordingSnapshot>();out->sampleRate=rate;out->channels=channels;out->last=length;
        out->original.reserve((size_t)((length+recordingPageSize-1)/recordingPageSize));juce::AudioBuffer<float> buffer(channels,recordingPageSize);
        for(int64_t position=0;position<length;position+=recordingPageSize){const int n=(int)juce::jmin<int64_t>(recordingPageSize,length-position);
            if(!reader.read(&buffer,0,n,position,true,true)){source.error="ARA audio read failed. No partial source was accepted; click Load to retry.";return false;}
            auto page=std::make_shared<AudioPage>();for(int i=0;i<n;++i){page->valid.set((size_t)i);for(int c=0;c<channels;++c){const float v=buffer.getSample(c,i);page->samples[c][i]=std::isfinite(v)?v:0;}}
            out->original.push_back(page);
        }
        out->rendered=out->original;source.raw=out;source.error.clear();return true;
    }catch(...){source.error="Not enough memory to load this ARA source.";return false;}
}
void Controller::sync(Modification& m,bool notify){
    auto* source=m.getAudioSource<Source>();const auto raw=source->raw;
    const auto old=std::atomic_load(&m.runtime->raw);std::atomic_store(&m.runtime->raw,raw);
    m.runtime->active=!m.isDeactivatedForUndoHistory()&&!source->isDeactivatedForUndoHistory()&&source->isSampleAccessEnabled();
    auto e=std::atomic_load(&m.runtime->engine);
    if(raw&&(!m.edits.empty()||e)){
        if(!e){e=std::make_shared<RecordingEngine>();std::atomic_store(&m.runtime->engine,e);}
        if(raw!=old||!e->isEnabled())e->restoreDocument({raw,immutableEdits(m.edits),true});else e->setEdits(m.edits);
    }
    if(notify){m.notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(),true);for(auto* r:m.getPlaybackRegions())r->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(),true);}
}
void Controller::warmup(){if(auto* d=getDocument())for(auto* s:d->getAudioSources<Source>()){
    if(s->isSampleAccessEnabled()&&!s->isDeactivatedForUndoHistory())cache(*s);
    for(auto* m:s->getAudioModifications<Modification>())sync(*m,false);
}}
std::optional<Span> Controller::span(Region& r)const{
    auto* m=r.getAudioModification<Modification>();auto* s=m->getAudioSource<Source>();
    const double duration=r.getDurationInPlaybackTime(),sourceDuration=r.getDurationInAudioModificationTime();
    // No time-stretch capability is advertised. Never substitute pitch-changing resampling for elastic audio.
    if(!std::isfinite(duration)||duration<=0||std::abs(duration-sourceDuration)>1.e-7||m->isDeactivatedForUndoHistory()||s->isDeactivatedForUndoHistory())return {};
    return Span{r.getStartInPlaybackTime(),r.getStartInPlaybackTime()+duration,r.getStartInAudioModificationTime(),m->runtime,{}};
}
void Controller::didEndEditing(juce::ARADocument*){warmup();for(auto* p:players)p->rebuild();poll();}
void Controller::didEnableAudioSourceSamplesAccess(juce::ARAAudioSource* base,bool enable){auto& s=*static_cast<Source*>(base);if(enable)cache(s);else invalidate(s);for(auto* m:s.getAudioModifications<Modification>())sync(*m,false);}
void Controller::didDeactivateAudioSourceForUndoHistory(juce::ARAAudioSource* base,bool off){auto& s=*static_cast<Source*>(base);if(off)invalidate(s);else cache(s);for(auto* m:s.getAudioModifications<Modification>())sync(*m,false);}
void Controller::didDeactivateAudioModificationForUndoHistory(juce::ARAAudioModification* m,bool){sync(*static_cast<Modification*>(m),false);}
void Controller::didUpdateAudioSourceProperties(juce::ARAAudioSource* s){auto& source=*static_cast<Source*>(s);invalidate(source);if(source.isSampleAccessEnabled())cache(source);for(auto* m:source.getAudioModifications<Modification>())sync(*m,false);}
void Controller::doUpdateAudioSourceContent(juce::ARAAudioSource* s,juce::ARAContentUpdateScopes){didUpdateAudioSourceProperties(s);}
void Controller::willDestroyRegionSequence(juce::ARARegionSequence* s){for(auto* v:views)v->remove(s);}
void Controller::willDestroyPlaybackRegion(juce::ARAPlaybackRegion* r){for(auto* v:views)v->remove(static_cast<Region*>(r));}
void Controller::willDestroyAudioSource(juce::ARAAudioSource* s){if(auto raw=static_cast<Source*>(s)->raw)retiredAudio.push_back(std::move(raw));}
void Controller::willDestroyAudioModification(juce::ARAAudioModification* m){retiredRuntimes.push_back(static_cast<Modification*>(m)->runtime);}
void Controller::poll(){
    if(getDocumentController()->isHostEditingDocument())return;
    for(auto* v:views)v->tick();for(auto* p:players)p->retirePlans();
    retiredAudio.erase(std::remove_if(retiredAudio.begin(),retiredAudio.end(),[](const auto& p){return p.use_count()==1;}),retiredAudio.end());
    retiredRuntimes.erase(std::remove_if(retiredRuntimes.begin(),retiredRuntimes.end(),[](const auto& p){return p.use_count()==1;}),retiredRuntimes.end());
}
View::View(Controller& c):juce::ARAEditorView(c.getDocumentController()),owner(c){owner.views.push_back(this);engine.onExternalEdits=[this](const auto& edits){applyEdits(edits);};engine.present({});}
View::~View(){engine.onExternalEdits={};erasePointer(owner.views,this);}
void View::setTrackScope(const std::vector<Region*>& regions){
    tracks.clear();ungrouped.clear();
    for(auto* r:regions){if(auto* seq=r->getRegionSequence<juce::ARARegionSequence>()){
        if(std::find(tracks.begin(),tracks.end(),seq)==tracks.end())tracks.push_back(seq);
    }else ungrouped.push_back(r);}
}
std::vector<Region*> View::regionsInScope()const{
    auto regions=ungrouped;for(auto* seq:tracks)for(auto* r:seq->getPlaybackRegions<Region>())regions.push_back(r);
    std::sort(regions.begin(),regions.end(),[](auto* a,auto* b){return a->key<b->key;});
    regions.erase(std::unique(regions.begin(),regions.end()),regions.end());return regions;
}
void View::remove(juce::ARARegionSequence* seq){erasePointer(tracks,seq);}
void View::remove(Region* r){
    erasePointer(ungrouped,r);
    if(std::find(selected.begin(),selected.end(),r)==selected.end())return;
    erasePointer(selected,r);signature.clear();++selectionRevision;
    for(auto it=projections.begin();it!=projections.end();)if(it->second.region==r)it=projections.erase(it);else ++it;
    requested=composer.request({});engine.present({});
}
void View::doNotifySelection(const ARA::PlugIn::ViewSelection* selection)noexcept{
    juce::ARAEditorView::doNotifySelection(selection);
    try{
        auto next=selection?selection->getEffectivePlaybackRegions<Region>():std::vector<Region*>{};
        setTrackScope(next);next=regionsInScope();
        if(next!=selected){if(beforeSelection)beforeSelection();selected=std::move(next);signature.clear();++selectionRevision;key.clear();projections.clear();engine.present({});}
        explicitSelection=true;
        if(manualLoadOnly){key.clear();for(auto* r:selected)key+=r->key+":";return;}
        const bool firstLoad=!loaded;loaded=true;if(firstLoad)owner.warmup();tick();
    }catch(...){status="Unable to load the ARA selection.";}
}
void View::load(const std::vector<Region*>& fallback){
    if(!explicitSelection&&selected.empty()){
        selected=trackRegions(fallback);
        if(selected.empty())for(auto* s:owner.getDocument()->getRegionSequences())for(auto* r:s->getPlaybackRegions<Region>())selected.push_back(r);
        ++selectionRevision;
    }
    setTrackScope(selected);selected=regionsInScope();manualLoadOnly=false;loaded=true;owner.warmup();tick();
}
std::optional<int> View::frequencyChoice()const{
    if(selected.empty()||getDocumentController()->isHostEditingDocument())return {};
    const int value=selected.front()->getAudioModification<Modification>()->viewFrequencyScale;
    return value>=0?std::optional<int>(value):std::nullopt;
}
void View::rememberFrequencyChoice(int value){
    if(getDocumentController()->isHostEditingDocument()||value<0||value>6)return;
    if(value==3)value=1;bool changed=false;
    for(auto* r:selected){auto* m=r->getAudioModification<Modification>();if(m->viewFrequencyScale!=value){m->viewFrequencyScale=value;changed=true;}}
    if(changed)if(auto* updates=getDocumentController()->getHostModelUpdateController())updates->notifyDocumentDataChanged();
}
ViewState View::captureState()const{
    ViewState state;state.loaded=loaded;state.manualLoadOnly=manualLoadOnly;
    std::set<Modification*> seen;
    for(auto* region:selected){auto* m=region->getAudioModification<Modification>();
        if(seen.insert(m).second)state.modifications.push_back({m->getPersistentID(),m->edits});
    }
    return state;
}
void View::clear(){
    if(getDocumentController()->isHostEditingDocument())return;
    auto state=captureState();for(auto& entry:state.modifications)entry.edits.clear();
    state.loaded=false;state.manualLoadOnly=true;restoreState(state);
}
void View::restoreState(const ViewState& state){
    if(getDocumentController()->isHostEditingDocument())return;
    // Stable IDs, not raw model pointers, make the undo record safe after host edits.
    for(auto* source:owner.getDocument()->getAudioSources<Source>())for(auto* m:source->getAudioModifications<Modification>())
        for(const auto& entry:state.modifications)if(m->getPersistentID()==entry.modification){
            m->edits=entry.edits;owner.sync(*m,true);break;
        }
    loaded=state.loaded;manualLoadOnly=state.manualLoadOnly;signature.clear();projections.clear();
    requested=composer.request({});engine.present({});engine.setExternalProgress(false,1);
    if(loaded){owner.warmup();tick(true);}
    else status="Cleared. Select audio events, then click Load to start again.";
}
juce::String View::statusText(bool chinese) const
{
    if(!chinese||status.isEmpty())return status;
    struct Translation {const char* en;const char* zh;};
    static const Translation translations[] {
        {"Cleared. Select audio events, then click Load to start again.","已清空。请选择音频事件，再点击加载重新开始。"},
        {"Select audio events in the DAW, then click Load.","在 DAW 中选择音频事件，然后点击加载。"},
        {"Unsupported stretched event. Render the event in the DAW before loading.","暂不支持拉伸后的事件；请先在 DAW 中渲染事件，再加载。"},
        {"Waiting for ARA audio access. Click Load to retry.","等待宿主开放 ARA 音频读取；点击加载重试。"},
        {"This build supports mono/stereo sources up to 64M samples at 8-384 kHz.","当前支持 8–384 kHz 单声道或立体声音源，每个音源最多 6400 万采样。"},
        {"Cannot read this ARA source. Click Load to retry.","无法读取此 ARA 音源；点击加载重试。"},
        {"ARA audio read failed. No partial source was accepted; click Load to retry.","ARA 音频读取失败，未采用不完整数据；点击加载重试。"},
        {"Not enough memory to load this ARA source.","内存不足，无法加载此 ARA 音源。"},
        {"Unable to load the ARA selection.","无法加载当前 ARA 选择。"},
        {"This host did not provide an ARA playback controller.","当前宿主未提供 ARA 播放指针控制接口。"},
        {"Selection exceeds the 15-minute / 64M-sample view limit. Select fewer events.","所选范围超过 15 分钟或 6400 万采样的视图上限；请减少所选事件。"}
    };
    for(const auto& item:translations)if(status==item.en)return juce::String::fromUTF8(item.zh);
    return status;
}
void View::seek(double seconds){
    if(!std::isfinite(seconds)||std::abs(seconds)>=1.e9)return;
    if(auto* transport=getDocumentController()->getHostPlaybackController())transport->requestSetPlaybackPosition(seconds);
    else status="This host did not provide an ARA playback controller.";
}
void View::tick(bool force){
    if(!loaded||getDocumentController()->isHostEditingDocument())return;
    const auto track=regionsInScope();
    if(track!=selected){if(beforeSelection)beforeSelection();selected=track;signature.clear();++selectionRevision;}

    if(restoreRevision!=owner.restored){restoreRevision=owner.restored;signature.clear();++selectionRevision;}
    Composer::Job job;job.rate=rate;std::vector<uint64_t> nextSignature;std::map<juce::String,Projection> nextProjections;
    bool rendering=false,unavailable=false;float progress=1.f;status.clear();
    juce::String selectionKey;
    for(auto* r:selected){
        selectionKey+=r->key+":";auto* m=r->getAudioModification<Modification>();auto* source=m->getAudioSource<Source>();
        auto span=owner.span(*r);if(!span){unavailable=true;status="Unsupported stretched event. Render the event in the DAW before loading.";continue;}
        auto audio=m->runtime->audio();if(!audio){unavailable=true;status=source->error;continue;}
        if(auto e=std::atomic_load(&m->runtime->engine);e&&e->isBusy()){rendering=true;progress=juce::jmin(progress,e->renderProgress());}
        span->frozen=audio;job.spans.push_back(*span);
        nextSignature.push_back((uint64_t)(uintptr_t)audio.get());
        nextSignature.push_back((uint64_t)juce::String(span->start,12).hashCode64());nextSignature.push_back((uint64_t)juce::String(span->end,12).hashCode64());nextSignature.push_back((uint64_t)juce::String(span->sourceStart,12).hashCode64());
        for(auto e:m->edits){const auto original=e;e.start=span->start+(e.start-span->sourceStart);if(!convertFrequencyRange(e,audio->sampleRate,rate))continue;
            if(e.start+e.duration<=span->start||e.start>=span->end||e.low>=e.high)continue;
            // A single Print spanning several events is shown once when projections coincide.
            auto existing=nextProjections.find(e.id);
            if(existing!=nextProjections.end()){
                if(std::abs(existing->second.edit.start-e.start)<1.e-8&&std::abs(existing->second.edit.duration-e.duration)<1.e-8)continue;
                e.id+="@"+r->key;
            }
            nextProjections[e.id]={r,e,original.id};job.edits.push_back(e);
        }
    }
    key=selectionKey;job.key=key;nextSignature.push_back((uint64_t)rate);nextSignature.push_back((uint64_t)key.hashCode64());
    projections=nextProjections;
    if(selected.empty())status="Select audio events in the DAW, then click Load.";
    if(unavailable){
        engine.setExternalProgress(false,1);signature.clear();
        if(!availabilityCleared){requested=composer.request({});engine.present({});availabilityCleared=true;}
        return;
    }
    availabilityCleared=false;
    if(!rendering&&(force||nextSignature!=signature)){
        signature=std::move(nextSignature);requested=composer.request(std::move(job));
    }
    uint64_t id=0;auto ready=composer.result(id);
    if(id==requested&&id>accepted&&!rendering){accepted=id;engine.present(std::move(ready));}
    const bool composing=composer.busy();engine.setExternalProgress(rendering||composing,rendering?progress:.9f);
    if(auto error=composer.error();error.isNotEmpty())status=error;
}
void View::applyEdits(const std::vector<PrintEdit>& next){
    if(getDocumentController()->isHostEditingDocument())return;
    requested=composer.cancel();engine.cancelPresentation();
    std::map<Modification*,std::vector<PrintEdit>> changes;
    auto change=[&](Modification* m)->std::vector<PrintEdit>&{auto [it,inserted]=changes.emplace(m,m->edits);return it->second;};
    std::set<juce::String> removed;
    for(const auto& [id,p]:projections)if(std::none_of(next.begin(),next.end(),[&](const auto& e){return e.id==id;}))removed.insert(p.canonical);
    for(auto* region:selected){auto* m=region->getAudioModification<Modification>();
        if(std::any_of(m->edits.begin(),m->edits.end(),[&](const auto& e){return removed.count(e.id)!=0;})){
            auto& edits=change(m);edits.erase(std::remove_if(edits.begin(),edits.end(),[&](const auto& e){return removed.count(e.id)!=0;}),edits.end());
        }
    }
    for(const auto& edit:next){
        const auto prior=projections.find(edit.id);if(prior!=projections.end()&&sameEdit(prior->second.edit,edit))continue;
        const juce::String canonical=prior!=projections.end()?prior->second.canonical:edit.id;
        // Replace the whole image transaction, including portions on other selected events.
        for(auto* r:selected){auto* m=r->getAudioModification<Modification>();auto& edits=change(m);edits.erase(std::remove_if(edits.begin(),edits.end(),[&](const auto& e){return e.id==canonical;}),edits.end());}
        std::set<Modification*> seen;
        for(auto* r:selected){auto span=owner.span(*r);if(!span||edit.start>=span->end||edit.start+edit.duration<=span->start)continue;
            auto* m=r->getAudioModification<Modification>();const auto raw=m->getAudioSource<Source>()->raw;if(!raw||!seen.insert(m).second)continue;
            auto e=edit;e.id=canonical;e.start=span->sourceTime(edit.start);if(!convertFrequencyRange(e,rate,raw->sampleRate))continue;
            if(e.valid())change(m).push_back(std::move(e));
        }
    }
    for(auto& [m,edits]:changes)if(!sameEdits(immutableEdits(m->edits),immutableEdits(edits))){m->edits=std::move(edits);owner.sync(*m,true);}
    signature.clear();tick();
}
namespace {
juce::ValueTree encode(const PrintEdit& e){
    juce::ValueTree v("Print");
    v.setProperty("id",e.id,nullptr);v.setProperty("image",e.imagePath,nullptr);
    v.setProperty("start",e.start,nullptr);v.setProperty("duration",e.duration,nullptr);
    v.setProperty("low",e.low,nullptr);v.setProperty("high",e.high,nullptr);v.setProperty("ratio",e.ratio,nullptr);v.setProperty("noise",e.compensation,nullptr);
    v.setProperty("fft",e.fftSize,nullptr);v.setProperty("rows",e.rows,nullptr);v.setProperty("cols",e.cols,nullptr);v.setProperty("invert",e.invert,nullptr);
    v.setProperty("speed",e.speed,nullptr);v.setProperty("amount",e.amount,nullptr);v.setProperty("top",e.displayTop,nullptr);v.setProperty("aspect",e.imageAspect,nullptr);v.setProperty("zoom",e.viewZoom,nullptr);v.setProperty("mode",e.imageMode,nullptr);v.setProperty("scale",e.scaleMode,nullptr);
    juce::MemoryOutputStream mask;for(float f:*e.mask)mask.writeFloat(f);v.setProperty("mask",mask.getMemoryBlock(),nullptr);return v;
}
std::optional<PrintEdit> decode(const juce::ValueTree& v){
    if(!v.hasType("Print"))return {};PrintEdit e;e.id=v["id"].toString();e.imagePath=v["image"].toString();e.start=v["start"];e.duration=v["duration"];e.low=v["low"];e.high=v["high"];e.ratio=v["ratio"];e.compensation=v["noise"];e.fftSize=v["fft"];e.rows=v["rows"];e.cols=v["cols"];e.invert=v["invert"];e.speed=v["speed"];e.amount=v["amount"];e.displayTop=v["top"];e.imageAspect=v["aspect"];e.viewZoom=v["zoom"];e.imageMode=v["mode"];e.scaleMode=v["scale"];
    const auto maskValue=v["mask"];auto* bytes=maskValue.getBinaryData();if(!bytes||e.id.isEmpty()||e.rows<1||e.rows>2049||e.cols<1||e.cols>4096||bytes->getSize()!=(size_t)e.rows*e.cols*4)return {};
    auto mask=std::make_shared<std::vector<float>>();mask->reserve((size_t)e.rows*e.cols);juce::MemoryInputStream stream(*bytes,false);
    for(int i=0;i<e.rows*e.cols;++i){float f=stream.readFloat();if(!std::isfinite(f)||f<0||f>1)return {};mask->push_back(f);}e.mask=mask;
    if(!e.valid()||!std::isfinite(e.speed)||!std::isfinite(e.amount)||!std::isfinite(e.displayTop)||!std::isfinite(e.imageAspect)||!std::isfinite(e.viewZoom))return {};return e;
}
}
bool Controller::doStoreObjectsToStream(juce::ARAOutputStream& out,const juce::ARAStoreObjectsFilter* filter){
    try{
        juce::ValueTree archive("SpectrumTagARA");archive.setProperty("schema",1,nullptr);
        for(auto* m:filter->getAudioModificationsToStore<Modification>()){
            juce::ValueTree v("Modification");v.setProperty("id",juce::String(m->getPersistentID()),nullptr);v.setProperty("viewFrequencyScale",m->viewFrequencyScale,nullptr);for(const auto& e:m->edits)v.addChild(encode(e),-1,nullptr);archive.addChild(v,-1,nullptr);
        }
        juce::MemoryOutputStream bytes;archive.writeToStream(bytes);if(bytes.getDataSize()>256*1024*1024)return false;
        return out.writeInt(0x53504131)&&out.writeInt((int)bytes.getDataSize())&&out.write(bytes.getData(),bytes.getDataSize());
    }catch(...){return false;}
}
bool Controller::doRestoreObjectsFromStream(juce::ARAInputStream& in,const juce::ARARestoreObjectsFilter* filter){
    try{
        if(in.readInt()!=0x53504131)return false;const int length=in.readInt();if(length<=0||length>256*1024*1024)return false;
        juce::MemoryBlock bytes((size_t)length);if(in.read(bytes.getData(),length)!=length)return false;
        auto root=juce::ValueTree::readFromData(bytes.getData(),bytes.getSize());if(!root.hasType("SpectrumTagARA")||(int)root["schema"]!=1)return false;
        std::map<Modification*,std::vector<PrintEdit>> changes;std::map<Modification*,int> choices;
        for(auto v:root){if(!v.hasType("Modification"))return false;const auto id=v["id"].toString();
            auto* m=filter->getAudioModificationToRestoreStateWithID<Modification>(id.toRawUTF8());std::vector<PrintEdit> edits;
            for(auto child:v){auto e=decode(child);if(!e)return false;edits.push_back(std::move(*e));}const int scale=(int)v.getProperty("viewFrequencyScale",-1);if(scale < -1||scale>6)return false;
            if(m){changes[m]=std::move(edits);choices[m]=scale;}
        }
        for(auto& [m,edits]:changes){m->edits=std::move(edits);m->viewFrequencyScale=choices[m];sync(*m,false);
            m->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(),false);
            for(auto* r:m->getPlaybackRegions())r->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(),false);
        }
        ++restored;return !in.failed();
    }catch(...){return false;}
}
}
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory(){return juce::ARADocumentControllerSpecialisation::createARAFactory<spectrumtag::ara::Controller>();}
