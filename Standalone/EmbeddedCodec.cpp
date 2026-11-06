#include "EmbeddedCodec.h"
#include <mutex>
#if JUCE_WINDOWS && SPECTRUMTAG_EMBEDDED_CODEC
#include "EmbeddedCodecConfig.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace spectrumtag::standalone {
juce::File resolveEncoder(juce::String& error){
    const auto override=juce::SystemStats::getEnvironmentVariable("SPECTRUMTAG_FFMPEG",{});
    if(override.isNotEmpty())return juce::File(override);
#if JUCE_WINDOWS && SPECTRUMTAG_EMBEDDED_CODEC
    static std::mutex mutex;static juce::File verified;
    std::lock_guard<std::mutex> guard(mutex);
    if(verified.existsAsFile())return verified;
    const auto cache=juce::SystemStats::getEnvironmentVariable("SPECTRUMTAG_RUNTIME_CACHE","D:/Codex/Temp/SpectrumTag-Standalone/Runtime");
    const auto folder=juce::File(cache).getChildFile(juce::String(embedded::exeHash).substring(0,20));
    juce::InterProcessLock lock("SpectrumTagCodec-"+juce::String(embedded::exeHash).substring(0,20));
    if(!lock.enter(30000)){error="Codec cache is busy. Please try again.";return {};}
    struct Unlock{juce::InterProcessLock& lock;~Unlock(){lock.exit();}} unlock{lock};
    if(auto result=folder.createDirectory();result.failed()){error="Cannot create codec cache: "+folder.getFullPathName()+". "+result.getErrorMessage();return {};}
    const auto module=GetModuleHandleW(nullptr);const auto resource=FindResourceW(module,MAKEINTRESOURCEW(22001),MAKEINTRESOURCEW(10));
    const auto size=resource?SizeofResource(module,resource):0;const auto data=resource?LockResource(LoadResource(module,resource)):nullptr;
    if(!data||size==0){error="The embedded audio codec is missing.";return {};}
    juce::MemoryInputStream stream(data,size,false);juce::ZipFile zip(stream);
    struct Entry{const char* name;const char* hash;};
    for(const auto& entry:{Entry{"ffmpeg.exe",embedded::exeHash},Entry{"COPYING.GPLv3",embedded::licenseHash},Entry{"FFmpeg-NOTICE.txt",embedded::noticeHash}}){
        const auto target=folder.getChildFile(entry.name);
        if(target.existsAsFile()&&juce::SHA256(target).toHexString()==entry.hash)continue;
        const int index=zip.getIndexOfFileName(entry.name);const auto* metadata=zip.getEntry(index);
        std::unique_ptr<juce::InputStream> input(index>=0?zip.createStreamForEntry(index):nullptr);
        if(!metadata||!input){error="The embedded codec archive is incomplete.";return {};}
        // Extract to a unique sibling, verify, then atomically publish it. A
        // second app cannot observe a partially extracted executable.
        juce::TemporaryFile temporary(target);auto output=temporary.getFile().createOutputStream();
        if(!output||output->writeFromInputStream(*input,-1)!=metadata->uncompressedSize){error="Cannot extract codec cache: "+folder.getFullPathName();return {};}
        output->flush();const bool written=output->getStatus().wasOk();output.reset();
        if(!written||juce::SHA256(temporary.getFile()).toHexString()!=entry.hash){error="Embedded codec checksum failed.";return {};}
        if(!temporary.overwriteTargetFileWithTemporary()){error="Cannot update codec cache: "+target.getFullPathName();return {};}
    }
    verified=folder.getChildFile("ffmpeg.exe");return verified;
#else
    // Non-Windows packaging is a separate platform task. Windows development
    // builds without an embedded resource give a clear error, never silently
    // depend on a folder beside a supposedly portable executable.
    error="This standalone build has no embedded codec. Build with SPECTRUMTAG_STANDALONE_RUNTIME_DIR.";return {};
#endif
}
}
