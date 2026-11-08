#pragma once
#include "Recording.h"
#if JUCE_MAC
 #include <sys/sysctl.h>
#endif

namespace spectrumtag::diagnostics {
// Explicit allow-list. Never serialize processor state, file names/paths,
// audio samples, mask pixels, screenshots, environment variables or host state.
inline juce::var object(){return juce::var(new juce::DynamicObject);}
inline void put(juce::var& o,const char* key,const juce::var& value){o.getDynamicObject()->setProperty(key,value);}
inline juce::var rectangle(juce::Rectangle<int> r){return juce::Array<juce::var>{r.getX(),r.getY(),r.getWidth(),r.getHeight()};}
inline juce::var environment(){
    auto o=object();put(o,"productVersion",JucePlugin_VersionString);put(o,"juceVersion",juce::SystemStats::getJUCEVersion());
    put(o,"os",juce::SystemStats::getOperatingSystemName());put(o,"host",juce::PluginHostType().getHostDescription());
    put(o,"hostExecutable",juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName());
   #if JUCE_ARM
    put(o,"processArchitecture","arm64");
   #elif JUCE_64BIT
    put(o,"processArchitecture","x86_64");
   #else
    put(o,"processArchitecture","32-bit");
   #endif
   #if JUCE_MAC
    int translated=0;size_t length=sizeof(translated);
    put(o,"rosetta",sysctlbyname("sysctl.proc_translated",&translated,&length,nullptr,0)==0&&translated!=0);
   #endif
    return o;
}
inline juce::var engine(const RecordingEngine& engine){
    auto o=object();put(o,"enabled",engine.isEnabled());put(o,"armed",engine.isArmed());put(o,"busy",engine.isBusy());
    put(o,"progress",engine.renderProgress());put(o,"stage",(int)engine.renderStage());put(o,"errorCode",engine.errorCode());
    put(o,"printPasses",(juce::int64)engine.printPasses());put(o,"analysisPasses",(juce::int64)engine.analysisPasses());
    put(o,"analysisMs",engine.analysisMilliseconds());put(o,"cacheHits",(juce::int64)engine.cacheHits());
    auto s=engine.snapshot();auto edits=engine.edits();put(o,"desiredPrintCount",edits?(int)edits->size():0);
    put(o,"snapshotRevision",s?s->revision:-1);
    if(s){put(o,"sampleRate",s->sampleRate);put(o,"channels",s->channels);put(o,"fftSize",s->fftSize);
        put(o,"startSeconds",s->start());put(o,"endSeconds",s->end());put(o,"renderedPrintCount",(int)s->edits.size());
        put(o,"spectrumTiles",(int)s->spectrum.size());put(o,"originalSpectrumTiles",(int)s->originalSpectrum.size());}
    return o;
}
// Bounded, aggregate-only probes. These numbers can distinguish correct audio /
// stale FFT data / stale raster without exporting recoverable user media.
inline juce::var prints(const RecordingEngine& engine){
    juce::Array<juce::var> output;const auto s=engine.snapshot();const auto desired=engine.edits();
    if(!desired)return output;
    for(size_t index=0;index<std::min<size_t>(desired->size(),64);++index){const auto& e=(*desired)[index];auto o=object();
        put(o,"index",(int)index);put(o,"startSeconds",e.start);put(o,"durationSeconds",e.duration);
        put(o,"lowNormalised",e.low);put(o,"highNormalised",e.high);put(o,"scale",e.scaleMode);put(o,"rangeHz",e.displayTop);
        put(o,"speed",e.speed);put(o,"viewZoom",e.viewZoom);put(o,"aspect",e.imageAspect);put(o,"fftSize",e.fftSize);
        put(o,"bwMode",e.imageMode);put(o,"bwAmount",e.amount);put(o,"invert",e.invert);put(o,"ratio",e.ratio);put(o,"noiseFill",e.compensation);
        put(o,"maskRows",e.rows);put(o,"maskColumns",e.cols);put(o,"maskValid",e.valid());
        bool applied=false;if(s)for(const auto& wet:s->edits)if(sameAudioEdit(e,wet)){applied=true;break;}put(o,"matchesPublishedPrint",applied);
        double dryPower=0,wetPower=0,deltaPower=0,level=0,originalLevel=0,levelDelta=0;int samples=0,frames=0,bins=0;
        if(s&&e.valid())for(int t=0;t<128;++t){const double seconds=e.start+(t+.5)*e.duration/128.;
            const double position=std::floor(seconds*s->sampleRate);if(!std::isfinite(position)||std::abs(position)>1.e15)continue;
            const auto at=(int64_t)position;if(s->contains(at))for(int c=0;c<std::min(2,s->channels);++c){const double dry=s->sample(c,at,false),wet=s->sample(c,at);dryPower+=dry*dry;wetPower+=wet*wet;deltaPower+=(wet-dry)*(wet-dry);++samples;}
            auto* frame=s->frameAt(seconds);auto* original=s->frameAt(seconds,false);
            if(frame&&original&&frame->bins>0&&original->bins>0){++frames;for(int k=0;k<32;++k){const float hz=(e.low+(e.high-e.low)*(k+.5f)/32.f)*(float)s->sampleRate*.5f;
                const int b=juce::jlimit(0,juce::jmin(frame->bins,original->bins)-1,juce::roundToInt(hz/(s->sampleRate*.5)*(frame->bins-1)));
                if(b<0)continue;level+=frame->level[b];originalLevel+=original->level[b];levelDelta+=std::abs((int)frame->level[b]-(int)original->level[b]);++bins;}}
        }
        put(o,"audioProbeSamples",samples);put(o,"originalRms",std::sqrt(dryPower/juce::jmax(1,samples)));put(o,"renderedRms",std::sqrt(wetPower/juce::jmax(1,samples)));put(o,"audioChangeRms",std::sqrt(deltaPower/juce::jmax(1,samples)));
        put(o,"spectrumProbeFrames",frames);put(o,"spectrumMean",level/juce::jmax(1,bins));put(o,"originalSpectrumMean",originalLevel/juce::jmax(1,bins));put(o,"spectrumMeanAbsoluteChange",levelDelta/juce::jmax(1,bins));
        output.add(std::move(o));
    }
    return output;
}
class History {
public:
    void add(juce::var value){if(rows.size()==120)rows.pop_front();rows.push_back(std::move(value));}
    juce::var json()const{juce::Array<juce::var> a;for(const auto& r:rows)a.add(r);return a;}
private:
    std::deque<juce::var> rows;
};
inline bool write(const juce::File& destination,const juce::var& report){
    // A small, human-readable JSON file; no source media or auxiliary archives.
    return destination.replaceWithText(juce::JSON::toString(report));
}
}
