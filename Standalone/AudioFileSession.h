#pragma once
#include "../PluginProcessor.h"
#include <mutex>
#include "EmbeddedCodec.h"
namespace spectrumtag::standalone {
struct FileInfo {juce::File file;double rate=0;int channels=0,bits=24;bool floating=false,video=false;juce::String codec;};
inline juce::File workFolder(){
#if JUCE_WINDOWS
    auto folder=juce::File("D:/").isDirectory() ? juce::File("D:/Codex/Temp/SpectrumTag-Standalone")
        : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SpectrumTag/Temporary");
#else
    auto folder=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SpectrumTag/Temporary");
#endif
    folder.createDirectory();return folder;
}
inline bool codecRun(juce::StringArray args,juce::String& output){
    const auto codec=resolveEncoder(output);if(!codec.existsAsFile()){if(output.isEmpty())output="Audio encoder is unavailable.";return false;}
    args.insert(0,codec.getFullPathName());juce::ChildProcess child;
    if(!child.start(args)){output="Cannot start the audio encoder: "+codec.getFullPathName();return false;}
    // Drain pipes while encoding; otherwise long stderr output can block the child.
    output=child.readAllProcessOutput();return child.waitForProcessToFinish(10000)&&child.getExitCode()==0;
}
inline bool writeFloatWave(const juce::File& path,const RecordingSnapshot& audio,juce::String& error){
    auto out=path.createOutputStream();if(!out){error="Cannot create export audio.";return false;}
    out->setPosition(0);out->truncate();const auto samples=audio.last-audio.first;
    const uint64_t size=(uint64_t)samples*audio.channels*4;
    if(size>0xffffffffu-36){error="Audio is too large for this export.";return false;}
    out->write("RIFF",4);out->writeInt((int)(size+36));out->write("WAVEfmt ",8);out->writeInt(16);out->writeShort(3);out->writeShort((short)audio.channels);
    out->writeInt((int)audio.sampleRate);out->writeInt((int)audio.sampleRate*audio.channels*4);out->writeShort((short)(audio.channels*4));out->writeShort(32);out->write("data",4);out->writeInt((int)size);
    for(int64_t first=audio.first;first<audio.last;first+=4096){juce::MemoryOutputStream block;
        for(int64_t n=first;n<juce::jmin(audio.last,first+4096);++n)for(int c=0;c<audio.channels;++c)block.writeFloat(audio.sample(c,n));
        if(!out->write(block.getData(),block.getDataSize())){error="Export write failed.";return false;}
    }
    out->flush();if(out->getStatus().failed()){error=out->getStatus().getErrorMessage();return false;}return true;
}
inline bool inspect(const juce::File& file,FileInfo& info,juce::File& decoded,juce::String& error){
    info.file=file;juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    juce::String log;codecRun({"-hide_banner","-i",file.getFullPathName()},log);
    if(!log.contains("Audio: ")){error=log.contains("Input #")?juce::String("No readable audio track in this file."):log;return false;}
    const auto extension=file.getFileExtension().toLowerCase();
    info.video=juce::StringArray{".mp4",".m4v",".mov",".mkv",".webm",".avi"}.contains(extension)&&log.contains("Video: ");
    const auto line=log.fromFirstOccurrenceOf("Audio: ",false,false);info.codec=line.upToFirstOccurrenceOf(",",false,false).trim().upToFirstOccurrenceOf(" ",false,false);
    if(info.video)reader.reset(); // Always honour video-track timestamps via the decoder.
    if(!reader){
        const auto durationText=log.fromFirstOccurrenceOf("Duration: ",false,false).upToFirstOccurrenceOf(",",false,false).trim();
        const auto parts=juce::StringArray::fromTokens(durationText,":","");
        const double seconds=parts.size()==3?parts[0].getDoubleValue()*3600+parts[1].getDoubleValue()*60+parts[2].getDoubleValue():0;
        const double sourceRate=line.upToFirstOccurrenceOf(" Hz",false,false).fromLastOccurrenceOf(", ",false,false).getDoubleValue();
        if(seconds<=0||seconds>900||sourceRate<8000||sourceRate>384000||seconds*sourceRate>64000000){error="Audio limit: 15 minutes / 64M samples, 8-384 kHz.";return false;}
        decoded=workFolder().getNonexistentChildFile("decode-"+juce::Uuid().toString(),".wav");
        if(!codecRun({"-nostdin","-v","error","-i",file.getFullPathName(),"-map","0:a:0","-vn","-af","aresample=async=1:first_pts=0","-c:a","pcm_f32le","-fs","536871000","-y",decoded.getFullPathName()},error))return false;
        if(decoded.getSize()>=536870912){error="Decoded audio exceeds the supported size.";return false;}
        reader.reset(formats.createReaderFor(decoded));
    }
    if(!reader){error="Cannot decode the selected audio.";return false;}
    if(reader->numChannels<1||reader->numChannels>2){error="Please use mono or stereo audio.";return false;}
    if(reader->lengthInSamples<1||reader->lengthInSamples>64000000||reader->lengthInSamples/reader->sampleRate>900||reader->sampleRate<8000||reader->sampleRate>384000){error="Audio limit: 15 minutes / 64M samples, 8-384 kHz.";return false;}
    info.rate=reader->sampleRate;info.channels=(int)reader->numChannels;info.bits=(int)reader->bitsPerSample;info.floating=reader->usesFloatingPointData;return true;
}
inline juce::String audioExtension(const FileInfo& info){
    const auto c=info.codec;
    if(c=="aac"||c=="alac")return ".m4a";
    if(c=="mp3")return ".mp3";if(c=="mp2")return ".mp2";
    if(c=="opus")return ".opus";if(c=="vorbis")return ".ogg";
    if(c=="flac")return ".flac";if(c=="ac3")return ".ac3";if(c=="eac3")return ".eac3";
    if(c=="wmav1"||c=="wmav2")return ".wma";
    if(c.startsWith("pcm_"))return c.endsWith("be")?".aiff":".wav";
    return {};
}
inline juce::String audioEncoder(const FileInfo& info){
    if(audioExtension(info).isEmpty())return {};
    if(info.codec=="mp3")return "libmp3lame";if(info.codec=="opus")return "libopus";
    if(info.codec=="vorbis")return "libvorbis";return info.codec;
}
inline bool exportOriginal(const FileInfo& info,const RecordingSnapshot& audio,const juce::File& target,juce::String& error,bool audioOnly=false){
    if(target==info.file){error="Choose a new file name to retain the source audio.";return false;}
    const auto ext=info.video&&audioOnly?audioExtension(info):info.file.getFileExtension().toLowerCase();
    if(target.getFileExtension().toLowerCase()!=ext){error="Keep the original file extension: "+ext;return false;}
    const auto wave=workFolder().getNonexistentChildFile("render-"+juce::Uuid().toString(),".wav");
    const auto encoded=workFolder().getNonexistentChildFile("export-"+juce::Uuid().toString(),ext);
    // Only these newly created scratch files are cleaned up; user files are never removed.
    struct Scratch {juce::File a,b;~Scratch(){a.deleteFile();b.deleteFile();}} scratch{wave,encoded};
    if(!writeFloatWave(wave,audio,error))return false;
    juce::String codec;
    if(info.video){codec=audioEncoder(info);if(codec.isEmpty()){error="The source audio codec cannot be exported without changing format.";return false;}}
    else if(ext==".mp4"||ext==".m4v"||ext==".mov"||ext==".mkv"||ext==".webm"||ext==".avi")codec=audioEncoder(info);
    else if(ext==".wav"||ext==".wave")codec=info.floating?(info.bits>32?"pcm_f64le":"pcm_f32le"):info.bits<=8?"pcm_u8":info.bits<=16?"pcm_s16le":info.bits<=24?"pcm_s24le":"pcm_s32le";
    else if(ext==".aif"||ext==".aiff")codec=info.floating?(info.bits>32?"pcm_f64be":"pcm_f32be"):info.bits<=8?"pcm_s8":info.bits<=16?"pcm_s16be":info.bits<=24?"pcm_s24be":"pcm_s32be";
    else if(ext==".flac")codec="flac";
    else if(ext==".mp3")codec="libmp3lame";
    else if(ext==".ogg")codec=info.codec=="opus"?"libopus":"libvorbis";
    else if(ext==".opus")codec="libopus";
    else if(ext==".m4a")codec=info.codec=="alac"?"alac":"aac";
    else if(ext==".aac")codec="aac";
    else if(ext==".wma"){if(info.codec=="wmav1"||info.codec=="wmav2")codec=info.codec;else{error="This WMA codec cannot be re-encoded in its original format.";return false;}}
    else {error="This source format cannot be exported.";return false;}
    juce::StringArray args{"-nostdin","-v","error"};
    if(info.video&&!audioOnly){
        // Keep every video/subtitle/additional audio stream. The first audio
        // stream is edited; do not re-encode the video or shorten it to audio.
        args.addArray({"-i",info.file.getFullPathName(),"-i",wave.getFullPathName(),
            "-map","0:v?","-map","1:a:0","-map","0:a?","-map","-0:a:0",
            "-map","0:s?","-map","0:d?","-map","0:t?","-map_metadata","0",
            "-map_chapters","0","-c","copy","-c:a:0",codec,
            "-ar:a:0",juce::String((int)info.rate),"-ac:a:0",juce::String(info.channels),
            "-map_metadata:s:a:0","0:s:a:0"});
    }else args.addArray({"-i",wave.getFullPathName(),"-vn","-c:a",codec,"-ar",juce::String((int)info.rate),"-ac",juce::String(info.channels)});
    if(codec.isEmpty()){error="The source audio codec cannot be exported without changing format.";return false;}
    if(ext==".m4v")args.addArray({"-f","mp4"});
    if(codec=="libmp3lame"||codec=="libvorbis")args.addArray({"-q:a","2"});
    if(codec=="flac")args.addArray({"-sample_fmt",info.bits<=16?"s16":"s32"});
    if(ext==".wave")args.addArray({"-f","wav"});
    args.addArray({"-y",encoded.getFullPathName()});
    if(!codecRun(args,error))return false;
    if(!encoded.existsAsFile()||encoded.getSize()<32||!target.getParentDirectory().createDirectory().wasOk()||!encoded.copyFileTo(target)){error="Cannot save the completed export.";return false;}
    return true;
}
class Session final : private juce::Thread {
public:
    explicit Session(SpectrumTagAudioProcessor& value):juce::Thread("SpectrumTag file IO"),processor(value){startThread();}
    ~Session()override{signalThreadShouldExit();notify();stopThread(-1);}
    bool load(juce::File file){return submit({false,std::move(file),{}});}
    bool exportTo(juce::File file,bool audioOnly=false){return submit({true,std::move(file),current(),audioOnly});}
    bool busy()const{return working.load();}
    FileInfo current()const{std::lock_guard<std::mutex> lock(mutex);return info;}
    juce::String status()const{std::lock_guard<std::mutex> lock(mutex);return message;}
    uint64_t loadedRevision()const{return loaded.load();}
    bool exportable()const{auto s=processor.recording().snapshot();std::lock_guard<std::mutex> lock(mutex);return s&&s->sourceId==sourceId&&info.file.existsAsFile();}
private:
    struct Job{bool exporting=false;juce::File file;FileInfo original;bool audioOnly=false;};
    SpectrumTagAudioProcessor& processor;mutable std::mutex mutex;std::optional<Job> pending;FileInfo info;juce::String message,sourceId;
    std::atomic<bool> working{false};std::atomic<uint64_t> loaded{0};
    bool submit(Job job){if(working.exchange(true))return false;{std::lock_guard<std::mutex> lock(mutex);message=job.exporting?"Exporting...":"Loading...";pending=std::move(job);}notify();return true;}
    void run()override{while(!threadShouldExit()){
        std::optional<Job> job;{std::lock_guard<std::mutex> lock(mutex);job.swap(pending);}if(!job){wait(50);continue;}
        juce::String error;bool ok=false;
        if(job->exporting){const auto snapshot=processor.recording().snapshot();ok=snapshot&&exportOriginal(job->original,*snapshot,job->file,error,job->audioOnly);}
        else {FileInfo next;juce::File decoded;if(inspect(job->file,next,decoded,error)){
            processor.recording().importAudio(decoded.existsAsFile()?decoded:job->file,0);
            ok=processor.recording().flush(120000)&&processor.recording().errorCode()==0;
            if(ok){std::lock_guard<std::mutex> lock(mutex);info=next;sourceId=processor.recording().snapshot()->sourceId;++loaded;}
            else error="Audio loading failed; the previous file is retained.";
        }if(decoded.existsAsFile())decoded.deleteFile();}
        {std::lock_guard<std::mutex> lock(mutex);message=ok?(job->exporting?"Exported: ":"Loaded: ")+job->file.getFileName():error;}
        working=false;
    }}
};
inline float sampleAt(const RecordingSnapshot& audio,int channel,double time,double rate){
    const double at=time*audio.sampleRate;const auto nearest=(int64_t)llround(at);
    if(rate>=audio.sampleRate&&std::abs(at-nearest)<1.e-7)return audio.sample(channel,nearest);
    const double cutoff=juce::jmin(1.,rate/audio.sampleRate),pi=juce::MathConstants<double>::pi;constexpr int radius=16;const auto centre=(int64_t)std::floor(at);double sum=0,weight=0;
    for(int k=-radius+1;k<=radius;++k){const auto n=centre+k;const double d=at-n;if(std::abs(d)>=radius)continue;const double x=pi*d*cutoff;const double w=cutoff*(std::abs(x)<1.e-10?1:std::sin(x)/x)*(.5+.5*std::cos(pi*d/radius));weight+=w;sum+=w*audio.sample(channel,n);}return weight?(float)(sum/weight):0;
}
}
