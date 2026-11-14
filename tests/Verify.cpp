
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "shared/ImageMask.h"
#include <random>
#include <iostream>
struct Transport : juce::AudioPlayHead {
    bool playing = true; int64_t sample = 0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setTimeInSamples(sample); return p;
    }
};
static void wav(const juce::File& f, const juce::AudioBuffer<float>& b, double sr) {
    auto stream=f.createOutputStream();
    if (!stream || !stream->setPosition(0) || stream->truncate().failed()) throw std::runtime_error("WAV stream reset failed");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> w(format.createWriterFor(stream.release(),sr,(unsigned)b.getNumChannels(),24,{},0));
    if (!w || !w->writeFromAudioSampleBuffer(b,0,b.getNumSamples())) throw std::runtime_error("WAV write failed");
}

static void require(bool pass,const char* message) { if(!pass) throw std::runtime_error(message); }
static void set(SpectrumTagAudioProcessor& p,const juce::String& id,float value) {
    auto* parameter=p.getAPVTS().getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
static void png(const juce::File& f,const juce::Image& im) {auto out=f.createOutputStream();require(out&&out->setPosition(0)&&out->truncate().wasOk(),"PNG stream reset");juce::PNGImageFormat fmt;require(out&&fmt.writeImageToStream(im,*out),"PNG write");}
static ImageBoxComponent* imageBox(juce::Component& root) {
    if(auto* image=dynamic_cast<ImageBoxComponent*>(&root)) return image;
    for(auto* child:root.getChildren())if(auto* image=imageBox(*child))return image;
    return nullptr;
}
#include "InteractionChecks.h"
#include "RecordingChecks.h"
#include "NoiseChecks.h"
#include "TimelineChecks.h"
#include "Revision146Checks.h"
#include "Revision147Checks.h"
#include "Revision148Checks.h"
#include "Revision201Checks.h"
#include "Revision202Checks.h"
#include "../shared/TimeScale.h"
#include "Revision149Checks.h"
#include "Revision1410Checks.h"
#include "Revision1411Checks.h"
#include "Revision1412Checks.h"
#include "Revision1416Checks.h"
#include "RenderReplacementChecks.h"
#include "Revision1421Checks.h"
#include "Revision1427Checks.h"
static void checks(const juce::File& dir) {
    dir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s("SPECTRUMTAG_PREFS_DIR",dir.getChildFile("preferences-test").getFullPathName().toRawUTF8());
   #endif
    juce::Image art(juce::Image::ARGB,160,96,true);
    {juce::Graphics g(art); g.setColour(juce::Colours::black);g.fillRect(12,12,136,72);
        g.setColour(juce::Colours::white);g.fillEllipse(56.0f,28.0f,48.0f,40.0f);}
    png(dir.getChildFile("transparent-detail.png"),art);
    auto mask=spectrumtag::image::binarize(art,{0,0.5f});
    require(mask.getPixelAt(20,20).getAlpha()==255,"Otsu retains dark opaque artwork");
    require(mask.getPixelAt(80,48).getAlpha()==0,"Otsu preserves white interior in transparent PNG");
    require(mask.getPixelAt(0,0).getAlpha()==0,"transparent margin stays white");
    auto silhouette=spectrumtag::image::binarize(art,{3,0.5f});
    require(silhouette.getPixelAt(80,48).getAlpha()==255,"alpha silhouette is explicitly different");
    auto normal=spectrumtag::image::preview(mask,false),inverse=spectrumtag::image::preview(mask,true);
    for(int y=0;y<96;++y)for(int x=0;x<160;++x)
        require(normal.getPixelAt(x,y).getRed()+inverse.getPixelAt(x,y).getRed()==255,"invert preview complement");
    ImageBoxComponent box;box.setContentBounds({0,0,600,400});box.loadImageFromFile(dir.getChildFile("transparent-detail.png"));
    auto before=box.generateMask(257,160);box.setConversion(0,0.5f,true);
    require(before==box.generateMask(257,160),"preview invert must not invert mask twice");
    png(dir.getChildFile("preview-normal.png"),normal);png(dir.getChildFile("preview-invert.png"),inverse);
    // Low contrast text under a strong lighting gradient: compare local vs global threshold.
    juce::Image uneven(juce::Image::RGB,320,120,false);int errorsGlobal=0,errorsLocal=0;
    for(int y=0;y<120;++y)for(int x=0;x<320;++x) {
        const bool ink=(y>=30&&y<90&&x%32>=12&&x%32<18);
        float l=0.28f+0.68f*x/319; if(ink)l*=0.48f;
        uneven.setPixelAt(x,y,juce::Colour::fromFloatRGBA(l,l,l,1));
    }
    auto global=spectrumtag::image::binarize(uneven,{0,0.5f});
    auto local=spectrumtag::image::binarize(uneven,{1,0.5f});
    for(int y=0;y<120;++y)for(int x=0;x<320;++x) {
        const bool ink=(y>=30&&y<90&&x%32>=12&&x%32<18);
        errorsGlobal+=((global.getPixelAt(x,y).getAlpha()>128)!=ink);
        errorsLocal+=((local.getPixelAt(x,y).getAlpha()>128)!=ink);
    }
    require(errorsLocal<errorsGlobal/2,"local threshold should recover uneven-lighting strokes");
    std::cout<<"binarization_gradient_errors global="<<errorsGlobal<<" adaptive="<<errorsLocal<<" of 38400\n";
    png(dir.getChildFile("uneven-original.png"),uneven);
    png(dir.getChildFile("uneven-auto.png"),spectrumtag::image::preview(global,false));
    png(dir.getChildFile("uneven-adaptive.png"),spectrumtag::image::preview(local,false));
    for(int N:{1024,2048,4096})for(double sr:{44100.0,48000.0,96000.0}) {
        SpectrumTagAudioProcessor processor;Transport transport;processor.setPlayHead(&transport);
        set(processor,ParameterIDs::fftSize,N==1024?0:N==2048?1:N==4096?2:3);
        set(processor,ParameterIDs::amplitudeRatio,0.0f);
        processor.setRateAndBufferSizeDetails(sr,1024);processor.prepareToPlay(sr,1024);
        const int count=(int)(sr*1.2),latency=processor.getLatencySamples();
        std::vector<float> input(count),out(count); std::mt19937 gen(123);std::uniform_real_distribution<float>d(-0.25f,0.25f);
        for(auto&v:input)v=d(gen);
        processor.startPrint(std::vector<float>(33*20,0),33,20,0,1,0.4);
        juce::MidiBuffer midi;int cycle=0;
        for(int offset=0;offset<count;) {
            const int sizes[]={31,256,1024,7};const int length=std::min(sizes[cycle++%4],count-offset);
            juce::AudioBuffer<float>b(2,length);for(int ch=0;ch<2;++ch)b.copyFrom(ch,0,input.data()+offset,length);
            processor.processBlock(b,midi);std::copy_n(b.getReadPointer(0),length,out.data()+offset);offset+=length;transport.sample+=length;
        }
        float maximum=0;for(int i=latency;i<count;++i)maximum=std::max(maximum,std::abs(out[i]-input[i-latency]));
        std::cout<<"unity sr="<<sr<<" fft="<<N<<" max_error="<<maximum<<"\n";
        if(maximum>=0.00002f) { juce::AudioBuffer<float> debug(2,count);debug.copyFrom(0,0,input.data(),count);debug.copyFrom(1,0,out.data(),count);wav(dir.getChildFile("unity-failure.wav"),debug,sr); }
        require(maximum<0.00002f,"Print unity reconstruction/dry-wet alignment");
        const auto clock=processor.getVisualisedSamples();const int frames=processor.getDisplayUpdateCounter();
        transport.playing=false;
        for(int i=0;i<30;++i){juce::AudioBuffer<float>b(2,256);b.clear();processor.processBlock(b,midi);}
        require(processor.getVisualisedSamples()==clock && processor.getDisplayUpdateCounter()==frames,"transport stop freeze");
        transport.playing=true;
        for(int i=0;i<30;++i){juce::AudioBuffer<float>b(2,256);b.clear();processor.processBlock(b,midi);}
        require(processor.getVisualisedSamples()>clock && processor.getDisplayUpdateCounter()>frames,"silent playing must scroll");
    }
    {
        SpectrumTagAudioProcessor p; p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
        set(p,ParameterIDs::fftSize,3);p.startPrint(std::vector<float>(33*20,1),33,20,0,1,1);
        juce::AudioBuffer<float>b(2,256);b.clear();juce::MidiBuffer midi;p.processBlock(b,midi);
        require(p.getLatencySamples()==4095,"FFT change before arming Print takes effect on first block");
    }
    // Full-band mute catches the formerly unmodified Nyquist FFT bin.
    for(bool inverted:{false,true}) {
        SpectrumTagAudioProcessor p;set(p,ParameterIDs::invert,inverted?1.0f:0.0f);
        p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
        p.startPrint(std::vector<float>(33*20,inverted?0.0f:1.0f),33,20,0,1,2.0);
        juce::MidiBuffer midi;float peak=0;
        for(int block=0;block<200;++block){juce::AudioBuffer<float>b(2,256);
            for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)b.setSample(ch,i,(i%2?0.25f:-0.25f));
            p.processBlock(b,midi);if(block>140)peak=std::max(peak,b.getMagnitude(0,256));}
        require(peak<1e-5,"full-band invert/Nyquist attenuation");
    }
    SpectrumTagAudioProcessor p;set(p,ParameterIDs::imageMode,1);set(p,ParameterIDs::imageAmount,.63f);set(p,ParameterIDs::invert,1);
    SpectrumTagAudioProcessor::EditorState state;state.imagePath=dir.getChildFile("transparent-detail.png").getFullPathName();
    state.imgRectXNorm=.12f;state.imgRectWNorm=.42f;state.imgBoxWidthPx=321;state.hasValidValues=true;p.setEditorState(state);
    juce::MemoryBlock saved;p.getStateInformation(saved);
    SpectrumTagAudioProcessor restored;restored.setStateInformation(saved.getData(),(int)saved.getSize());
    require(std::abs(*restored.getAPVTS().getRawParameterValue(ParameterIDs::imageAmount)-.63f)<1e-5,"conversion parameter recall");
    require(restored.getEditorState().imgBoxWidthPx==321,"Print duration survives state save/load");
    restored.setRateAndBufferSizeDetails(48000,256);restored.prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
    auto* restoredImage=imageBox(*editor);require(restoredImage!=nullptr,"image component exists");
    require(std::abs((double)restoredImage->getWidth()/restoredImage->getHeight()-restoredImage->aspect())<.01,"recalled image preserves original aspect");
    auto expected=spectrumtag::image::preview(spectrumtag::image::binarize(art,{1,.63f}),true);
    require(restoredImage->getPreviewImage().getPixelAt(20,20)==expected.getPixelAt(20,20),"restored invert preview");
    png(dir.getChildFile("editor-state-recall-1.4.6.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    interactionChecks(dir);
    recordingChecks(dir);
    noiseChecks(dir);
    timelineChecks(dir);
    revision146Checks(dir);
    revision147Checks(dir);
    revision148Checks(dir);
    revision201Checks(dir);
    revision202Checks(dir);
    revision149Checks(dir);
    revision1410Checks(dir);
    revision1411Checks(dir);
    revision1412Checks(dir);
    {SpectrumTagAudioProcessor p;p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
     juce::AudioBuffer<float> b(1,48000);for(int i=0;i<48000;++i)b.setSample(0,i,.1f*std::sin(juce::MathConstants<double>::twoPi*700*i/48000));p.recording().setArmed(true);p.recording().capture(b,0,48000,true);p.recording().setArmed(false);require(p.recording().flush(),"1416 source");revision1416::ui(p,dir,[]{});}
    revision1416::replacements();
    revision1421Checks(dir);
    revision1427Checks(dir.getChildFile("1427-display"));
    std::cout<<"ALL CHECKS PASSED\n";
}

static void writeTestFixtures(const juce::File& dir){
    juce::Image artwork(juce::Image::ARGB,160,96,true);
    {juce::Graphics g(artwork);g.setColour(juce::Colours::black);g.fillRect(12,12,136,72);
        g.setColour(juce::Colours::white);g.fillEllipse(56.0f,28.0f,48.0f,40.0f);}
    png(dir.getChildFile("transparent-detail.png"),artwork);
}
int main(int argc,char**argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if(argc==3&&std::string(argv[1])=="--checks1427"){try{revision1427Checks(juce::File(argv[2]));return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
    if(argc==3&&(std::string(argv[1])=="--checks201"||std::string(argv[1])=="--checks202")){try{juce::File dir(argv[2]);dir.createDirectory();
      #if JUCE_WINDOWS
        _putenv_s("SPECTRUMTAG_PREFS_DIR",dir.getChildFile("preferences-test").getFullPathName().toRawUTF8());
      #endif
        writeTestFixtures(dir);
        if(std::string(argv[1])=="--checks202")revision202Checks(dir);else revision201Checks(dir);
        return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
    if(argc==3 && (std::string(argv[1])=="--checks1410"||std::string(argv[1])=="--checks1412"||std::string(argv[1])=="--checks1421")) {try {juce::File dir(argv[2]);dir.createDirectory();
      #if JUCE_WINDOWS
        _putenv_s("SPECTRUMTAG_PREFS_DIR",dir.getChildFile("preferences-test").getFullPathName().toRawUTF8());
      #endif
        writeTestFixtures(dir);
        if(std::string(argv[1])=="--checks1421")revision1421Checks(dir);else if(std::string(argv[1])=="--checks1412")revision1412Checks(dir);else revision1410Checks(dir);return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
    if(argc==3 && std::string(argv[1])=="--checks") {try {checks(juce::File(argv[2]));return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
    if(argc<4) {std::cerr<<"outdir image fftSize\n"; return 2;}
    const auto dir=juce::File(argv[1]); dir.createDirectory();
    auto img=juce::ImageFileFormat::loadFrom(juce::File(argv[2]));
    if(!img.isValid()) return 3;
    const int N=std::stoi(argv[3]), sr=48000, block=256, samples=14*sr;
    SpectrumTagAudioProcessor processor; Transport transport; processor.setPlayHead(&transport);
    auto* param=processor.getAPVTS().getParameter(ParameterIDs::fftSize);
    int choice=(N==1024?0:N==2048?1:N==4096?2:3);
    param->setValueNotifyingHost(param->convertTo0to1((float)choice));
    processor.setRateAndBufferSizeDetails(sr,block); processor.prepareToPlay(sr,block);
    processor.notifyEditorAttached();
    juce::AudioBuffer<float> input(2,samples), output(2,samples);
    std::mt19937 random(9381); std::normal_distribution<float> noise(0.0f,0.12f);
    for(int n=0;n<samples;++n) for(int ch=0;ch<2;++ch) input.setSample(ch,n,noise(random));
    const int rows=img.getHeight(), cols=img.getWidth(); std::vector<float> mask(rows*cols);
    for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) mask[r*cols+c]=img.getPixelAt(c,rows-1-r).getBrightness()<0.5f?1.0f:0.0f;
    juce::MidiBuffer midi; bool started=false;
    const auto start=juce::Time::getMillisecondCounterHiRes();
    for(int offset=0;offset<samples;offset+=block) {
        if(!started && offset>=2*sr) {processor.startPrint(std::move(mask),rows,cols,0.1f,0.9f,8.0);started=true;}
        int len=std::min(block,samples-offset); juce::AudioBuffer<float>b(2,len);
        for(int ch=0;ch<2;++ch)b.copyFrom(ch,0,input,ch,offset,len);
        processor.processBlock(b,midi);
        for(int ch=0;ch<2;++ch)output.copyFrom(ch,offset,b,ch,0,len);
        transport.sample+=len;
    }
    std::cout<<"render_ms="<<(juce::Time::getMillisecondCounterHiRes()-start)<<" latency="<<processor.getLatencySamples()<<"\n";
    wav(dir.getChildFile("input.wav"),input,sr); wav(dir.getChildFile("output.wav"),output,sr);
    int before=processor.getDisplayUpdateCounter(); transport.playing=false;
    for(int i=0;i<100;++i){juce::AudioBuffer<float>b(2,block);b.clear();processor.processBlock(b,midi);}
    std::cout<<"stopped_display_frames="<<(processor.getDisplayUpdateCounter()-before)<<"\n";
    processor.notifyEditorDetached(); processor.releaseResources();
    return 0;
}
