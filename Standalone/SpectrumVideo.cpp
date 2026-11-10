#include "SpectrumVideo.h"
#include "AudioFileSession.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <cerrno>
#endif

namespace spectrumtag::standalone {
namespace {
juce::File settingsFile(){return Preferences::file().getParentDirectory().getChildFile("Standalone/video-export.xml");}
double smooth(double t){t=std::clamp(t,0.,1.);return t*t*t*(t*(t*6-15)+10);}

// Send one frame at a time, without an image sequence or a large raw video on
// disk. Pending writes remain cancellable if the encoder stalls or exits.
class FramePipe {
public:
    explicit FramePipe(const juce::File& scratch){
#if JUCE_WINDOWS
        name="\\\\.\\pipe\\SpectrumTag-"+juce::Uuid().toString();
        handle=CreateNamedPipeW(name.toWideCharPointer(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1024*1024,1024*1024,0,nullptr);
        event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
#else
        name=scratch.withFileExtension(".pipe").getFullPathName();
        created=mkfifo(name.toRawUTF8(),0600)==0;
        sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);pthread_sigmask(SIG_BLOCK,&blocked,&oldMask);
#endif
    }
    ~FramePipe(){close();
#if JUCE_WINDOWS
        if(event)CloseHandle(event);
#else
        if(created)unlink(name.toRawUTF8());
        sigset_t pending;sigpending(&pending);if(sigismember(&pending,SIGPIPE)&&!sigismember(&oldMask,SIGPIPE)){int signal=0;sigwait(&blocked,&signal);}
        pthread_sigmask(SIG_SETMASK,&oldMask,nullptr);
#endif
    }
    bool valid()const{
#if JUCE_WINDOWS
        return handle!=INVALID_HANDLE_VALUE&&event;
#else
        return created;
#endif
    }
    juce::String path()const{return name;}
    bool connect(juce::ChildProcess& child,const std::function<bool()>& stop){
#if JUCE_WINDOWS
        OVERLAPPED op{};op.hEvent=event;ResetEvent(event);
        if(ConnectNamedPipe(handle,&op))return true;
        const auto e=GetLastError();if(e==ERROR_PIPE_CONNECTED)return true;
        if(e!=ERROR_IO_PENDING)return false;DWORD count=0;return await(op,count,child,stop);
#else
        while(!stop()&&child.isRunning()){fd=open(name.toRawUTF8(),O_WRONLY|O_NONBLOCK);if(fd>=0)return true;if(errno!=ENXIO&&errno!=EINTR)return false;juce::Thread::sleep(10);}return false;
#endif
    }
    bool write(const void* bytes,size_t size,juce::ChildProcess& child,const std::function<bool()>& stop){
        auto* data=static_cast<const char*>(bytes);
        while(size){if(stop())return false;
#if JUCE_WINDOWS
            OVERLAPPED op{};op.hEvent=event;ResetEvent(event);DWORD written=0;
            if(!WriteFile(handle,data,(DWORD)juce::jmin(size,(size_t)1048576),&written,&op)){
                if(GetLastError()!=ERROR_IO_PENDING||!await(op,written,child,stop))return false;
            }
#else
            const auto written=::write(fd,data,juce::jmin(size,(size_t)1048576));
            if(written<0){
                if(errno==EINTR)continue;
                if((errno==EAGAIN||errno==EWOULDBLOCK)&&child.isRunning()){
                    // A FIFO may accept only a few KiB at a time on macOS.
                    // Wait for space rather than adding 5 ms to every chunk;
                    // keep the wait bounded so cancellation remains responsive.
                    pollfd writable{fd,POLLOUT,0};
                    const auto ready=::poll(&writable,1,20);
                    if(ready<0&&errno!=EINTR)return false;
                    if(ready>0&&(writable.revents&(POLLERR|POLLHUP|POLLNVAL)))return false;
                    continue;
                }
                return false;
            }
#endif
            if(written==0)return false;data+=written;size-=(size_t)written;
        }return true;
    }
    void close(){
#if JUCE_WINDOWS
        if(handle!=INVALID_HANDLE_VALUE){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}
#else
        if(fd>=0){::close(fd);fd=-1;}
#endif
    }
private:
    juce::String name;
#if JUCE_WINDOWS
    HANDLE handle=INVALID_HANDLE_VALUE,event=nullptr;
    bool await(OVERLAPPED& op,DWORD& bytes,juce::ChildProcess& child,const std::function<bool()>& stop){
        while(WaitForSingleObject(event,20)==WAIT_TIMEOUT){if(stop()||!child.isRunning()){
            CancelIoEx(handle,&op);GetOverlappedResult(handle,&op,&bytes,TRUE);return false;
        }}return GetOverlappedResult(handle,&op,&bytes,FALSE)!=FALSE;
    }
#else
    int fd=-1;bool created=false;sigset_t blocked{},oldMask{};
#endif
};
}

VideoOptions VideoOptions::remembered(){VideoOptions result;if(auto xml=juce::XmlDocument::parse(settingsFile())){result.format=xml->getIntAttribute("format");result.resolution=xml->getIntAttribute("resolution",1);result.fps=xml->getIntAttribute("fps",30);result.aspect=xml->getIntAttribute("aspect",ratio16x9);}result.normalise();return result;}
void VideoOptions::remember()const{auto v=*this;v.normalise();juce::XmlElement xml("SpectrumVideo");xml.setAttribute("format",v.format);xml.setAttribute("resolution",v.resolution);xml.setAttribute("fps",v.fps);xml.setAttribute("aspect",v.aspect);auto file=settingsFile();file.getParentDirectory().createDirectory();juce::TemporaryFile temporary(file);if(xml.writeTo(temporary.getFile()))temporary.overwriteTargetFileWithTemporary();}

SpectrumMovie::Camera SpectrumMovie::tagCamera(const PrintEdit& e,int index)const{
    Camera c;c.scale=e.scaleMode;c.top=juce::jlimit(21.f,(float)audio->sampleRate*.5f,e.displayTop);c.tag=index;
    const double nyquist=audio->sampleRate*.5;
    const double high=frequency::toY((float)(e.high*nyquist),c.top,c.scale),low=frequency::toY((float)(e.low*nyquist),c.top,c.scale);
    const double fraction=juce::jmax(.000001,low-high),aspect=std::isfinite(e.imageAspect)&&e.imageAspect>0?e.imageAspect:1.;
    const double height=juce::jmin(bounds.getHeight()*.72,bounds.getWidth()*.76/aspect);
    c.ySpan=fraction*bounds.getHeight()/height;c.yStart=(high+low-c.ySpan)*.5;
    c.pixelsPerSecond=height*aspect/juce::jmax(.000001,e.duration);return c;
}
SpectrumMovie::SpectrumMovie(std::shared_ptr<const RecordingSnapshot> source,VideoOptions settings):audio(std::move(source)),options(settings){
    bounds={0,0,options.width(),options.height()};
    for(int i=0;i<256;++i){const auto c=spectrumColour(options.palette,i/255.f);colours[i].setARGB(255,c.getRed(),c.getGreen(),c.getBlue());}
    initial.scale=options.scale;initial.top=juce::jlimit(21.f,(float)audio->sampleRate*.5f,options.top);initial.pixelsPerSecond=60.*juce::jmax(.01f,options.speed)*bounds.getHeight()/466.;
    std::vector<int> order;for(int i=0;i<(int)audio->edits.size();++i)if(audio->edits[i].valid())order.push_back(i);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return audio->edits[a].start<audio->edits[b].start;});
    Camera previous=initial;double lockedUntil=audio->start();bool first=true;
    for(int index:order){const auto& tag=audio->edits[index];const double end=tag.start+tag.duration;
        if(end<=lockedUntil)continue;const Camera next=tagCamera(tag,index);
        if(first&&tag.start-audio->start()<1.25){initial=previous=next;lockedUntil=end;first=false;continue;}
        first=false;
        // Earlier overlapping tags retain their camera until their end. A
        // later tag extending past it is followed for its remaining duration.
        const bool overlap=tag.start<lockedUntil;
        const double begin=overlap?lockedUntil:juce::jmax(lockedUntil,tag.start-1.25);
        const double finish=overlap?juce::jmin(end,begin+1.25):tag.start;
        cues.push_back({begin,finish,previous,next});previous=next;lockedUntil=end;
    }
}
SpectrumMovie::View SpectrumMovie::viewAt(double time)const{
    View result{initial,initial,1};for(const auto& cue:cues){if(time<cue.begin)break;if(time<cue.end)return {cue.from,cue.to,smooth((time-cue.begin)/(cue.end-cue.begin))};result={cue.to,cue.to,1};}return result;
}
juce::Image SpectrumMovie::frame(double time)const{
    const auto view=viewAt(time);const double pps=view.pixelsPerSecond();
    const int width=bounds.getWidth(),height=bounds.getHeight();++frameSerial;
    const auto sameAxis=[](const Camera& a,const Camera& b){return a.scale==b.scale&&a.top==b.top&&a.yStart==b.yStart&&a.ySpan==b.ySpan;};
    if(rowHz.empty()||cachedView.mix!=view.mix||!sameAxis(cachedView.from,view.from)||!sameAxis(cachedView.to,view.to)){
        cachedView=view;columnCache.clear();rowHz.resize((size_t)height);
        const double nyquist=audio->sampleRate*.5,top=view.y(nyquist),bottom=view.y(0);
        for(int y=0;y<height;++y){const double position=(y+.5)/height;double low=0,high=nyquist;
            const bool visible=position>=top&&position<=bottom;
            if(visible)for(int n=0;n<24;++n){const double hz=(low+high)*.5;if(view.y(hz)>position)low=hz;else high=hz;}
            rowHz[(size_t)y]=visible?(float)((low+high)*.5):-1.f;
        }
    }
    // Keep at most the previous and current visible columns, independent of
    // song duration. A camera change discards all frequency-dependent colours.
    if(columnCache.size()>(size_t)width)for(auto it=columnCache.begin();it!=columnCache.end();){
        if(it->second.used+1<frameSerial)it=columnCache.erase(it);else ++it;
    }
    struct Run {int x,count;const juce::PixelRGB* pixels;};
    std::vector<Run> runs;runs.reserve((size_t)width);
    const SpectrumFrame* previous=nullptr;
    for(int x=0;x<width;++x){
        // The newest column touches the right edge. No future-time margin.
        const double t=time+(x+.5-width)/pps;
        const auto* f=t>=audio->start()&&t<=time&&t<audio->end()?audio->frameAt(t):nullptr;
        if(!runs.empty()&&f==previous){++runs.back().count;continue;}previous=f;
        const juce::PixelRGB* pixels=nullptr;
        if(f&&f->bins>1){
            auto result=columnCache.try_emplace(f);auto& column=result.first->second;column.used=frameSerial;
            if(result.second){column.pixels.resize((size_t)height);
                for(int y=0;y<height;++y){const float hz=rowHz[(size_t)y];int level=0;
                    if(hz>=0){const float bin=juce::jlimit(0.f,(float)f->bins-1,hz/(float)(f->sampleRate*.5)*(f->bins-1));
                        const int a=(int)bin,b=juce::jmin(f->bins-1,a+1);const float fraction=bin-a;
                        level=juce::roundToInt(f->level[a]+fraction*(f->level[b]-f->level[a]));
                    }column.pixels[(size_t)y]=colours[(size_t)juce::jlimit(0,255,level)];
                }
            }pixels=column.pixels.data();
        }
        runs.push_back({x,1,pixels});
    }
    juce::Image image(juce::Image::RGB,width,height,false,juce::SoftwareImageType{});
    juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
    for(int y=0;y<height;++y){auto* row=reinterpret_cast<juce::PixelRGB*>(pixels.getLinePointer(y));
        for(const auto& run:runs)std::fill_n(row+run.x,run.count,run.pixels?run.pixels[y]:colours[0]);
    }
    return image;
}
SpectrumVideoExport::SpectrumVideoExport():juce::Thread("SpectrumTag video export"){}
SpectrumVideoExport::~SpectrumVideoExport(){cancel();stopThread(-1);}
bool SpectrumVideoExport::start(std::shared_ptr<const RecordingSnapshot> snapshot,VideoOptions settings,const juce::File& file){
    if(active.exchange(true))return false;stopThread(-1);if(!snapshot||snapshot->empty()){active=false;return false;}
    settings.normalise();audio=std::move(snapshot);options=settings;target=file;cancelRequested=false;fraction=0;currentStage=preparing;
    {std::lock_guard<std::mutex> lock(mutex);failure.clear();}if(!startThread()){active=false;currentStage=failed;return false;}return true;
}
juce::String SpectrumVideoExport::error()const{std::lock_guard<std::mutex> lock(mutex);return failure;}
void SpectrumVideoExport::run(){
    juce::String message;bool ok=false;const auto stop=[this]{return cancelRequested.load()||threadShouldExit();};
    const auto wave=workFolder().getChildFile("movie-"+juce::Uuid().toString()+".wav");
    struct Cleanup{juce::File file;~Cleanup(){file.deleteFile();}} cleanup{wave};
    try{do{
        const auto codec=resolveEncoder(message);if(!codec.existsAsFile())break;
        if(!writeFloatWave(wave,*audio,message,stop,[this](float value){fraction=.02f*value;})||stop())break;
        if(target.getParentDirectory().createDirectory().failed()){message="Cannot create the destination folder.";break;}
        juce::TemporaryFile output(target);FramePipe pipe(wave);if(!pipe.valid()){message="Cannot open the video encoder pipe.";break;}
        const auto duration=audio->end()-audio->start();const int count=(int)std::ceil(duration*options.fps);
        const juce::String container=juce::StringArray{"mp4","mov","matroska","webm"}[options.format];
        const auto pixelFormat=juce::PixelRGB::indexR==0?"rgb24":"bgr24";
        const int encoderThreads=juce::jlimit(2,8,juce::SystemStats::getNumCpus()/2);
        juce::StringArray args{codec.getFullPathName(),"-nostdin","-v","error","-f","rawvideo","-pixel_format",pixelFormat,"-video_size",juce::String(options.width())+"x"+juce::String(options.height()),"-framerate",juce::String(options.fps),"-i",pipe.path(),"-i",wave.getFullPathName(),"-map","0:v:0","-map","1:a:0","-threads",juce::String(encoderThreads)};
        if(options.format==3)args.addArray({"-c:v","libvpx-vp9","-deadline","good","-cpu-used","5","-row-mt","1","-crf","24","-b:v","0","-c:a","libopus","-b:a","192k"});
        else args.addArray({"-c:v","libx264","-preset","fast","-crf","17","-c:a","aac","-b:a","256k"});
        args.addArray({"-pix_fmt","yuv420p","-t",juce::String(duration,9)});if(options.format<2)args.addArray({"-movflags","+faststart"});args.addArray({"-f",container,"-y",output.getFile().getFullPathName()});
        juce::ChildProcess child;struct Kill{juce::ChildProcess& child;~Kill(){if(child.isRunning()){child.kill();child.waitForProcessToFinish(3000);}}} kill{child};
        if(!child.start(args,0)||!pipe.connect(child,stop)){message="Cannot connect to the video encoder.";break;}
        SpectrumMovie movie(audio,options);bool sent=true;currentStage=frames;
        for(int index=0;index<count&&!stop();++index){
            const double time=index==count-1?audio->end():audio->start()+(double)index/options.fps;
            const auto image=movie.frame(time);juce::Image::BitmapData pixels(image,juce::Image::BitmapData::readOnly);
            // JUCE uses BGR on Windows and RGB on Apple. Tell the encoder the
            // actual layout; never swizzle/copy every pixel into another frame.
            const size_t rowBytes=(size_t)options.width()*sizeof(juce::PixelRGB);
            if((size_t)pixels.lineStride==rowBytes)sent=pipe.write(pixels.data,rowBytes*options.height(),child,stop);
            else for(int y=0;y<options.height()&&sent;++y)sent=pipe.write(pixels.getLinePointer(y),rowBytes,child,stop);
            if(!sent)break;fraction=.02f+.93f*(index+1)/count;
        }
        // All overlapped writes have completed before closing the pipe. EOF
        // lets the encoder drain delayed frames and write the container index.
        pipe.close();if(stop())break;if(!sent){message="The video encoder stopped before all frames were written.";break;}currentStage=finishing;
        while(child.isRunning()&&!stop())juce::Thread::sleep(20);
        if(stop())break;if(child.getExitCode()!=0||output.getFile().getSize()<32){message="Video encoding failed. Check available disk space and try another format.";break;}
        if(!output.overwriteTargetFileWithTemporary()){message="Cannot save the completed video.";break;}ok=true;
    }while(false);}catch(const std::exception& e){message=juce::String(e.what());}
    {std::lock_guard<std::mutex> lock(mutex);failure=message;}currentStage=ok?complete:stop()?cancelled:failed;if(ok)fraction=1;active=false;
}
}
