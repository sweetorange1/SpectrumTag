#pragma once
#include "Recording.h"

namespace spectrumtag {
// Derived data only. Original audio and edit instructions remain authoritative.
// Content keys also work after ARA recreates its objects with new pointer/UUID values.
class RenderCache {
    struct PageHashes {
        std::vector<std::weak_ptr<const AudioPage>> previous;
        std::vector<juce::MemoryBlock> hashes;
        int lastChannels=0;
        void append(juce::OutputStream& out,const std::vector<std::shared_ptr<const AudioPage>>& pages,int channels) {
            if(lastChannels!=channels){previous.clear();hashes.clear();lastChannels=channels;}
            previous.resize(pages.size());hashes.resize(pages.size());out.writeInt((int)pages.size());
            for(size_t i=0;i<pages.size();++i){const auto& page=pages[i];out.writeBool(page!=nullptr);if(!page)continue;
                if(previous[i].lock()!=page||hashes[i].isEmpty()){
                    juce::MemoryOutputStream bytes;
                    for(int n=0;n<recordingPageSize;n+=8){unsigned mask=0;for(int b=0;b<8;++b)if(page->valid[n+b])mask|=1u<<b;bytes.writeByte((char)mask);}
                    for(int c=0;c<channels;++c)bytes.write(page->samples[c].data(),recordingPageSize*sizeof(float));
                    hashes[i]=juce::SHA256(bytes.getData(),bytes.getDataSize()).getRawData();previous[i]=page;
                }
                out.write(hashes[i].getData(),hashes[i].getSize());
            }
        }
    } rawHashes,wetHashes;
public:
    static bool writeFloats(juce::OutputStream& out,const float* values,size_t count){
        if(!juce::ByteOrder::isBigEndian())return out.write(values,count*sizeof(float));
        for(size_t i=0;i<count;++i)if(!out.writeFloat(values[i]))return false;return true;
    }
    static bool readFloats(juce::InputStream& in,float* values,size_t count){
        if(juce::ByteOrder::isBigEndian()){for(size_t i=0;i<count;++i)values[i]=in.readFloat();}
        else if(in.read(values,(int)(count*sizeof(float)))!=(int)(count*sizeof(float)))return false;
        return std::all_of(values,values+count,[](float v){return std::isfinite(v);});
    }
    static juce::File file(const juce::String& key){return Preferences::file().getParentDirectory().getChildFile("RenderCache").getChildFile(key+".sptrender");}
    juce::String key(const RecordingSnapshot& s,bool analysisOnly){
        juce::MemoryOutputStream bytes;bytes.writeString("SpectrumTag-render-1.4.21-v1");bytes.writeBool(analysisOnly);
        bytes.writeDouble(s.sampleRate);bytes.writeInt(s.channels);bytes.writeInt(s.fftSize);bytes.writeInt64(s.origin);bytes.writeInt64(s.first);bytes.writeInt64(s.last);
        if(s.fileSourceLast>0&&(s.last>s.fileSourceLast||s.first<0)){bytes.writeString("standalone-print-extent-v1");bytes.writeInt64(s.fileSourceLast);}
        rawHashes.append(bytes,s.original,s.channels);
        if(analysisOnly)wetHashes.append(bytes,s.rendered,s.channels);
        else for(const auto& e:s.edits){bytes.writeString(e.id);bytes.writeDouble(e.start);bytes.writeDouble(e.duration);bytes.writeFloat(e.low);bytes.writeFloat(e.high);bytes.writeFloat(e.ratio);bytes.writeFloat(e.compensation);bytes.writeBool(e.invert);bytes.writeInt(e.fftSize);bytes.writeInt(e.rows);bytes.writeInt(e.cols);bytes.writeInt(e.fillVersion);for(float v:*e.mask)bytes.writeFloat(v);}
        return juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString();
    }
    bool load(const juce::String& key,RecordingSnapshot& s){
        auto input=file(key).createInputStream();if(!input||input->getTotalLength()<40)return false;
        const auto length=input->getTotalLength()-32;juce::SubregionStream body(input.get(),0,length,false);
        const auto expected=juce::SHA256(body).getRawData();juce::MemoryBlock actual(32);
        input->setPosition(length);if(input->read(actual.getData(),32)!=32||actual!=expected)return false;
        input->setPosition(0);juce::SubregionStream compressed(input.get(),0,length,false);juce::GZIPDecompressorInputStream in(compressed);
        if(in.readInt()!=0x53524331||in.readString()!=key)return false;
        auto result=s;result.rendered.resize(s.original.size());
        for(size_t i=0;i<result.rendered.size();++i){const int tag=(unsigned char)in.readByte();
            if(tag==0)result.rendered[i].reset();else if(tag==1)result.rendered[i]=s.original[i];
            else if(tag==2&&s.original[i]){auto page=std::make_shared<AudioPage>(*s.original[i]);bool same=true;for(int c=0;c<s.channels;++c){if(!readFloats(in,page->samples[c].data(),recordingPageSize))return false;same&=std::memcmp(page->samples[c].data(),s.original[i]->samples[c].data(),recordingPageSize*sizeof(float))==0;}result.rendered[i]=same?s.original[i]:page;}else return false;
        }
        auto peaks=[&](auto& values){const int count=in.readInt();if(count!=0&&count!=(int)s.original.size()*16)return false;values.resize((size_t)count);for(auto& v:values){v.first=in.readFloat();v.second=in.readFloat();if(!std::isfinite(v.first)||!std::isfinite(v.second))return false;}return true;};
        const int tiles=(int)((s.last-s.origin+RecordingSnapshot::tileSize-1)/RecordingSnapshot::tileSize),frames=RecordingSnapshot::tileSize/(s.fftSize/8),bins=s.fftSize/2+1;
        auto readTiles=[&](auto& values,bool original){const int count=in.readInt();if(count!=0&&count!=tiles)return false;values.resize((size_t)count);
            for(int t=0;t<count;++t){const int tag=(unsigned char)in.readByte();if(tag==0){values[t].reset();continue;}if(tag==2&&original&&t<(int)result.spectrum.size()){values[t]=result.spectrum[t];continue;}if(tag!=1)return false;
                auto tile=std::make_shared<RecordingSnapshot::Tile>();tile->frames.resize((size_t)frames);
                for(int f=0;f<frames;++f){auto& frame=tile->frames[f];frame.time=(s.origin+(int64_t)t*RecordingSnapshot::tileSize+f*(s.fftSize/8))/s.sampleRate;frame.sampleRate=s.sampleRate;frame.interval=(s.fftSize/8)/s.sampleRate;frame.bins=bins;if(in.read(frame.level.data(),bins)!=bins)return false;}values[t]=tile;
            }return true;
        };
        if(!peaks(result.peaks)||!peaks(result.originalPeaks)||!readTiles(result.spectrum,false)||!readTiles(result.originalSpectrum,true)||in.readInt()!=0x454e4421)return false;
        if((int)result.spectrum.size()!=tiles||std::any_of(result.spectrum.begin(),result.spectrum.end(),[](const auto& t){return !t;}))return false;
        s=std::move(result);return true;
    }
    bool save(const juce::String& key,const RecordingSnapshot& s,const std::function<bool()>& cancelled){
        const auto target=file(key);
        if(target.getParentDirectory().createDirectory().failed())return false;
        juce::TemporaryFile temporary(target);auto output=temporary.getFile().createOutputStream();if(!output)return false;
        {juce::GZIPCompressorOutputStream out(*output,1);out.writeInt(0x53524331);out.writeString(key);
            for(size_t i=0;i<s.original.size();++i){if(cancelled())return false;const auto page=i<s.rendered.size()?s.rendered[i]:nullptr;
                if(!page)out.writeByte(0);else if(page==s.original[i])out.writeByte(1);else {out.writeByte(2);for(int c=0;c<s.channels;++c)writeFloats(out,page->samples[c].data(),recordingPageSize);}}
            for(const auto* values:{&s.peaks,&s.originalPeaks}){out.writeInt((int)values->size());for(auto v:*values){out.writeFloat(v.first);out.writeFloat(v.second);}}
            for(int original=0;original<2;++original){const auto& tiles=original?s.originalSpectrum:s.spectrum;out.writeInt((int)tiles.size());
                for(size_t t=0;t<tiles.size();++t){if(cancelled())return false;const auto& tile=tiles[t];if(!tile){out.writeByte(0);continue;}if(original&&t<s.spectrum.size()&&tile==s.spectrum[t]){out.writeByte(2);continue;}
                    out.writeByte(1);for(const auto& frame:tile->frames)out.write(frame.level.data(),(size_t)(s.fftSize/2+1));}}
            out.writeInt(0x454e4421);out.flush();}
        output->flush();if(output->getStatus().failed())return false;output.reset();
        const auto digest=juce::SHA256(temporary.getFile()).getRawData();output=temporary.getFile().createOutputStream();if(!output||!output->write(digest.getData(),digest.getSize()))return false;output->flush();if(output->getStatus().failed())return false;output.reset();
        return !cancelled()&&temporary.overwriteTargetFileWithTemporary();
    }
};
}
