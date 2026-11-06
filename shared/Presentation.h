#pragma once
#include <JuceHeader.h>
#include <array>
#include <deque>
namespace spectrumtag {
struct Palette {
    const char* name;
    juce::uint32 swatch;
    std::array<juce::uint32,5> colours;
    bool classic=false;
};
inline const std::array<Palette,8> palettes {{
    {"Classic Orange",0xffec7731,{0xff000000,0xff2a0a55,0xffd13a1a,0xfff8e83a,0xfff8e83a},true},
    {"Deep Teal",0xff30b7ac,{0xff000000,0xff063746,0xff087f83,0xff67e8cf,0xffe6fff5}},
    {"Electric Blue",0xff569bff,{0xff000000,0xff102964,0xff235cdb,0xff64b5ff,0xffe5f4ff}},
    {"Lavender",0xffb598e9,{0xff000000,0xff301f56,0xff7551b8,0xffba9bef,0xfff4eaff}},
    {"Rose",0xffe889b0,{0xff000000,0xff4b183c,0xffb6427d,0xfff098ba,0xffffeff5}},
    {"Mint",0xff78cda3,{0xff000000,0xff123f35,0xff2b936e,0xff8dddab,0xffefffe1}},
    {"Cream Gold",0xffe5cf8e,{0xff000000,0xff41351b,0xffa18338,0xffdfc779,0xfffff1bd}},
    {"Ice White",0xffdae5ee,{0xff000000,0xff252f3b,0xff647487,0xffbdcbd7,0xfff4f8fc}}
}};
inline juce::Colour spectrumColour(int palette,float t) {
    const auto& p=palettes[(size_t)juce::jlimit(0,7,palette)];
    t=juce::jlimit(0.0f,1.0f,t);
    const std::array<float,5> positions=p.classic?std::array<float,5>{0,.33f,.66f,1,1}:std::array<float,5>{0,.25f,.5f,.78f,1};
    int end=1;while(end<(p.classic?3:4)&&t>positions[(size_t)end])++end;
    return juce::Colour(p.colours[(size_t)end-1]).interpolatedWith(juce::Colour(p.colours[(size_t)end]),
        (t-positions[(size_t)end-1])/(positions[(size_t)end]-positions[(size_t)end-1]));
}
struct Preferences {
    juce::String imagePath;
    int palette=0;
    bool chineseTips=true;
    float uiScale=1.0f;
    static juce::File file() {
        auto overridePath=juce::SystemStats::getEnvironmentVariable("SPECTRUMTAG_PREFS_DIR",{});
        if(overridePath.isNotEmpty())return juce::File(overridePath).getChildFile("preferences.xml");
       #if JUCE_WINDOWS
        return juce::File("D:\\Codex\\PluginData\\SpectrumTag\\preferences.xml");
       #else
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SpectrumTag/preferences.xml");
       #endif
    }
    static Preferences read() {
        Preferences p;
        if(auto xml=juce::XmlDocument::parse(file())) {
            p.uiScale=juce::jlimit(.75f,2.5f,(float)xml->getDoubleAttribute("uiScale",1.0));
            p.imagePath=xml->getStringAttribute("lastImage");
            p.chineseTips=xml->getBoolAttribute("chineseTips",true);
            p.palette=juce::jlimit(0,7,xml->getIntAttribute("palette",0));
        }
        return p;
    }
    static bool remember(const juce::String* image, const int* colour, const bool* language=nullptr, const float* scale=nullptr) {
        juce::InterProcessLock lock("SpectrumTagPreferences");
        if(!lock.enter(200))return false;
        const auto p=read();juce::XmlElement xml("SpectrumTagPreferences");
        xml.setAttribute("uiScale",scale?juce::jlimit(.75f,2.5f,*scale):p.uiScale);
        xml.setAttribute("lastImage",image?*image:p.imagePath);
        xml.setAttribute("chineseTips",language?*language:p.chineseTips);
        xml.setAttribute("palette",colour?juce::jlimit(0,7,*colour):p.palette);
        const auto f=file();const bool ok=f.getParentDirectory().createDirectory().wasOk()&&f.replaceWithText(xml.toString());
        lock.exit();return ok;
    }
};
struct SpectrumFrame {
    double time=0,sampleRate=48000,interval=.01;
    int bins=0,epoch=0;
    uint64_t clearSerial=0;
    std::array<juce::uint8,2049> level {};
};
class SpectrumQueue {
public:
    SpectrumQueue():slots(1024),fifo(1024){}
    bool push(const SpectrumFrame& frame) {
        int start1,size1,start2,size2;fifo.prepareToWrite(1,start1,size1,start2,size2);
        if(size1==0)return false;slots[(size_t)start1]=frame;fifo.finishedWrite(1);return true;
    }
    void drain(std::vector<SpectrumFrame>& output) {
        int start1,size1,start2,size2;fifo.prepareToRead(fifo.getNumReady(),start1,size1,start2,size2);
        output.clear();output.reserve((size_t)(size1+size2));
        for(int i=0;i<size1;++i)output.push_back(slots[(size_t)(start1+i)]);
        for(int i=0;i<size2;++i)output.push_back(slots[(size_t)(start2+i)]);
        fifo.finishedRead(size1+size2);
    }
private:
    std::vector<SpectrumFrame> slots;
    juce::AbstractFifo fifo;
};
struct SpectrumHistory {
    std::deque<std::shared_ptr<const SpectrumFrame>> frames;
    void append(const SpectrumFrame& frame) {
        if(!frames.empty()&&(frames.back()->epoch!=frame.epoch||frame.time<frames.back()->time))frames.clear();
        frames.push_back(std::make_shared<const SpectrumFrame>(frame));
        while(!frames.empty()&&(frame.time-frames.front()->time>180.0||frames.size()>32768))frames.pop_front();
    }
};
struct SpectrumViewport {
    double zoomX=1,endTime=0;
    float top=0,span=1;
    bool follow=true;
    void reset(double now){zoomX=1;endTime=now;top=0;span=1;follow=true;}
    void wheel(float delta,bool shift,bool ctrl,float x,float y,double now,double baseSpan,double oldest) {
        if(ctrl) {
            const auto next=juce::jlimit(.02f,1.0f,span*std::exp(-delta*2.0f));
            top=juce::jlimit(0.0f,1.0f-next,top+y*(span-next));span=next;return;
        }
        if(follow)endTime=now;
        if(shift)endTime-=delta*baseSpan/zoomX*1.5;
        else {
            const auto oldSpan=baseSpan/zoomX;
            zoomX=juce::jlimit(.1,64.0,zoomX*std::exp((double)delta*2));
            endTime+=(1-x)*(baseSpan/zoomX-oldSpan);
        }
        endTime=juce::jlimit(juce::jmin(now,oldest+baseSpan/zoomX),now,endTime);
        follow=endTime>=now-.0001;
    }
};
}
