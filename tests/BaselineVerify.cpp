
#include "PluginProcessor.h"
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

int main(int argc,char**argv) {
    juce::ScopedJuceInitialiser_GUI init;
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
