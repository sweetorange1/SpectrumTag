#pragma once
#include <JuceHeader.h>
#include "../shared/Recording.h"
#include <map>
#include <set>

namespace spectrumtag::ara {
struct Runtime {
    std::shared_ptr<const RecordingSnapshot> raw;
    std::shared_ptr<RecordingEngine> engine;
    std::atomic<bool> active{true};
    std::shared_ptr<const RecordingSnapshot> audio() const {
        if(!active.load())return {};
        const auto original=std::atomic_load(&raw);if(!original)return {};
        if(auto e=std::atomic_load(&engine))if(auto s=e->snapshot();s&&s->sourceId==original->sourceId)return s;
        return original;
    }
};
struct Source final : juce::ARAAudioSource {
    using juce::ARAAudioSource::ARAAudioSource;
    std::shared_ptr<const RecordingSnapshot> raw;
    juce::String error;
};
struct Modification final : juce::ARAAudioModification {
    Modification(juce::ARAAudioSource*,ARA::ARAAudioModificationHostRef,const juce::ARAAudioModification*);
    std::shared_ptr<Runtime> runtime=std::make_shared<Runtime>();
    std::vector<PrintEdit> edits;
    // New ARA modifications start in Mel even if the host reuses a wrapper.
    // -1 denotes an older archive: its wrapper state remains authoritative.
    int viewFrequencyScale=4;
};
struct Region final : juce::ARAPlaybackRegion {
    using juce::ARAPlaybackRegion::ARAPlaybackRegion;
    const juce::String key=juce::Uuid().toString();
};
struct Span {
    double start=0,end=0,sourceStart=0;
    std::shared_ptr<Runtime> runtime;
    std::shared_ptr<const RecordingSnapshot> frozen;
    double sourceTime(double projectTime)const{return sourceStart+projectTime-start;}
};
bool convertFrequencyRange(PrintEdit&,double sourceRate,double targetRate);
float readSample(const RecordingSnapshot&,int,double,double,bool=true) noexcept;
void renderSpans(const std::vector<Span>&,juce::AudioBuffer<float>&,double,double) noexcept;
class Controller;
class View;
class Playback final : public juce::ARAPlaybackRenderer {
public:
    explicit Playback(Controller&);
    ~Playback() override;
    void addPlaybackRegion(ARA::ARAPlaybackRegionRef)noexcept override;
    void removePlaybackRegion(ARA::ARAPlaybackRegionRef)noexcept override;
    void prepareToPlay(double,int,int,juce::AudioProcessor::ProcessingPrecision,AlwaysNonRealtime) override;
    bool processBlock(juce::AudioBuffer<float>&,juce::AudioProcessor::Realtime,const juce::AudioPlayHead::PositionInfo&) noexcept override;
    void rebuild();
    void retirePlans();
private:
    Controller& owner;double rate=48000;
    std::vector<std::shared_ptr<const std::vector<Span>>> retiredPlans;
    std::shared_ptr<const std::vector<Span>> plan=std::make_shared<const std::vector<Span>>();
};
class Composer final : private juce::Thread {
public:
    struct Job {uint64_t id=0;juce::String key;double rate=48000;std::vector<Span> spans;std::vector<PrintEdit> edits;};
    Composer();~Composer() override;
    uint64_t request(Job);
    uint64_t cancel();
    std::shared_ptr<const RecordingSnapshot> result(uint64_t&) const;
    bool busy()const{return working.load();}
    juce::String error()const;
private:
    void run()override;
    mutable std::mutex mutex;
    std::optional<Job> pending;
    std::shared_ptr<const RecordingSnapshot> completed;
    juce::String failure;
    std::atomic<uint64_t> generation{0};
    uint64_t completedId=0;
    std::atomic<bool> working{false};
};
struct ViewState {
    struct Entry {juce::String modification;std::vector<PrintEdit> edits;};
    std::vector<Entry> modifications;
    bool loaded=false,manualLoadOnly=false;
};
class View final : public juce::ARAEditorView {
public:
    explicit View(Controller&);~View()override;
    void doNotifySelection(const ARA::PlugIn::ViewSelection*)noexcept override;
    void load(const std::vector<Region*>& fallback={});
    void tick(bool force=false);
    void clear();
    ViewState captureState()const;
    void restoreState(const ViewState&);
    void remove(Region*);
    void remove(juce::ARARegionSequence*);
    void setRate(double value){if(value>=8000&&value<=384000&&rate!=value){rate=value;signature.clear();}}
    void seek(double);
    juce::String statusText(bool chinese) const;
    void applyEdits(const std::vector<PrintEdit>&);
    std::optional<int> frequencyChoice()const;
    void rememberFrequencyChoice(int);
    RecordingEngine engine{true};
    juce::String key,status;
    uint64_t selectionRevision=0,restoreRevision=0;
    bool loaded=false,explicitSelection=false,manualLoadOnly=false;
    std::function<void()> beforeSelection;
    std::vector<Region*> selected;
private:
    Controller& owner;Composer composer;
    std::vector<juce::ARARegionSequence*> tracks;
    std::vector<Region*> ungrouped;
    void setTrackScope(const std::vector<Region*>&);
    std::vector<Region*> regionsInScope()const;
    struct Projection {Region* region=nullptr;PrintEdit edit;juce::String canonical;};
    std::map<juce::String,Projection> projections;
    std::vector<uint64_t> signature;
    uint64_t requested=0,accepted=0;
    double rate=48000;
    bool availabilityCleared=false;
};
class Controller final : public juce::ARADocumentControllerSpecialisation,private juce::Timer {
public:
    Controller(const ARA::PlugIn::PlugInEntry*,const ARA::ARADocumentControllerHostInstance*);
    ~Controller()override;
    bool cache(Source&);
    void sync(Modification&,bool hostNotification,bool restoring=false);
    void warmup();
    void poll();
    std::optional<Span> span(Region&)const;
    std::vector<Playback*> players;
    std::vector<View*> views;
    uint64_t restored=0;
protected:
    juce::ARAAudioSource* doCreateAudioSource(juce::ARADocument*,ARA::ARAAudioSourceHostRef)override;
    juce::ARAAudioModification* doCreateAudioModification(juce::ARAAudioSource*,ARA::ARAAudioModificationHostRef,const juce::ARAAudioModification*)override;
    juce::ARAPlaybackRegion* doCreatePlaybackRegion(juce::ARAAudioModification*,ARA::ARAPlaybackRegionHostRef)override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer()override;
    juce::ARAEditorView* doCreateEditorView()override;
    void didEndEditing(juce::ARADocument*)override;
    void didEnableAudioSourceSamplesAccess(juce::ARAAudioSource*,bool)override;
    void didDeactivateAudioSourceForUndoHistory(juce::ARAAudioSource*,bool)override;
    void didDeactivateAudioModificationForUndoHistory(juce::ARAAudioModification*,bool)override;
    void didUpdateAudioSourceProperties(juce::ARAAudioSource*)override;
    void doUpdateAudioSourceContent(juce::ARAAudioSource*,juce::ARAContentUpdateScopes)override;
    void willDestroyPlaybackRegion(juce::ARAPlaybackRegion*)override;
    void willDestroyRegionSequence(juce::ARARegionSequence*)override;
    void willDestroyAudioSource(juce::ARAAudioSource*)override;
    void willDestroyAudioModification(juce::ARAAudioModification*)override;
    bool doStoreObjectsToStream(juce::ARAOutputStream&,const juce::ARAStoreObjectsFilter*)override;
    bool doRestoreObjectsFromStream(juce::ARAInputStream&,const juce::ARARestoreObjectsFilter*)override;
private:
    void timerCallback()override{poll();}
    void invalidate(Source&);
    std::vector<std::shared_ptr<const RecordingSnapshot>> retiredAudio;
    std::vector<std::shared_ptr<Runtime>> retiredRuntimes;
};
}
