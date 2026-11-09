#pragma once
#include "../shared/Recording.h"
#include "../shared/FrequencyScale.h"
#include <unordered_map>

namespace spectrumtag::standalone {
struct VideoOptions {
    enum Aspect { ratio4x3, ratio3x4, ratio16x9, ratio9x16 };
    int format=0, resolution=1, fps=30, palette=0, scale=frequency::mel;
    int aspect=ratio16x9;
    float top=20000, speed=1;
    bool chinese=true;
    // Resolution is the short edge, so rotating the canvas preserves quality.
    // Every offered dimension is even and can be encoded directly as yuv420p.
    static constexpr int shortEdges[]{720,1080,1440,2160};
    int shortEdge()const{return shortEdges[juce::jlimit(0,3,resolution)];}
    int longEdge()const{return aspect==ratio4x3||aspect==ratio3x4?shortEdge()*4/3:shortEdge()*16/9;}
    bool portrait()const{return aspect==ratio3x4||aspect==ratio9x16;}
    int width()const{return portrait()?shortEdge():longEdge();}
    int height()const{return portrait()?longEdge():shortEdge();}
    juce::String extension()const{return juce::StringArray{".mp4",".mov",".mkv",".webm"}[juce::jlimit(0,3,format)];}
    void normalise(){format=juce::jlimit(0,3,format);resolution=juce::jlimit(0,3,resolution);fps=fps==60?60:30;if(aspect<ratio4x3||aspect>ratio9x16)aspect=ratio16x9;}
    static VideoOptions remembered();
    void remember()const;
};

// A camera maps physical time/frequency to the video. It never changes audio,
// Print edits or the editor's Speed. Blending forward frequency curves keeps
// the intermediate mapping monotonic even between different scale families.
class SpectrumMovie {
public:
    struct Camera {
        double pixelsPerSecond=60,yStart=0,ySpan=1;
        float top=20000;int scale=frequency::mel,tag=-1;
        double y(double hz)const{return (frequency::toY((float)hz,top,scale)-yStart)/ySpan;}
    };
    struct View {
        Camera from,to;double mix=1;
        double y(double hz)const{return (1-mix)*from.y(hz)+mix*to.y(hz);}
        double pixelsPerSecond()const{return std::exp((1-mix)*std::log(from.pixelsPerSecond)+mix*std::log(to.pixelsPerSecond));}
    };
    SpectrumMovie(std::shared_ptr<const RecordingSnapshot>,VideoOptions);
    View viewAt(double time)const;
    juce::Image frame(double time)const;
    juce::Rectangle<int> plot()const{return bounds;}
    Camera tagCamera(const PrintEdit&,int index)const;
private:
    struct Cue{double begin=0,end=0;Camera from,to;};
    std::shared_ptr<const RecordingSnapshot> audio;VideoOptions options;
    juce::Rectangle<int> bounds;Camera initial;std::vector<Cue> cues;
    std::array<juce::PixelRGB,256> colours;
    // A movie owns an immutable audio snapshot. Reuse colourised FFT columns
    // while the frequency camera is unchanged, rather than repainting history.
    struct CachedColumn {std::vector<juce::PixelRGB> pixels;uint64_t used=0;};
    mutable std::unordered_map<const SpectrumFrame*,CachedColumn> columnCache;
    mutable std::vector<float> rowHz;
    mutable View cachedView;
    mutable uint64_t frameSerial=0;
};

class SpectrumVideoExport final : private juce::Thread {
public:
    enum Stage {idle,preparing,frames,finishing,complete,cancelled,failed};
    SpectrumVideoExport();
    ~SpectrumVideoExport()override;
    bool start(std::shared_ptr<const RecordingSnapshot>,VideoOptions,const juce::File&);
    void cancel(){cancelRequested=true;}
    bool busy()const{return active.load();}
    Stage stage()const{return currentStage.load();}
    float progress()const{return fraction.load();}
    juce::String error()const;
private:
    void run()override;
    std::atomic<bool> active{false},cancelRequested{false};
    std::atomic<Stage> currentStage{idle};std::atomic<float> fraction{0};
    std::shared_ptr<const RecordingSnapshot> audio;VideoOptions options;juce::File target;
    mutable std::mutex mutex;juce::String failure;
};
}
