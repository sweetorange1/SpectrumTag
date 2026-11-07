#pragma once
#include "Recording.h"
#include "FrequencyScale.h"
#include <mutex>
namespace spectrumtag {
// Both live and recorded spectra are rasterised off the message/audio threads.
// Latest requests are coalesced, but a useful in-flight frame is allowed to finish.
class ViewRenderer : private juce::Thread {
public:
    using LiveFrames=std::vector<std::shared_ptr<const SpectrumFrame>>;
    struct Request {std::shared_ptr<const RecordingSnapshot> source;int width=0,height=0,palette=0,scale=0;float top=22000;double start=0,end=1;std::optional<PrintEdit> preview;LiveFrames live;};
    struct Result {uint64_t generation=0,epoch=0;juce::Image image;double start=0,end=0;std::shared_ptr<const RecordingSnapshot> source;int palette=0,scale=0;float top=22000;double milliseconds=0;};
    ViewRenderer():juce::Thread("SpectrumTag display"){startThread(juce::Thread::Priority::low);}
    ~ViewRenderer() override {signalThreadShouldExit();notify();stopThread(-1);}
    uint64_t request(Request value){std::lock_guard<std::mutex> lock(mutex);job=std::move(value);const auto id=++generation;notify();return id;}
    uint64_t invalidate(){std::lock_guard<std::mutex> lock(mutex);job={};++generation;const auto e=++epoch;std::atomic_store(&published,std::shared_ptr<const Result>{});notify();return e;}
    std::shared_ptr<const Result> result() const {return std::atomic_load(&published);}
private:
    std::mutex mutex;Request job;uint64_t generation=0,done=0;std::atomic<uint64_t> epoch{0};
    std::shared_ptr<const Result> published;
    void run() override {
        while(!threadShouldExit()){
            Request r;uint64_t id,e;{std::lock_guard<std::mutex> lock(mutex);id=generation;e=epoch;r=job;}
            if(id==done){wait(40);continue;}done=id;
            if(r.width<1||r.height<1||r.end<=r.start)continue;
            const auto started=juce::Time::getMillisecondCounterHiRes();
            juce::Image image(juce::Image::RGB,r.width,r.height,true);
            juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
            std::array<juce::PixelRGB,256> lut;for(int i=0;i<256;++i){auto c=spectrumColour(r.palette,i/255.f);lut[i].setARGB(255,c.getRed(),c.getGreen(),c.getBlue());}
            std::vector<const SpectrumFrame*> columns((size_t)r.width,nullptr);
            auto cursor=r.live.begin();
            for(int x=0;x<r.width;++x){
                const double t=r.start+(x+.5)*(r.end-r.start)/r.width;
                if(r.source)columns[x]=r.source->frameAt(t);
                else if(!r.live.empty()){
                    while(cursor!=r.live.end()&&(*cursor)->time<t)++cursor;
                    auto nearest=cursor==r.live.end()?std::prev(cursor):cursor;
                    if(nearest!=r.live.begin()&&t-(*std::prev(nearest))->time<(*nearest)->time-t)--nearest;
                    if(std::abs(t-(*nearest)->time)<=juce::jmax((*nearest)->interval*1.6,(r.end-r.start)/r.width*.5))columns[x]=nearest->get();
                }
            }
            // Row-major contiguous stores and one frequency calculation per row.
            // Bin interpolation remains identical to the previous display.
            for(int y=0;y<r.height&&!threadShouldExit();++y){
                if((y&15)==0&&e!=epoch.load())break;
                const float t=1.f-(float)y/juce::jmax(1,r.height-1);
                const float hz=frequency::interpolate(t,frequency::minimum(r.scale),r.top,r.scale);
                auto* row=reinterpret_cast<juce::PixelRGB*>(pixels.getLinePointer(y));
                int cachedBins=-1,low=0,high=0;double cachedRate=0;float fraction=0;
                for(int x=0;x<r.width;++x){int level=0;const auto* frame=columns[x];
                    if(frame&&frame->bins>1){
                        if(frame->bins!=cachedBins||frame->sampleRate!=cachedRate){
                            cachedBins=frame->bins;cachedRate=frame->sampleRate;
                            const float bin=juce::jlimit(0.f,(float)cachedBins-1,hz/(float)(cachedRate*.5)*(cachedBins-1));
                            low=(int)bin;high=juce::jmin(cachedBins-1,low+1);fraction=bin-low;
                        }
                        level=juce::roundToInt(frame->level[low]+fraction*(frame->level[high]-frame->level[low]));
                    }
                    row[x]=lut[(size_t)juce::jlimit(0,255,level)];
                }
            }
            if(e==epoch.load())std::atomic_store(&published,std::make_shared<const Result>(Result{id,e,std::move(image),r.start,r.end,r.source,r.palette,r.scale,r.top,juce::Time::getMillisecondCounterHiRes()-started}));
        }
    }
};
}
