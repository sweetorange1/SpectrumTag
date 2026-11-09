#pragma once
#include "../Standalone/SpectrumVideo.h"
#include "../Standalone/AudioFileSession.h"
namespace spectrumtag::standalone {
// Independent scalar renderer: verify that caching changes neither frequency
// sampling nor colour, including animated camera transitions and reverse seeks.
inline juce::Image scalarMovieFrame(const RecordingSnapshot& audio,const SpectrumMovie& movie,const VideoOptions& options,double time){
    const auto view=movie.viewAt(time);const int width=options.width(),height=options.height();const double pps=view.pixelsPerSecond();
    juce::Image image(juce::Image::RGB,width,height,false,juce::SoftwareImageType{});juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
    std::array<juce::PixelRGB,256> colours;for(int i=0;i<256;++i){auto c=spectrumColour(options.palette,i/255.f);colours[i].setARGB(255,c.getRed(),c.getGreen(),c.getBlue());}
    std::vector<const SpectrumFrame*> columns((size_t)width);
    for(int x=0;x<width;++x){const double t=time+(x+.5-width)/pps;if(t>=audio.start()&&t<=time&&t<audio.end())columns[x]=audio.frameAt(t);}
    for(int y=0;y<height;++y){const double position=(y+.5)/height,nyquist=audio.sampleRate*.5;double low=0,high=nyquist;const bool visible=position>=view.y(nyquist)&&position<=view.y(0);
        if(visible)for(int n=0;n<24;++n){const double hz=(low+high)*.5;if(view.y(hz)>position)low=hz;else high=hz;}
        const float hz=(float)((low+high)*.5);auto* row=reinterpret_cast<juce::PixelRGB*>(pixels.getLinePointer(y));int bins=-1,a=0,b=0;double sr=0;float fraction=0;
        for(int x=0;x<width;++x){int level=0;if(visible)if(auto* f=columns[x];f&&f->bins>1){if(f->bins!=bins||f->sampleRate!=sr){bins=f->bins;sr=f->sampleRate;const float bin=juce::jlimit(0.f,(float)bins-1,hz/(float)(sr*.5)*(bins-1));a=(int)bin;b=juce::jmin(bins-1,a+1);fraction=bin-a;}level=juce::roundToInt(f->level[a]+fraction*(f->level[b]-f->level[a]));}row[x]=colours[(size_t)juce::jlimit(0,255,level)];}
    }return image;
}
inline int spectrumVideoChecks(const juce::File& folder){try{
    folder.createDirectory();auto logFile=folder.getChildFile("video-checks.log").createOutputStream();
    auto log=[&](const juce::String& text){logFile->writeText(text+"\n",false,false,nullptr);logFile->flush();};
    auto need=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    auto wait=[&](auto condition){const auto end=juce::Time::getMillisecondCounter()+180000;while(!condition()){juce::MessageManager::getInstance()->runDispatchLoopUntil(5);juce::Thread::sleep(5);if((int)(end-juce::Time::getMillisecondCounter())<0)throw std::runtime_error("video wait timed out");}};
    SpectrumTagAudioProcessor processor;processor.filePlayerMode=true;Session session(processor);session.ensureBlankCanvas();need(processor.recording().flush(),"blank canvas preparation");
    auto blank=processor.recording().snapshot();need(blank&&blank->sourceId.startsWith("generated:")&&blank->fileSourceLast==1&&!session.exportable(),"blank canvas has no imported media or arbitrary duration");
    auto tag=[](double start,double duration,int mode,float aspect){PrintEdit e;e.start=start;e.duration=duration;e.low=.1f;e.high=.75f;e.displayTop=20000;e.scaleMode=mode;e.fftSize=4096;e.rows=2049;e.cols=128;e.imageAspect=aspect;e.compensation=.7f;
        auto mask=std::make_shared<std::vector<float>>((size_t)e.rows*e.cols,0.f);
        // A T-shaped cutout, sampled in the selected frequency curve.
        const float top=frequency::toY(e.high*24000,e.displayTop,mode),bottom=frequency::toY(e.low*24000,e.displayTop,mode);
        for(int row=0;row<e.rows;++row){const float hz=(e.low+(e.high-e.low)*row/(e.rows-1))*24000;const float y=(frequency::toY(hz,e.displayTop,mode)-top)/(bottom-top);for(int x=0;x<e.cols;++x){const float u=(float)x/(e.cols-1);if((y>.1&&y<.28&&u>.12&&u<.88)||(y>=.28&&y<.9&&u>.4&&u<.6))(*mask)[(size_t)row*e.cols+x]=1;}}
        e.mask=mask;return e;};
    auto first=tag(.25,.8,frequency::mel,.65f),second=tag(2.4,.8,frequency::logarithmic,1.6f),overlap=tag(.5,.25,frequency::linear,1.f);
    processor.recording().setEdits({first,second,overlap});need(processor.recording().flush(30000),"generated tags render");auto audio=processor.recording().snapshot();
    need(audio&&std::abs(audio->end()-3.2)<.00003&&session.exportable(),"generated duration follows tags");
    double energy=0;for(int64_t i=audio->first;i<audio->last;++i)energy+=std::pow(audio->sample(0,i),2);need(energy>.01,"generated tags create real noise-filled sound");
    const auto wav=folder.getChildFile("generated.wav");need(session.exportTo(wav),"generated WAV starts");wait([&]{return !session.busy();});juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));need(reader&&reader->bitsPerSample==24&&reader->lengthInSamples==153600&&reader->numChannels==2,"generated audio exports 24-bit stereo WAV with exact extent");reader.reset();
    need(session.saveLastSession(),"generated session save");{SpectrumTagAudioProcessor restored;restored.filePlayerMode=true;Session recall(restored,true);wait([&]{return !recall.busy();});need(recall.current().file==juce::File{}&&recall.exportable()&&restored.recording().snapshot()->edits.size()==3,"generated session restores without source file");}
    log("PASS no-input Print: real audio, exact extent, WAV export, source-free restore");
    VideoOptions options;options.resolution=0;options.palette=3;
    SpectrumMovie movie(audio,options);auto c=movie.tagCamera(first,0);const auto bounds=movie.plot();
    const double h=(c.y(first.low*24000)-c.y(first.high*24000))*bounds.getHeight();need(std::abs(c.pixelsPerSecond*first.duration/h-first.imageAspect)<1.e-5,"Tag aspect matched by camera");
    need(movie.viewAt(.6).to.tag==0,"earlier overlapping tag owns camera");need(movie.viewAt(2.4).to.scale==frequency::logarithmic&&movie.viewAt(2.4).mix==1,"next tag's scale matched at its start");
    for(double t=1.1;t<2.4;t+=.01){auto v=movie.viewAt(t),next=movie.viewAt(t+.0001);need(std::abs(v.y(3000)-next.y(3000))<.003&&std::abs(v.pixelsPerSecond()-next.pixelsPerSecond())<2,"camera changes continuously");for(double hz=200;hz<22000;hz+=200)need(v.y(hz)<=v.y(hz-100)+.00001,"blended frequency axis remains monotonic");}
    auto saveImage=[&](double time,const char* name){auto image=movie.frame(time);juce::PNGImageFormat png;auto out=folder.getChildFile(name).createOutputStream();need(out&&png.writeImageToStream(image,*out),"video frame fixture");return image;};
    const auto before=saveImage(0,"frame-start.png"),middle=saveImage(.7,"frame-reveal.png"),after=saveImage(1.1,"frame-tag.png");saveImage(2.9,"frame-second.png");
    need(bounds==juce::Rectangle<int>(0,0,options.width(),options.height()),"spectrum fills the entire exported frame");
    int changed=0,rightEdge=0;for(int y=0;y<options.height();++y){rightEdge+=middle.getPixelAt(options.width()-1,y).getBrightness()>.05f;for(int x=0;x<options.width();++x){need(before.getPixelAt(x,y)==juce::Colours::black,"first frame has no title, rulers, text or playhead");changed+=middle.getPixelAt(x,y)!=before.getPixelAt(x,y);}}
    need(changed>100&&rightEdge>20,"real spectrum progressively enters at the extreme right edge");
    for(double time:{.7,1.3,1.8,2.4,2.9,.4,3.2}){auto actual=movie.frame(time),reference=scalarMovieFrame(*audio,movie,options,time);juce::Image::BitmapData a(actual,juce::Image::BitmapData::readOnly),b(reference,juce::Image::BitmapData::readOnly);for(int y=0;y<options.height();++y)need(std::memcmp(a.getLinePointer(y),b.getLinePointer(y),(size_t)options.width()*3)==0,"cached movie is pixel-identical to scalar reference across scale changes/seeks");}
    // Fixed input and camera; exclude encoding from the renderer benchmark.
    {auto benchOptions=options;benchOptions.resolution=1;SpectrumMovie bench(audio,benchOptions);double oldMs=0,newMs=0;int checksum=0;
        for(int pass=0;pass<2;++pass){const auto start=juce::Time::getMillisecondCounterHiRes();for(int n=0;n<90;++n){const double time=.2+n/120.;const auto image=pass?bench.frame(time):scalarMovieFrame(*audio,bench,benchOptions,time);checksum+=image.getPixelAt(1900,500).getBlue();}if(pass)newMs=juce::Time::getMillisecondCounterHiRes()-start;else oldMs=juce::Time::getMillisecondCounterHiRes()-start;}
        log("BENCH 1080p 90 frames: scalar="+juce::String(oldMs,1)+"ms cached="+juce::String(newMs,1)+"ms speedup="+juce::String(oldMs/newMs,2)+"x checksum="+juce::String(checksum));}
    constexpr int expectedWidth[4][4]={{960,1440,1920,2880},{720,1080,1440,2160},{1280,1920,2560,3840},{720,1080,1440,2160}};
    constexpr int expectedHeight[4][4]={{720,1080,1440,2160},{960,1440,1920,2880},{720,1080,1440,2160},{1280,1920,2560,3840}};
    for(int aspect=0;aspect<4;++aspect)for(int resolution=0;resolution<4;++resolution){
        auto sizedOptions=options;sizedOptions.aspect=aspect;sizedOptions.resolution=resolution;SpectrumMovie sized(audio,sizedOptions);
        need(sized.plot()==juce::Rectangle<int>(0,0,expectedWidth[aspect][resolution],expectedHeight[aspect][resolution]),"canvas matches the selected landscape/portrait dimensions");
        for(const auto& edit:{first,second}){auto v=sized.tagCamera(edit,0);const double height=(v.y(edit.low*24000)-v.y(edit.high*24000))*sized.plot().getHeight(),width=v.pixelsPerSecond*edit.duration;
            need(std::abs(width/height-edit.imageAspect)<.00001,"portrait and landscape preserve both tall and wide Tag proportions");
            need(height<=sized.plot().getHeight()*.72001&&width<=sized.plot().getWidth()*.76001,"Tag fits within the selected canvas without aspect stretching");}
        if(resolution==0){auto frame=sized.frame(.7),reference=scalarMovieFrame(*audio,sized,sizedOptions,.7);juce::Image::BitmapData a(frame,juce::Image::BitmapData::readOnly),b(reference,juce::Image::BitmapData::readOnly);
            for(int y=0;y<frame.getHeight();++y)need(std::memcmp(a.getLinePointer(y),b.getLinePointer(y),(size_t)frame.getWidth()*3)==0,"all aspect frames match independent spectrum sampling");}
    }
    log("PASS full-frame spectrum only, right-edge reveal, pixel-exact cache, camera aspect at all sizes, overlap priority, continuous scale");
    options.resolution=0;SpectrumVideoExport exporter;juce::String error;
    auto encode=[&](std::shared_ptr<const RecordingSnapshot> source,VideoOptions value,const juce::File& out){
        const auto start=juce::Time::getMillisecondCounterHiRes();need(exporter.start(source,value,out),"video export starts");
        double nextLog=start+10000;
        try{wait([&]{const auto now=juce::Time::getMillisecondCounterHiRes();if(now>=nextLog){log("ENCODING "+out.getFileName()+" stage="+juce::String((int)exporter.stage())+" progress="+juce::String(exporter.progress(),4));nextLog=now+10000;}return !exporter.busy();});}
        catch(...){log("TIMEOUT "+out.getFileName()+" stage="+juce::String((int)exporter.stage())+" progress="+juce::String(exporter.progress(),4)+" "+exporter.error());throw;}
        log("ENCODE "+out.getFileName()+" "+juce::String(juce::Time::getMillisecondCounterHiRes()-start,1)+"ms "+exporter.error());need(exporter.stage()==SpectrumVideoExport::complete&&out.getSize()>1000,"video export completes");
    };
    const auto main=folder.getChildFile("SpectrumTag-animation.mp4");encode(audio,options,main);
    juce::String frames;need(codecRun({"-v","error","-i",main.getFullPathName(),"-map","0:v:0","-an","-f","framemd5","-"},frames),"decode all exported frames");int frameCount=0;for(auto line:juce::StringArray::fromLines(frames))if(line.startsWith("0,"))++frameCount;need(frameCount==96,"3.2 seconds at 30 fps");
    juce::String info;codecRun({"-hide_banner","-i",main.getFullPathName()},info);need(info.contains("1280x720")&&info.contains("30 fps")&&info.contains("Audio: aac"),"video resolution fps and processed soundtrack");
    const auto decodedFrame=folder.getChildFile("decoded-colour.png");need(codecRun({"-v","error","-i",main.getFullPathName(),"-vf","select=eq(n\\,21)","-frames:v","1","-y",decodedFrame.getFullPathName()},error),"decode colour verification frame");
    const auto decoded=juce::ImageFileFormat::loadFrom(decodedFrame);need(decoded.isValid(),"decoded video frame");double colourError=0;int samplesCompared=0;for(int y=0;y<options.height();y+=3)for(int x=0;x<options.width();x+=3){const auto a=middle.getPixelAt(x,y),b=decoded.getPixelAt(x,y);if(a.getBrightness()>.15f){colourError+=std::abs(a.getRed()-b.getRed())+std::abs(a.getGreen()-b.getGreen())+std::abs(a.getBlue()-b.getBlue());++samplesCompared;}}
    need(samplesCompared>100&&colourError/(3*samplesCompared)<16,"native RGB/BGR pipe preserves palette through video encoding");
    need(codecRun({"-v","error","-i",main.getFullPathName(),"-vn","-c:a","pcm_f32le","-y",folder.getChildFile("movie-audio.wav").getFullPathName()},error),"decode soundtrack");std::unique_ptr<juce::AudioFormatReader> ar(formats.createReaderFor(folder.getChildFile("movie-audio.wav")));need(ar&&ar->sampleRate==48000,"soundtrack rate");juce::AudioBuffer<float> samples(2,(int)ar->lengthInSamples);ar->read(&samples,0,samples.getNumSamples(),0,true,true);double dot=0,aa=0,bb=0;for(int n=0;n<juce::jmin(153600,samples.getNumSamples());++n){const double a=audio->sample(0,n),b=samples.getSample(0,n);dot+=a*b;aa+=a*a;bb+=b*b;}need(dot/std::sqrt(aa*bb)>.94,"video soundtrack retains real processed audio and sample alignment");
    log("PASS MP4 720p30, 96 decoded frames, synchronised processed soundtrack");
    // Short probes exercise every format, size and frame-rate choice without
    // encoding a full song four times. Source snapshot remains immutable.
    auto tiny=std::make_shared<RecordingSnapshot>(*audio);tiny->last=7200;
    for(int format=0;format<4;++format){options.format=format;options.aspect=format;options.resolution=format;options.fps=format%2?60:30;const auto file=folder.getChildFile("probe"+options.extension());encode(tiny,options,file);codecRun({"-hide_banner","-i",file.getFullPathName()},info);need(info.contains(juce::String(expectedWidth[format][format])+"x"+juce::String(expectedHeight[format][format]))&&info.contains(juce::String(options.fps)+" fps"),"encoded format/aspect/resolution/fps probe");log("PASS "+options.extension()+" "+juce::String(options.width())+"x"+juce::String(options.height())+" "+juce::String(options.fps)+"fps");}
    {auto portrait=options;portrait.aspect=VideoOptions::ratio9x16;portrait.resolution=0;portrait.format=0;portrait.fps=30;
        const auto file=folder.getChildFile("portrait-tag.mp4");encode(audio,portrait,file);
        const auto frameFile=folder.getChildFile("portrait-decoded.png");need(codecRun({"-v","error","-i",file.getFullPathName(),"-vf","select=eq(n\\,21)","-frames:v","1","-y",frameFile.getFullPathName()},error),"decode portrait Tag frame");
        const auto actual=juce::ImageFileFormat::loadFrom(frameFile);need(actual.getWidth()==720&&actual.getHeight()==1280,"portrait file has physical portrait pixels, not just rotation metadata");
        SpectrumMovie portraitMovie(audio,portrait);const auto reference=portraitMovie.frame(.7);double difference=0;int count=0;
        for(int y=0;y<1280;y+=3)for(int x=0;x<720;x+=3){auto a=actual.getPixelAt(x,y),b=reference.getPixelAt(x,y);difference+=std::abs(a.getRed()-b.getRed())+std::abs(a.getGreen()-b.getGreen())+std::abs(a.getBlue()-b.getBlue());++count;}
        need(difference/(3*count)<16,"encoded portrait keeps Tag geometry and palette");log("PASS 9:16 physical canvas and decoded Tag pixels");}
    options.remember();auto saved=VideoOptions::remembered();need(saved.format==options.format&&saved.aspect==options.aspect&&saved.resolution==options.resolution&&saved.fps==options.fps,"video options including portrait aspect persist");
    {const auto file=Preferences::file().getParentDirectory().getChildFile("Standalone/video-export.xml");juce::XmlElement legacy("SpectrumVideo");legacy.setAttribute("format",2);legacy.setAttribute("resolution",2);legacy.setAttribute("fps",60);need(legacy.writeTo(file),"legacy settings fixture");
        const auto old=VideoOptions::remembered();need(old.aspect==VideoOptions::ratio16x9&&old.width()==2560&&old.height()==1440&&old.format==2&&old.fps==60,"old settings retain 16:9 and existing quality choices");
        legacy.setAttribute("aspect",99);need(legacy.writeTo(file),"invalid aspect fixture");need(VideoOptions::remembered().aspect==VideoOptions::ratio16x9,"invalid stored aspect safely defaults to 16:9");}
    const auto protectedFile=folder.getChildFile("cancel-keep.mp4");protectedFile.replaceWithText("existing destination must survive cancellation");const auto hash=juce::SHA256(protectedFile).toHexString();options.resolution=0;options.format=0;
    need(exporter.start(audio,options,protectedFile),"cancel job starts");wait([&]{return exporter.progress()>.05f||!exporter.busy();});exporter.cancel();wait([&]{return !exporter.busy();});need(exporter.stage()==SpectrumVideoExport::cancelled&&juce::SHA256(protectedFile).toHexString()==hash,"cancel leaves existing destination untouched");
    auto beforeDelete=processor.recording().document();processor.recording().setEdits({first,overlap});need(processor.recording().flush(),"delete last generated tag");need(std::abs(processor.recording().snapshot()->end()-1.05)<.0001,"delete shortens generated duration");processor.recording().restoreDocument(beforeDelete);need(processor.recording().flush(),"restore generated tags");need(processor.recording().snapshot()->last==audio->last,"generated undo extent");
    log("PASS remembered settings, cancellable encoder, safe destination, generated deletion/restore");log("ALL SPECTRUM VIDEO CHECKS PASSED");return 0;
}catch(const std::exception& e){folder.getChildFile("video-failure.txt").replaceWithText(e.what());return 1;}}
}
