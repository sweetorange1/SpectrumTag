#pragma once
#include "ImageMask.h"
#include <mutex>
namespace spectrumtag {
// Coalesced B/W conversion. Slider callbacks never wait for image processing.
class PreviewWorker : private juce::Thread {
public:
    struct Result {uint64_t generation=0;juce::Image mask,source;int mode=0;};
    PreviewWorker():juce::Thread("SpectrumTag image preview"){startThread();}
    ~PreviewWorker() override {signalThreadShouldExit();notify();stopThread(-1);}
    uint64_t request(juce::Image image,int mode,float amount){
        std::lock_guard<std::mutex> lock(mutex);source=std::move(image);settings={mode,amount};const auto result=++requested;notify();return result;
    }
    std::shared_ptr<const Result> result() const {return std::atomic_load(&published);}
private:
    std::mutex mutex;juce::Image source;image::Settings settings;uint64_t requested=0,done=0;
    std::shared_ptr<const Result> published;
    void run() override {
        while(!threadShouldExit()){
            juce::Image input;image::Settings config;uint64_t generation;
            {std::lock_guard<std::mutex> lock(mutex);generation=requested;input=source;config=settings;}
            if(generation!=done){auto output=image::binarize(input,config);done=generation;
                // A useful completed frame must not starve while the pointer
                // keeps moving. The consumer checks source/mode and still waits
                // for the exact final generation before committing audio.
                std::atomic_store(&published,std::make_shared<const Result>(Result{generation,std::move(output),input,config.mode}));
            }else wait(50);
        }
    }
};
}
