#include <JuceHeader.h>
#include "../PluginEditor.h"
#include "AudioFileSession.h"
#include <iostream>
#include "../tests/Revision1416Checks.h"
using namespace spectrumtag;
using namespace spectrumtag::standalone;
namespace {
juce::File defaultMediaFolder() {
#if JUCE_WINDOWS
    if (juce::File("D:/").isDirectory()) return juce::File("D:/Codex/Outputs");
#endif
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}
class Player final : public juce::AudioAppComponent,private juce::Timer {
public:
    SpectrumTagAudioProcessor processor;
    explicit Player(bool enableAudio=true){
        processor.filePlayerMode=true;processor.filePlayerVersion=ProjectInfo::versionString;processor.onLoadAudioFile=[this]{chooseAudio();};processor.onDropAudioFile=[this](const juce::File& f){load(f);};
        processor.onFileSeek=[this](double seconds){const auto s=processor.recording().snapshot();if(s){const double t=juce::jlimit(s->start(),s->end(),seconds);seek=t;processor.publishFileTransport(playing,t);}};
        session=std::make_unique<Session>(processor);editor.reset(processor.createEditor());addAndMakeVisible(*editor);
        for(auto* button:{&play,&stop,&exportButton,&device})addAndMakeVisible(button);addAndMakeVisible(status);
        play.onClick=[this]{if(processor.recording().snapshot()){if(position>=processor.recording().snapshot()->end())seek=0;playing=!playing;}};
        stop.onClick=[this]{playing=false;processor.publishFileTransport(false,position);};exportButton.onClick=[this]{chooseExport();};
        device.onClick=[this]{auto* settings=new juce::AudioDeviceSelectorComponent(deviceManager,0,0,1,2,false,false,true,false);settings->setSize(500,400);juce::DialogWindow::LaunchOptions options;options.content.setOwned(settings);options.dialogTitle=processor.getEditorState().chinese?juce::String::fromUTF8("音频设备"):"Audio device";options.dialogBackgroundColour=juce::Colour(0xff1a1c1e);options.useNativeTitleBar=true;options.resizable=false;options.launchAsync();};
        setSize(1240,722);if(enableAudio)setAudioChannels(0,2);startTimerHz(30);
    }
    ~Player()override{stopTimer();shutdownAudio();session.reset();editor.reset();processor.onFileSeek={};processor.onLoadAudioFile={};processor.onDropAudioFile={};}
    void prepareToPlay(int size,double sr)override{rate=sr;processor.setRateAndBufferSizeDetails(sr,size);processor.prepareToPlay(sr,size);}
    void releaseResources()override{processor.releaseResources();}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output)override{
        output.clearActiveBufferRegion();const auto audio=processor.recording().snapshot();
        const auto request=seek.exchange(-1);if(request>=0)position=request;
        double t=position.load();const bool run=playing.load()&&audio;
        if(run){for(int n=0;n<output.numSamples;++n){if(t>=audio->end()){playing=false;break;}for(int c=0;c<output.buffer->getNumChannels();++c)output.buffer->setSample(c,output.startSample+n,sampleAt(*audio,juce::jmin(c,audio->channels-1),t,rate));t+=1./rate;}position=t;}
        processor.publishFileTransport(playing.load()&&audio!=nullptr,t);
    }
    void resized()override{
        auto bar=getLocalBounds().removeFromTop(42).reduced(8,5);play.setBounds(bar.removeFromLeft(80));bar.removeFromLeft(6);stop.setBounds(bar.removeFromLeft(80));bar.removeFromLeft(6);exportButton.setBounds(bar.removeFromLeft(90));bar.removeFromLeft(6);device.setBounds(bar.removeFromLeft(100));bar.removeFromLeft(12);status.setBounds(bar);if(editor)editor->setBounds(getLocalBounds().withTrimmedTop(42));
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff1a1c1e));}
    bool keyPressed(const juce::KeyPress& key)override{if(key==juce::KeyPress::spaceKey){play.onClick();return true;}return editor&&editor->keyPressed(key);}
private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;std::unique_ptr<Session> session;std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton play,stop,exportButton,device;juce::Label status;std::atomic<bool> playing{false};std::atomic<double> position{0},seek{-1};double rate=48000;uint64_t seen=0;
    void load(const juce::File& file){if(session->busy())return;playing=false;session->load(file);}
    void chooseAudio(){if(session->busy())return;chooser=std::make_unique<juce::FileChooser>(processor.getEditorState().chinese?juce::String::fromUTF8("加载音频 / 视频"):"Load audio / video",defaultMediaFolder(),"*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.wma;*.opus;*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<Player>(this)](const juce::FileChooser& c){if(safe&&c.getResult().existsAsFile())safe->load(c.getResult());});}
    void chooseExport(){
        if(session->busy()||processor.recording().isBusy()||!session->exportable())return;
        if(!session->current().video){chooseExportFile(false);return;}
        const bool zh=processor.getEditorState().chinese;juce::PopupMenu menu;
        menu.addItem(1,juce::String::fromUTF8(zh?"导出视频（保留画面）":"Export video (keep picture)"));
        menu.addItem(2,juce::String::fromUTF8(zh?"仅导出音频（原音轨格式）":"Export audio only (source codec)"));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportButton),[safe=juce::Component::SafePointer<Player>(this)](int choice){if(safe&&choice>0)safe->chooseExportFile(choice==2);});
    }
    void chooseExportFile(bool audioOnly){
        const auto info=session->current();const auto ext=info.video&&audioOnly?audioExtension(info):info.file.getFileExtension();
        if(ext.isEmpty()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SpectrumTag",processor.getEditorState().chinese?juce::String::fromUTF8("暂不支持以原音轨编码导出此文件。原文件保持不变。"):"This source audio codec cannot be exported without changing format.");return;}
        auto target=defaultMediaFolder().getNonexistentChildFile(info.file.getFileNameWithoutExtension()+"_SpectrumTag",ext);
        chooser=std::make_unique<juce::FileChooser>(processor.getEditorState().chinese?juce::String::fromUTF8(audioOnly?"导出原格式音轨":"按原格式导出"):audioOnly?"Export audio in source codec":"Export in original format",target,"*"+ext);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<Player>(this),audioOnly](const juce::FileChooser& c){if(safe&&c.getResult()!=juce::File{})safe->session->exportTo(c.getResult(),audioOnly);});
    }
    void timerCallback()override{
        if(session->loadedRevision()!=seen){seen=session->loadedRevision();position=0;seek=0;processor.resetFileView();processor.publishFileTransport(false,0);}
        const bool chinese=processor.getEditorState().chinese;auto tr=[&](const char* zh,const char* en){return juce::String::fromUTF8(chinese?zh:en);};
        play.setButtonText(playing?tr("暂停","Pause"):tr("播放","Play"));stop.setButtonText(tr("停止","Stop"));exportButton.setButtonText(tr("导出","Export"));device.setButtonText(tr("音频设备","Audio device"));
        status.setText(session->status(),juce::dontSendNotification);status.setColour(juce::Label::textColourId,juce::Colours::lightgrey);
        play.setEnabled(!session->busy()&&processor.recording().snapshot()!=nullptr);exportButton.setEnabled(!session->busy()&&!processor.recording().isBusy()&&session->exportable());
    }
};
int selfTest(const juce::File& folder){try{
    folder.createDirectory();auto report=folder.getChildFile("standalone-checks.log").createOutputStream();auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto log=[&](const juce::String& text){if(report){report->writeText(text+"\n",false,false,nullptr);report->flush();}};
    auto wait=[&](auto condition){const auto until=juce::Time::getMillisecondCounter()+60000;do{juce::MessageManager::getInstance()->runDispatchLoopUntil(5);if(condition())return;juce::Thread::sleep(3);}while((int)(until-juce::Time::getMillisecondCounter())>0);throw std::runtime_error("standalone wait timed out");};
    const auto wav=folder.getChildFile("input.wav");{auto stream=wav.createOutputStream();juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000,2,24,{},0));need(writer!=nullptr,"WAV fixture");stream.release();juce::AudioBuffer<float> audio(2,96000);for(int n=0;n<96000;++n)for(int c=0;c<2;++c)audio.setSample(c,n,.2f*std::sin((float)(juce::MathConstants<double>::twoPi*(700+c*100)*n/48000)));writer->writeFromAudioSampleBuffer(audio,0,96000);}
    SpectrumTagAudioProcessor p;p.filePlayerMode=true;p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);int seeks=0;p.onFileSeek=[&](double t){++seeks;p.publishFileTransport(false,t);};Session session(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto* concrete=static_cast<SpectrumTagAudioProcessorEditor*>(editor.get());
    p.onDropAudioFile=[&](const juce::File& f){session.load(f);};
    need(revision1416::find(*editor,"loadButton")&&!revision1416::find(*editor,"recordButton"),"standalone Load replaces Record");need(!revision1416::find(*editor,"uiScale"),"standalone host-sized UI has no zoom menu");
    need(concrete->isInterestedInFileDrag({wav.getFullPathName()}),"audio drag accepted");concrete->filesDropped({wav.getFullPathName()},200,200);wait([&]{return !session.busy();});need(session.exportable(),"audio drop loads successfully");
    need(p.recording().snapshot()->sampleRate==48000&&p.recording().snapshot()->channels==2,"native file rate/channels");
    p.auditionAt(.6);need(std::abs(p.audition().position()-.6)<1.e-8,"stopped standalone seek immediate");
    auto raw=p.recording().snapshot();PrintEdit edit;edit.start=.4;edit.duration=.3;edit.fftSize=1024;edit.rows=513;edit.cols=64;edit.low=0;edit.high=.5f;edit.mask=std::make_shared<const std::vector<float>>(edit.rows*edit.cols,1.f);p.recording().add(edit);need(p.recording().flush(),"standalone Print render");auto wet=p.recording().snapshot();double wetEnergy=0,dryEnergy=0;for(int n=23000;n<29000;++n){wetEnergy+=std::pow(wet->sample(0,n),2);dryEnergy+=std::pow(raw->sample(0,n),2);}need(wetEnergy<dryEnergy*.15,"Print changes the actual audio");
    const auto out=folder.getChildFile("output.wav");session.exportTo(out);wait([&]{return !session.busy();});juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out));need(reader&&reader->sampleRate==48000&&reader->numChannels==2&&reader->bitsPerSample==24&&reader->lengthInSamples==96000,"WAV export retains format/rate/channels/depth/duration");
    juce::AudioBuffer<float> result(2,96000);reader->read(&result,0,96000,0,true,true);double maximum=0;for(int c=0;c<2;++c)for(int n=0;n<96000;++n)maximum=juce::jmax(maximum,std::abs((double)result.getSample(c,n)-wet->sample(c,n)));need(maximum<.000001,"exported WAV matches rendered audio");log("PASS drag audio, stopped seek, real Print, 24-bit WAV export matching rendered samples");
    editor.reset();p.recording().remove(edit.id);need(p.recording().flush(),"remove test print");revision1416::ui(p,folder,[&]{juce::MessageManager::getInstance()->runDispatchLoopUntil(5);},[&]{return seeks;});log("PASS shared UI first-click scale, no seek on Print, previous/next navigation, zoom, Delete/preview");
    for(auto ext:{"flac","mp3","ogg","m4a","aiff","aac","wma","opus"}){juce::String error;auto input=folder.getChildFile(juce::String("input.")+ext);need(codecRun({"-nostdin","-v","error","-i",wav.getFullPathName(),"-y",input.getFullPathName()},error),"encode input format fixture");session.load(input);wait([&]{return !session.busy();});need(session.current().file==input&&session.exportable(),"load original format");auto output=folder.getChildFile(juce::String("output.")+ext);session.exportTo(output);wait([&]{return !session.busy();});FileInfo info;juce::File decoded;need(inspect(output,info,decoded,error)&&info.rate==48000&&info.channels==2,"original-format export preserves rate/channels");if(decoded.existsAsFile())decoded.deleteFile();log(juce::String("PASS original-format export: ")+ext);}
    for(auto extension:{"mp4","mov","mkv","webm","avi","m4v"}){
        const juce::String ext(extension);const auto video=folder.getChildFile("video-input."+ext);juce::String error;
        const bool webm=ext=="webm",avi=ext=="avi",multi=ext=="mp4";
        juce::StringArray fixture{"-nostdin","-v","error","-f","lavfi","-i","testsrc2=size=160x90:rate=10:duration=3","-itsoffset","0.25","-i",wav.getFullPathName()};
        if(multi)fixture.addArray({"-i",wav.getFullPathName()});fixture.addArray({"-map","0:v:0","-map","1:a:0"});if(multi)fixture.addArray({"-map","2:a:0"});
        fixture.addArray({"-c:v",webm?"libvpx":avi?"mpeg4":"libx264","-c:a",webm?"libopus":avi?"pcm_s16le":"aac"});
        if(avi)fixture.addArray({"-af","adelay=250|250"}); // AVI PCM does not retain input itsoffset; pad fixture samples explicitly.
        if(ext=="m4v")fixture.addArray({"-f","mp4"});fixture.addArray({"-y",video.getFullPathName()});
        need(codecRun(fixture,error),"video fixture creation");
        editor.reset(p.createEditor());concrete=static_cast<SpectrumTagAudioProcessorEditor*>(editor.get());
        need(concrete->isInterestedInFileDrag({video.getFullPathName()}),"video drop recognised");concrete->filesDropped({video.getFullPathName()},200,200);wait([&]{return !session.busy();});
        const auto input=session.current();need(input.file==video&&input.video&&session.exportable(),"video drag imports soundtrack");
        auto dry=p.recording().snapshot();double leading=0;for(int n=100;n<5000;++n)leading+=std::abs(dry->sample(0,n));need(leading<.01,"video delayed audio remains aligned, leading silence retained");
        PrintEdit mark;mark.start=.5;mark.duration=.5;mark.fftSize=1024;mark.rows=513;mark.cols=64;mark.low=0;mark.high=.5f;mark.mask=std::make_shared<const std::vector<float>>(mark.rows*mark.cols,1.f);p.recording().add(mark);need(p.recording().flush(),"video Print render");
        const auto output=folder.getChildFile("video-output."+ext);session.exportTo(output);wait([&]{return !session.busy();});need(output.existsAsFile(),"video export");
        auto streamHash=[&](const juce::File& f,const char* map){juce::String hash;need(codecRun({"-nostdin","-v","error","-i",f.getFullPathName(),"-map",map,"-c","copy","-f","hash","-hash","sha256","-"},hash),"stream preservation hash");need(hash.contains("SHA256="),"valid stream checksum");return hash.trim();};
        need(streamHash(video,"0:v:0")==streamHash(output,"0:v:0"),"export must copy unchanged video packets");
        if(multi)need(streamHash(video,"0:a:1")==streamHash(output,"0:a:1"),"additional audio track preserved");
        juce::String framesIn,framesOut;need(codecRun({"-v","error","-i",video.getFullPathName(),"-map","0:v:0","-an","-f","framemd5","-"},framesIn)&&codecRun({"-v","error","-i",output.getFullPathName(),"-map","0:v:0","-an","-f","framemd5","-"},framesOut),"video frame timing check");need(framesIn==framesOut,"video frames, timestamps and duration preserved");
        const auto audioOut=folder.getChildFile("video-audio-"+ext+audioExtension(input));session.exportTo(audioOut,true);wait([&]{return !session.busy();});FileInfo audioInfo;juce::File decoded;
        need(inspect(audioOut,audioInfo,decoded,error)&&audioInfo.codec==input.codec&&audioInfo.rate==input.rate&&audioInfo.channels==input.channels,"audio-only export keeps source codec rate channels");
        std::unique_ptr<juce::AudioFormatReader> ar(formats.createReaderFor(decoded.existsAsFile()?decoded:audioOut));need(ar!=nullptr,"read exported video soundtrack");juce::AudioBuffer<float> samples(2,12000);ar->read(&samples,0,12000,28000,true,true);double energy=0,original=0;for(int n=0;n<12000;++n){energy+=std::pow(samples.getSample(0,n),2);original+=std::pow(dry->sample(0,28000+n),2);}need(energy<original*.2,"exported video soundtrack contains rendered Print");ar.reset();if(decoded.existsAsFile())decoded.deleteFile();
        log("PASS video "+ext+": drag, delayed audio alignment, Print, unchanged video packets/frames/timing, audio-only source codec"+(multi?", second audio track preserved":""));editor.reset();
    }
    {juce::String error;auto silent=folder.getChildFile("no-audio.mp4");need(codecRun({"-v","error","-f","lavfi","-i","color=black:size=16x16:duration=1","-an","-y",silent.getFullPathName()},error),"silent video fixture");auto previousAudio=p.recording().snapshot();auto previousFile=session.current().file;session.load(silent);wait([&]{return !session.busy();});need(p.recording().snapshot()==previousAudio&&session.current().file==previousFile,"video without audio preserves current session");log("PASS video without audio rejected without losing loaded session");}
    {Player panel(false);panel.processor.onDropAudioFile(wav);wait([&]{return panel.processor.recording().snapshot()&&!panel.processor.recording().isBusy();});
        auto* spectrum=revision1416::find(panel,"spectrumView");
        const auto audio=panel.processor.recording().snapshot();const auto* frame=audio->frameAt(.5);int peak=0;if(frame)for(auto v:frame->level)peak=juce::jmax(peak,(int)v);
        // The first raster can belong to the empty, pre-load view. Wait for
        // this recording revision rather than accepting any completed raster.
        wait([&]{return (bool)spectrum->getProperties()["recordMode"]
            && spectrum->getProperties().contains("rasterRevision")
            && (int)spectrum->getProperties()["rasterRevision"]==audio->revision;});
        log("Standalone display peak="+juce::String(peak)+" raster="+spectrum->getProperties()["rasterApplied"].toString());need(peak>80,"standalone loaded spectral energy");

        const auto image=panel.createComponentSnapshot(panel.getLocalBounds());int lit=0;for(int y=150;y<585;++y)for(int x=100;x<300;++x)lit+=image.getPixelAt(x,y).getBrightness()>.08f;log("Standalone spectrum lit pixels="+juce::String(lit));need(lit>100,"standalone waveform must have visible spectrum");auto stream=folder.getChildFile("Standalone-"+juce::String(ProjectInfo::versionString)+".png").createOutputStream();juce::PNGImageFormat png;need(stream&&png.writeImageToStream(image,*stream),"standalone UI snapshot");}
    p.releaseResources();log("ALL STANDALONE CHECKS PASSED");return 0;
}catch(const std::exception& e){folder.getChildFile("standalone-failure.txt").replaceWithText(e.what());return 1;}}
class App final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName()override{return "SpectrumTag";}const juce::String getApplicationVersion()override{return ProjectInfo::versionString;}bool moreThanOneInstanceAllowed()override{return true;}
    void initialise(const juce::String& command)override{
        auto args=juce::StringArray::fromTokens(command,true);
        if(args.size()>=2&&args[0]=="--codec-check"){
            juce::String error;const auto started=juce::Time::getMillisecondCounterHiRes();const auto file=resolveEncoder(error);
            const bool ok=file.existsAsFile()&&codecRun({"-version"},error);
            auto* value=new juce::DynamicObject;value->setProperty("ok",ok);value->setProperty("version",ProjectInfo::versionString);value->setProperty("codec",file.getFullPathName());value->setProperty("sha256",file.existsAsFile()?juce::SHA256(file).toHexString():juce::String());value->setProperty("elapsedMs",juce::Time::getMillisecondCounterHiRes()-started);value->setProperty("details",error);
            const auto report=juce::File(args[1].unquoted());report.getParentDirectory().createDirectory();report.replaceWithText(juce::JSON::toString(juce::var(value)));setApplicationReturnValue(ok?0:1);quit();return;
        }
        if(args.size()>=2&&args[0]=="--self-test"){setApplicationReturnValue(selfTest(juce::File(args[1].unquoted())));quit();return;}
        window=std::make_unique<Window>();
    }
    void shutdown()override{window.reset();}void systemRequestedQuit()override{quit();}
private:
    class Window final : public juce::DocumentWindow {public:Window():juce::DocumentWindow("SpectrumTag "+juce::String(ProjectInfo::versionString),juce::Colour(0xff1a1c1e),allButtons){setUsingNativeTitleBar(true);setContentOwned(new Player(),true);setResizable(true,false);setResizeLimits(930,552,3100,1742);if(auto* c=getConstrainer())c->setFixedAspectRatio(1240./722);centreWithSize(1240,722);setVisible(true);}void closeButtonPressed()override{juce::JUCEApplication::getInstance()->systemRequestedQuit();}};
    std::unique_ptr<Window> window;
};
}
START_JUCE_APPLICATION(App)
