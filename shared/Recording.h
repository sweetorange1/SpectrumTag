#pragma once
#include "Presentation.h"
#include <bitset>
#include <mutex>
#include <functional>

namespace spectrumtag {
// Audio belongs to this plugin. Immutable pages let playback and the UI read it
// while a worker records or re-renders, without copying the entire recording.
constexpr int recordingPageSize=2048;
struct AudioPage {
    std::array<std::array<float,recordingPageSize>,2> samples {};
    std::bitset<recordingPageSize> valid;
};
struct PrintEdit {
    juce::String id=juce::Uuid().toString(),imagePath;
    double start=0,duration=1;
    float low=0,high=1,ratio=0,compensation=0;
    int fftSize=4096,rows=0,cols=0;
    bool invert=false;
    float speed=1, amount=.5f, displayTop=22000, imageAspect=1;
    double viewZoom=1;
    int imageMode=0, scaleMode=0;
    std::shared_ptr<const std::vector<float>> mask;
    bool valid() const;
};
inline bool sameAudioEdit(const PrintEdit& a,const PrintEdit& b) {
    return a.id==b.id&&a.start==b.start&&a.duration==b.duration&&a.low==b.low&&a.high==b.high
        &&a.ratio==b.ratio&&a.compensation==b.compensation&&a.fftSize==b.fftSize&&a.invert==b.invert
        &&a.rows==b.rows&&a.cols==b.cols&&(a.mask==b.mask||(a.mask&&b.mask&&*a.mask==*b.mask));
}
inline bool sameEdit(const PrintEdit& a,const PrintEdit& b) {
    return sameAudioEdit(a,b)&&a.imagePath==b.imagePath&&a.speed==b.speed&&a.amount==b.amount
        &&a.displayTop==b.displayTop&&a.imageAspect==b.imageAspect&&a.viewZoom==b.viewZoom
        &&a.imageMode==b.imageMode&&a.scaleMode==b.scaleMode;
}
inline bool sameEdits(const std::shared_ptr<const std::vector<PrintEdit>>& a,const std::shared_ptr<const std::vector<PrintEdit>>& b) {
    if(a==b)return true;if(!a||!b)return (!a||a->empty())&&(!b||b->empty());
    return a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin(),sameEdit);
}
struct RecordingSnapshot {
    juce::String sourceId=juce::Uuid().toString();
    double sampleRate=48000;
    int channels=2,fftSize=4096,revision=0;
    int64_t origin=0,first=0,last=0;
    std::vector<std::shared_ptr<const AudioPage>> original,rendered;
    std::vector<PrintEdit> edits;
    struct Tile { std::vector<SpectrumFrame> frames; };
    std::vector<std::shared_ptr<const Tile>> spectrum,originalSpectrum;
    std::vector<std::pair<float,float>> originalPeaks;
    std::vector<std::pair<float,float>> peaks;
    static constexpr int tileSize=8192;
    double start() const {return first/sampleRate;}
    double end() const {return last/sampleRate;}
    bool empty() const {return last<=first;}
    float sample(int channel,int64_t absolute,bool processed=true) const;
    bool contains(int64_t absolute) const;
    const SpectrumFrame* frameAt(double seconds,bool processed=true) const;
};

struct RecordingDocument {
    std::shared_ptr<const RecordingSnapshot> source;
    std::shared_ptr<const std::vector<PrintEdit>> edits;
    bool enabled=false;
};

// Host position is read only on the audio thread. A local audition offset never
// seeks the DAW. Stop, host seek and cycle wrap cancel the old offset.
class AuditionTransport {
public:
    void seek(double seconds) { requested.store(seconds); request.fetch_add(1); }
    void reset();
    void requestHostFollow(){followConsumed=request.load();followRequested=true;shown=hostShown.load();local=false;}
    double advance(bool playing,bool hasTime,double hostSeconds,int blockSamples,double sampleRate);
    void publishExternalPosition(double seconds){shown=seconds;hostShown=seconds;local=false;}
    double position() const {return shown.load();}
    double hostPosition() const {return hostShown.load();}
    bool isLocal() const {return local.load();}
private:
    std::atomic<double> requested {0},shown {0},hostShown {0};
    std::atomic<int> request {0},followConsumed {0};
    std::atomic<bool> local {false},followRequested {false};
    int consumed=0;
    double previousHost=0,previousDuration=0,offset=0;
    bool previousPlaying=false,previousHadTime=false,initialised=false;
};

class RecordingEngine : private juce::Thread {
public:
    explicit RecordingEngine(bool externalView=false);
    ~RecordingEngine() override;
    void setArmed(bool value) {if(externalView)return;armed.store(value); if(value) {enabled.store(true);error.store(0);} notify();}
    bool isArmed() const {return armed.load();}
    bool isEnabled() const {return enabled.load();}
    bool isBusy() const {return externalBusy.load() || busy.load() || pending.load()>0 || fifo.getNumReady()>0 || changed.load();}
    enum class RenderStage {ready,queued,preparing,printing,spectrum};
    float renderProgress() const {return externalBusy.load()?externalProgress.load():progress.load();}
    RenderStage renderStage() const {return externalBusy.load()?RenderStage::printing:stage.load();}
    int errorCode() const {return error.load();} // 1 queue, 2 capacity, 3 rate, 4 cache
    void capture(const juce::AudioBuffer<float>&,double hostSeconds,double sampleRate,bool offline=false);
    std::shared_ptr<const RecordingSnapshot> snapshot() const {return std::atomic_load(&published);}
    std::shared_ptr<const std::vector<PrintEdit>> edits() const {return std::atomic_load(&desiredEdits);}
    RecordingDocument document() const {return {snapshot(),edits(),isEnabled()};}
    void setEdits(std::vector<PrintEdit>);
    void restoreDocument(RecordingDocument);
    void clear() {restoreDocument({});}
    struct ClearResult {RecordingDocument before;std::atomic<bool> ready{false};};
    std::shared_ptr<ClearResult> clearKeepingUndo();
    void add(PrintEdit edit,juce::String replaceId={});
    void remove(juce::String id);
    void setCompensation(juce::String id,float amount);
    void importAudio(const juce::File&,double hostStart,double targetRate=0);
    void setDisplayFft(int size);
    bool flush(int timeoutMs=10000);
    juce::String save();
    void restore(const juce::String& path,bool wasEnabled);
    // ARA editor facade: already-rendered immutable audio, never a second Print pass.
    void cancelPresentation();
    void present(std::shared_ptr<const RecordingSnapshot>);
    void setExternalProgress(bool active,float value){externalBusy=active;externalProgress=value;}
    std::function<void(const std::vector<PrintEdit>&)> onExternalEdits;
private:
    const bool externalView;
    std::atomic<bool> externalBusy{false};
    std::atomic<float> externalProgress{1};
public:
private:
    static constexpr int packetSize=256,queueSize=4096;
    static constexpr int64_t maxSamples=64000000;
    struct Packet {double time=0,rate=48000;int channels=2,count=0,epoch=0;std::array<std::array<float,packetSize>,2> data;};
    std::vector<Packet> packets;
    juce::AbstractFifo fifo {queueSize};
    std::atomic<bool> armed {false},enabled {false},busy {false},clearing {false};
    std::atomic<int> error {0},pending {0},requestedFft {4096},captureEpoch {0};
    std::atomic<uint64_t> flushRequested {0},flushDone {0};
    std::mutex commandsMutex,saveMutex;
    std::vector<std::function<void()>> commands;
    std::shared_ptr<const std::vector<PrintEdit>> desiredEdits=std::make_shared<const std::vector<PrintEdit>>();
    bool editsQueued=false;
    std::atomic<uint64_t> editRevision {0};
    std::atomic<float> progress {1.f};
    std::atomic<RenderStage> stage{RenderStage::ready};
    void reportStage(RenderStage value){if(progressGeneration==editRevision.load())stage=value;}
    uint64_t progressGeneration=0,renderUnits=0,renderDone=0;
    void reportProgress(float value){if(progressGeneration==editRevision.load())progress.store(value,std::memory_order_relaxed);}
    void applyDesiredEdits();
    std::shared_ptr<const RecordingSnapshot> published;
    std::vector<std::shared_ptr<const RecordingSnapshot>> retired;
    RecordingSnapshot work;
    std::vector<std::shared_ptr<AudioPage>> source;
    int64_t dirtyFirst=INT64_MAX,dirtyLast=INT64_MIN;
    std::atomic<bool> changed{false};
    int revision=0,savedRevision=-1;
    juce::String savedPath;
    void enqueue(std::function<void()>);
    void run() override;
    void ingest(const Packet&);
    void dirty(int64_t a,int64_t b);
    void publish();
    void apply(const PrintEdit&,std::vector<std::shared_ptr<AudioPage>>&,int64_t,int64_t);
    void analyse(RecordingSnapshot&,int64_t,int64_t);
    bool load(const juce::String& path);
};
}
