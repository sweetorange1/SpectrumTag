#if defined(__APPLE__)
// Include legacy Apple Point declarations before JUCE's generated namespace
// import, otherwise MacTypes.h and juce::Point become ambiguous in the harness.
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "shared/PlatformFonts.h"
#include <ARA_Library/Dispatch/ARAHostDispatch.h>
#include <ARA_Library/Dispatch/ARAPlugInDispatch.h>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include "Revision1416Checks.h"
#include "RenderReplacementChecks.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory();
using namespace spectrumtag;
using namespace spectrumtag::ara;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void pump(){
#if JUCE_WINDOWS
    MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
#elif JUCE_MAC
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,.001,true);
#endif
}
struct Audio : ARA::Host::AudioAccessControllerInterface {
    struct Clip{double rate=48000;float gain=.1f;bool fail=false;};
    struct Handle{Clip* clip;bool wide;};
    std::vector<Clip> clips=std::vector<Clip>(48);
    std::thread::id model=std::this_thread::get_id();std::atomic<int> reads{0},badThread{0};
    float value(const Clip& c,int64_t n)const{return c.gain*(float)std::sin(juce::MathConstants<double>::twoPi*700*n/c.rate);}
    ARA::ARAAudioReaderHostRef createAudioReaderForSource(ARA::ARAAudioSourceHostRef r,bool wide)noexcept override{
        if(std::this_thread::get_id()!=model)++badThread;return reinterpret_cast<ARA::ARAAudioReaderHostRef>(new Handle{reinterpret_cast<Clip*>(r),wide});
    }
    bool readAudioSamples(ARA::ARAAudioReaderHostRef ref,ARA::ARASamplePosition first,ARA::ARASampleCount n,void*const channels[])noexcept override{
        if(std::this_thread::get_id()!=model)++badThread;++reads;auto& h=*reinterpret_cast<Handle*>(ref);if(h.clip->fail)return false;
        for(int64_t i=0;i<n;++i){const auto v=value(*h.clip,first+i);if(h.wide)static_cast<double*>(channels[0])[i]=v;else static_cast<float*>(channels[0])[i]=v;}return true;
    }
    void destroyAudioReader(ARA::ARAAudioReaderHostRef r)noexcept override{if(std::this_thread::get_id()!=model)++badThread;delete reinterpret_cast<Handle*>(r);}
};
struct Archives : ARA::Host::ArchivingControllerInterface {
    std::vector<ARA::ARAByte> data;const char* identity=nullptr;
    ARA::ARASize getArchiveSize(ARA::ARAArchiveReaderHostRef)noexcept override{return data.size();}
    bool readBytesFromArchive(ARA::ARAArchiveReaderHostRef,ARA::ARASize at,ARA::ARASize n,ARA::ARAByte* p)noexcept override{if(at>data.size()||n>data.size()-at)return false;std::copy_n(data.data()+at,n,p);return true;}
    bool writeBytesToArchive(ARA::ARAArchiveWriterHostRef,ARA::ARASize at,ARA::ARASize n,const ARA::ARAByte* p)noexcept override{if(at+n>data.size())data.resize(at+n);std::copy_n(p,n,data.data()+at);return true;}
    void notifyDocumentArchivingProgress(float)noexcept override{}
    void notifyDocumentUnarchivingProgress(float)noexcept override{}
    ARA::ARAPersistentID getDocumentArchiveID(ARA::ARAArchiveReaderHostRef)noexcept override{return identity;}
};
struct PlaybackRequests : ARA::Host::PlaybackControllerInterface {
    double position=-1;int seeks=0;
    void requestStartPlayback()noexcept override{}
    void requestStopPlayback()noexcept override{}
    void requestSetPlaybackPosition(double p)noexcept override{position=p;++seeks;}
    void requestSetCycleRange(double,double)noexcept override{}
    void requestEnableCycle(bool)noexcept override{}
};
struct Updates : ARA::Host::ModelUpdateControllerInterface {
    int modifications=0;
    void notifyAudioSourceAnalysisProgress(ARA::ARAAudioSourceHostRef,ARA::ARAAnalysisProgressState,float)noexcept override{}
    void notifyAudioSourceContentChanged(ARA::ARAAudioSourceHostRef,const ARA::ARAContentTimeRange*,ARA::ContentUpdateScopes)noexcept override{}
    void notifyAudioModificationContentChanged(ARA::ARAAudioModificationHostRef,const ARA::ARAContentTimeRange*,ARA::ContentUpdateScopes)noexcept override{++modifications;}
    void notifyPlaybackRegionContentChanged(ARA::ARAPlaybackRegionHostRef,const ARA::ARAContentTimeRange*,ARA::ContentUpdateScopes)noexcept override{}
    void notifyDocumentDataChanged()noexcept override{}
};
struct Head:juce::AudioPlayHead {PositionInfo position;juce::Optional<PositionInfo> getPosition()const override{return position;}};
juce::Component* component(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* c:root.getChildren())if(auto* found=component(*c,id))return found;return nullptr;}
// A host supplies onSize through the editor constrainer; a child resize is only a request.
// Exercise acknowledgement, rejection and delayed acknowledgement without opening a DAW.
struct EditorFrame : juce::Component {
    enum class Reply { accept, reject, delayed } reply=Reply::reject;
    juce::AudioProcessorEditor& editor;
    bool delivering=false;
    int requests=0;
    juce::Rectangle<int> pending;
    explicit EditorFrame(juce::AudioProcessorEditor& e):editor(e){setSize(1600,760);addAndMakeVisible(editor);}
    ~EditorFrame()override{removeChildComponent(&editor);}
    void deliver(int w,int h){
        const juce::ScopedValueSetter<bool> guard(delivering,true);
        setSize(w,h);
        editor.setBoundsConstrained(editor.getLocalArea(this,getLocalBounds()).withZeroOrigin());
    }
    void childBoundsChanged(juce::Component*)override{
        if(delivering)return;
        ++requests;pending=getLocalArea(&editor,editor.getLocalBounds()).withZeroOrigin();
        if(reply==Reply::accept)deliver(pending.getWidth(),pending.getHeight());
    }
    bool matches()const{return getLocalArea(&editor,editor.getLocalBounds()).withZeroOrigin()==getLocalBounds();}
};
bool containsResizeHandle(juce::Component& c){
    if(dynamic_cast<juce::ResizableCornerComponent*>(&c))return true;
    for(auto* child:c.getChildren())if(containsResizeHandle(*child))return true;
    return false;
}
void checkEditorHostSizing(juce::AudioProcessorEditor& editor){
    auto pumpFor=[](int ms){const auto until=juce::Time::getMillisecondCounter()+ms;do{pump();juce::Thread::sleep(3);}while((int)(until-juce::Time::getMillisecondCounter())>0);};
    EditorFrame host(editor);
    host.deliver(1600,760);
    check(host.matches(),"ARA editor must accept the exact non-proportional host rectangle");
    check(!containsResizeHandle(editor),"ARA must not contain an independent corner resizer");
    auto* print=component(editor,"printButton");auto* canvas=print->getParentComponent();
    auto canvasFits=[&]{auto b=editor.getLocalArea(canvas,canvas->getLocalBounds());return std::abs(b.getRight()-editor.getWidth())<=1&&std::abs(b.getBottom()-editor.getHeight())<=1;};
    check(canvasFits(),"ARA canvas must fill both axes of the host rectangle");
    check(component(editor,"uiScale")==nullptr,"ARA must not offer the VST3 zoom menu");
    host.deliver(930,510);pumpFor(100);check(host.matches()&&canvasFits(),"manual small host size fills panel");
    host.deliver(1700,900);pumpFor(100);check(host.matches()&&canvasFits(),"manual large host size fills panel");
    editor.setScaleFactor(1.25f);host.deliver(1700,1000);pumpFor(100);
    check(host.matches()&&canvasFits(),"host DPI must be applied once and still fill the host");
    const auto current=editor.getBounds();
    // Reload/selection restoration must not resize an already attached editor to saved UI scale.
    auto& processor=static_cast<SpectrumTagAudioProcessor&>(editor.processor);
    auto state=processor.getEditorState();state.uiScale=.75f;processor.setEditorState(state);
    processor.pollARA();pumpFor(50);
    check(editor.getBounds()==current,"selection polling must retain the host allocation");
    host.removeChildComponent(&editor);editor.setScaleFactor(1);editor.setSize(1240,680);
    std::cout<<"PASS ARA manual host geometry, DPI, no zoom menu or inner handle\n"<<std::flush;
}
void waitFor(Controller& owner,const std::function<bool()>& condition,const char* label){const auto end=juce::Time::getMillisecondCounter()+20000;do{pump();owner.poll();if(condition())return;juce::Thread::sleep(3);}while((int)(end-juce::Time::getMillisecondCounter())>0);throw std::runtime_error(label);}
}
#include "ARAReplacementChecks.h"
int main(int argc,char** argv){try{
    juce::ScopedJuceInitialiser_GUI gui;
    check(juce::Font::findAllTypefaceNames().contains(spectrumtag::ui::chineseFontName(),true),"Chinese UI font must resolve to an installed family");
    PrintEdit crop;crop.low=0;crop.high=1;crop.rows=5;crop.cols=1;crop.mask=std::make_shared<const std::vector<float>>(std::initializer_list<float>{0,.25f,.5f,.75f,1});
    check(convertFrequencyRange(crop,96000,48000)&&crop.high==1&&std::abs(crop.mask->back()-.5f)<1.e-6f,"Nyquist cropping must preserve physical mask frequencies");
    std::cout<<"PASS platform font resolution and frequency-domain cropping\n"<<std::flush;
    const auto* factory=createARAFactory();
    ARA::ARAInterfaceConfiguration config{};config.structSize=sizeof(config);config.desiredApiGeneration=ARA::kARAAPIGeneration_2_0_Final;
    factory->initializeARAWithConfiguration(&config);
    Audio audio;Archives archive;PlaybackRequests transport;Updates updates;archive.identity=factory->documentArchiveID;
    ARA::Host::DocumentControllerHostInstance host(&audio,&archive,nullptr,&updates,&transport);
    ARA::ARADocumentProperties properties{};properties.structSize=sizeof(properties);properties.name="SpectrumTag ARA regression";
    const auto* instance=factory->createDocumentControllerWithDocument(&host,&properties);
    auto* dc=static_cast<ARA::PlugIn::DocumentController*>(ARA::PlugIn::fromRef(instance->documentControllerRef));
    auto* owner=juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<Controller>(dc);
    std::vector<ARA::ARAAudioSourceRef> sources;std::vector<ARA::ARAAudioModificationRef> modifications;std::vector<ARA::ARAPlaybackRegionRef> regions;
    std::vector<ARA::ARAPlaybackRegionProperties> regionProperties;
    dc->beginEditing();
    ARA::ARAMusicalContextProperties contextProperties{};contextProperties.structSize=sizeof(contextProperties);contextProperties.name="Timeline";
    auto context=dc->createMusicalContext(reinterpret_cast<ARA::ARAMusicalContextHostRef>(&audio),&contextProperties);
    ARA::ARARegionSequenceProperties sequenceProperties{};sequenceProperties.structSize=sizeof(sequenceProperties);sequenceProperties.name="Track";sequenceProperties.musicalContextRef=context;
    std::vector<ARA::ARARegionSequenceRef> sequences;for(int i=0;i<22;++i)sequences.push_back(dc->createRegionSequence(reinterpret_cast<ARA::ARARegionSequenceHostRef>(&audio.clips[i]),&sequenceProperties));
    for(int i=0;i<44;++i){
        auto& clip=audio.clips[i];clip.rate=i%3==0?44100:i%3==1?48000:96000;clip.gain=.1f+i*.002f;
        const auto id=juce::String("source-")+juce::String(i);
        ARA::ARAAudioSourceProperties source{};source.structSize=sizeof(source);source.name=id.toRawUTF8();source.persistentID=id.toRawUTF8();source.sampleRate=clip.rate;source.sampleCount=(int64_t)llround(.25*clip.rate);source.channelCount=1;
        auto src=dc->createAudioSource(reinterpret_cast<ARA::ARAAudioSourceHostRef>(&clip),&source);sources.push_back(src);
        ARA::ARAAudioModificationProperties modification{};modification.structSize=sizeof(modification);modification.name=id.toRawUTF8();modification.persistentID=id.toRawUTF8();
        auto mod=dc->createAudioModification(src,reinterpret_cast<ARA::ARAAudioModificationHostRef>(&clip),&modification);modifications.push_back(mod);
        ARA::ARAPlaybackRegionProperties r{};r.structSize=sizeof(r);r.regionSequenceRef=sequences[i/2];r.startInPlaybackTime=10+i*.3;r.startInModificationTime=.05;r.durationInModificationTime=.12;r.durationInPlaybackTime=.12;
        regions.push_back(dc->createPlaybackRegion(mod,reinterpret_cast<ARA::ARAPlaybackRegionHostRef>(&clip),&r));regionProperties.push_back(r);
        dc->enableAudioSourceSamplesAccess(src,true);
    }
    dc->endEditing();
    {
        std::vector<std::unique_ptr<SpectrumTagAudioProcessor>> plugins;
        for(int i=0;i<22;++i){auto p=std::make_unique<SpectrumTagAudioProcessor>();const auto roles=ARA::kARAPlaybackRendererRole|(i==0?ARA::kARAEditorViewRole:0);
            p->bindToARA(instance->documentControllerRef,roles,roles);auto* renderer=p->getPlaybackRenderer();renderer->addPlaybackRegion(regions[i*2]);renderer->addPlaybackRegion(regions[i*2+1]);
            p->setRateAndBufferSizeDetails(48000,256);p->prepareToPlay(48000,256);plugins.push_back(std::move(p));
        }
        const int priorReads=audio.reads.load();int audible=0;
        for(int i=0;i<44;++i){auto& p=*plugins[i/2];Head head;head.position.setTimeInSeconds(regionProperties[i].startInPlaybackTime+.03);head.position.setIsPlaying(false);p.setPlayHead(&head);p.setNonRealtime(true);
            juce::AudioBuffer<float> b(2,256);juce::MidiBuffer midi;p.processBlock(b,midi);
            const double t=.08;float error=0;for(int n=0;n<256;++n){const float expected=audio.clips[i].gain*(float)std::sin(juce::MathConstants<double>::twoPi*700*(t+n/48000.));error=juce::jmax(error,std::abs(b.getSample(0,n)-expected));}
            check(error<.0002f,"source offset/sample-rate/renderer mapping");if(b.getRMSLevel(0,0,256)>.02f)++audible;p.setPlayHead(nullptr);p.setNonRealtime(false);
        }
        check(audible==44,"44 sources must remain audible");check(audio.reads==priorReads&&audio.badThread==0,"no host reads from processing/DSP workers");
        std::cout<<"PASS 44 sources / 22 renderers, 44.1/48/96 kHz, source offsets, offline not-playing\n"<<std::flush;
        auto& p=*plugins[0];auto* view=p.getEditorView<View>();
        auto parameter=[&](const juce::String& id,float value){auto* a=p.getAPVTS().getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(value));};
        parameter(ParameterIDs::frequencyPreset,0);parameter(ParameterIDs::fftScale,0);
        juce::MemoryBlock wrapperLinear;p.getStateInformation(wrapperLinear);
        p.setStateInformation(wrapperLinear.getData(),(int)wrapperLinear.getSize());
        std::cout<<"TRACE create editor\n"<<std::flush; auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());std::cout<<"TRACE editor created\n"<<std::flush;
        editor->setBoundsConstrained({0,0,1600,760});
        check(editor->getWidth()==1600&&editor->getHeight()==760,"first host allocation must be accepted without aspect-constrained inner resizing");
        editor->setSize(1240,680);
        ARA::ARAViewSelection selection{};selection.structSize=sizeof(selection);selection.playbackRegionRefsCount=1;selection.playbackRegionRefs=regions.data();view->notifySelection(&selection);p.loadARA();
        waitFor(*owner,[&]{auto s=p.recording().snapshot();return s&&!p.recording().isBusy();},"initial overview load timed out");
        p.pollARA();check(p.getFrequencyScale()==4,"1417 fresh ARA track must use Mel despite wrapper Linear state");
        auto* scaleChoice=dynamic_cast<juce::ComboBox*>(component(*editor,"fftScale"));
        p.setStateInformation(wrapperLinear.getData(),(int)wrapperLinear.getSize());p.pollARA();pump();
        check(p.getFrequencyScale()==4,"1417 late wrapper restore must not override a fresh Mel default");
        scaleChoice->setSelectedId(2,juce::sendNotificationSync);check(p.getFrequencyScale()==1,"1417 intentional Log choice");
        p.setStateInformation(wrapperLinear.getData(),(int)wrapperLinear.getSize());p.pollARA();pump();
        check(p.getFrequencyScale()==1,"1417 intentional ARA choice survives wrapper replacement");
        check(dc->storeObjectsToArchive(reinterpret_cast<ARA::ARAArchiveWriterHostRef>(&archive),nullptr),"1417 store scale choice");
        const auto savedChoices=archive.data;
        scaleChoice->setSelectedId(5,juce::sendNotificationSync);
        dc->beginEditing();check(dc->restoreObjectsFromArchive(reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&archive),nullptr),"1417 restore scale choice");dc->endEditing();p.pollARA();pump();
        check(p.getFrequencyScale()==1,"1417 ARA archive restores chosen frequency scale");
        // Legacy archives do not have the new field; their saved wrapper choice remains valid.
        auto root=juce::ValueTree::readFromData(archive.data.data()+8,archive.data.size()-8);
        for(auto child:root)child.removeProperty("viewFrequencyScale",nullptr);
        juce::MemoryOutputStream body;root.writeToStream(body);juce::MemoryOutputStream legacy;legacy.writeInt(0x53504131);legacy.writeInt((int)body.getDataSize());legacy.write(body.getData(),body.getDataSize());
        const auto* bytes=static_cast<const ARA::ARAByte*>(legacy.getData());archive.data.assign(bytes,bytes+legacy.getDataSize());
        p.setStateInformation(wrapperLinear.getData(),(int)wrapperLinear.getSize());
        dc->beginEditing();check(dc->restoreObjectsFromArchive(reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&archive),nullptr),"1417 legacy archive restore");dc->endEditing();p.pollARA();pump();
        check(p.getFrequencyScale()==0,"1417 legacy archive preserves wrapper scale");
        archive.data=savedChoices;
        dc->beginEditing();check(dc->restoreObjectsFromArchive(reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&archive),nullptr),"1417 restore modern archive");dc->endEditing();p.pollARA();pump();
        scaleChoice->setSelectedId(5,juce::sendNotificationSync);
        waitFor(*owner,[&]{return p.recording().snapshot()&&!p.recording().isBusy();},"1417 default checks settle");
        const auto sameTrack=p.recording().snapshot();const auto trackRevision=view->selectionRevision;const int readsBefore=audio.reads;
        for(int i=0;i<6;++i){selection.playbackRegionRefs=&regions[i%2];view->notifySelection(&selection);p.pollARA();pump();check(view->selected.size()==2,"1417 one selected event includes both track events");}
        check(p.recording().snapshot()==sameTrack&&!p.recording().isBusy()&&view->selectionRevision==trackRevision&&audio.reads==readsBefore,"1417 same-track switches reuse snapshot without host reads or recomposition");
        // Host edits may add/remove/move events in the already loaded track.
        regionProperties[2].regionSequenceRef=sequences[0];dc->beginEditing();dc->updatePlaybackRegionProperties(regions[2],&regionProperties[2]);dc->endEditing();
        waitFor(*owner,[&]{return view->selected.size()==3&&p.recording().snapshot()&&!p.recording().isBusy();},"1417 event moved into track must appear");
        regionProperties[2].regionSequenceRef=sequences[1];dc->beginEditing();dc->updatePlaybackRegionProperties(regions[2],&regionProperties[2]);dc->endEditing();
        waitFor(*owner,[&]{return view->selected.size()==2&&p.recording().snapshot()&&!p.recording().isBusy();},"1417 event moved out must leave view without following another track");
        selection.playbackRegionRefs=regions.data();
        std::cout<<"PASS 1.4.17 ARA Mel initialization, intentional/archived/legacy scale recall, same-track cache reuse, track membership updates\n"<<std::flush;
        std::cout<<"TRACE loaded\n"<<std::flush;auto loaded=p.recording().snapshot();check(std::abs(loaded->start()-10)<1.e-6&&std::abs(loaded->end()-10.42)<1.e-6,"events keep absolute timeline positions");check(!loaded->contains(llround(10.2*loaded->sampleRate)),"gap remains a gap");
        std::cout<<"TRACE controls\n"<<std::flush;check(component(*editor,"loadButton")!=nullptr&&component(*editor,"recordButton")==nullptr,"ARA has Load, no Record");check(component(*editor,"clearLoad")&&component(*editor,"clearLoad")->isVisible()&&!component(*editor,"clearRecord"),"ARA has Clear instead of Clear Record");
        p.auditionAt(10.23);check(transport.seeks==1&&transport.position==10.23,"click must reach ARA host playback controller even in a gap");check(!p.audition().isLocal(),"ARA must not use VST3 local offsets");
        const auto paintDeadline=juce::Time::getMillisecondCounter()+300;
        while((int)(paintDeadline-juce::Time::getMillisecondCounter())>0){pump();owner->poll();juce::Thread::sleep(3);}
        waitFor(*owner,[&]{return (juce::int64)component(*editor,"spectrumView")->getProperties()["rasterApplied"]>0;},"loaded spectrum raster was not adopted");
        const auto* frame=p.recording().snapshot()->frameAt(10.06);
        int peak=0;if(frame)for(auto level:frame->level)peak=juce::jmax(peak,(int)level);
        auto* display=component(*editor,"spectrumView");
        std::cout<<"DISPLAY bins="<<(frame?frame->bins:0)<<" peak="<<peak<<" raster="<<display->getProperties()["rasterApplied"].toString()<<" source="<<p.recording().snapshot()->sourceId<<"\n"<<std::flush;
        check(peak>80,"loaded spectrum must contain source energy");
        spectrumtag::ViewRenderer raster;
        spectrumtag::ViewRenderer::Request request;request.source=p.recording().snapshot();request.width=830;request.height=468;request.start=10;request.end=10.42;request.scale=4;request.top=20000;raster.request(request);
        waitFor(*owner,[&]{return raster.result()!=nullptr;},"direct spectrum render");
        int lit=0;for(int y=0;y<468;++y)for(int x=0;x<830;++x)if(raster.result()->image.getPixelAt(x,y).getBrightness()>.1f)++lit;
        auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());int uiLit=0;for(int y=430;y<550;++y)for(int x=100;x<320;++x)if(snapshot.getPixelAt(x,y).getBrightness()>.1f)++uiLit;
        std::cout<<"RASTER directLit="<<lit<<" uiLit="<<uiLit<<" start="<<display->getProperties()["displayStart"].toString()<<" end="<<display->getProperties()["displayEnd"].toString()<<"\n"<<std::flush;
        check(lit>100,"direct raster must display frequency energy");
        check(uiLit>20,"UI must display loaded frequency energy");
        if(argc>1){auto file=juce::File(juce::String::fromUTF8(argv[1]));file.getParentDirectory().createDirectory();auto stream=file.createOutputStream();if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*stream);}}
        checkEditorHostSizing(*editor);
        // Host embedding may supply delayed bounds that do not match our requested aspect.
        editor->setSize(930,680);editor->setScaleFactor(1.25f);
        const auto printBounds=editor->getLocalArea(component(*editor,"printButton"),component(*editor,"printButton")->getLocalBounds());
        check(printBounds.getRight()<=editor->getWidth()&&printBounds.getBottom()<=editor->getHeight(),"embedded host rectangle must not clip the right-side controls");
        editor->setScaleFactor(1);editor->setSize(1240,680);
        auto* language=dynamic_cast<juce::Button*>(component(*editor,"tooltipLanguage"));check(language!=nullptr,"language switch exists");
        language->onClick();check(dynamic_cast<juce::Button*>(component(*editor,"loadButton"))->getButtonText()=="Load audio","English ARA labels");language->onClick();
        std::cout<<"PASS Load UI, bilingual controls, host bounds/scale, multi-event overview, empty gap, host seek request\n"<<std::flush;
        PrintEdit edit;edit.start=10.02;edit.duration=.075;edit.low=0;edit.high=.9f;edit.rows=129;edit.cols=32;edit.fftSize=1024;edit.ratio=0;edit.compensation=.4f;edit.mask=std::make_shared<const std::vector<float>>(edit.rows*edit.cols,1.f);
        auto before=p.captureUserState();p.recording().add(edit);auto after=p.captureUserState();p.commitUserChange(before,after);
        // Closing the editor must not stop document-owned rendering.
        editor.reset();waitFor(*owner,[&]{return !p.recording().isBusy()&&p.recording().snapshot()&&!p.recording().snapshot()->edits.empty();},"Print did not finish after closing editor");
        auto* mod=static_cast<Modification*>(ARA::PlugIn::fromRef(modifications[0]));auto rendered=mod->runtime->audio();
        check(rendered&&rendered->edits.size()==1,"Print committed to original modification");check(std::abs(mod->edits.front().start-.07)<1.e-8,"Print project-to-source conversion");
        auto checksum=[](const RecordingSnapshot& s){double total=0;for(int64_t i=s.first;i<s.last;++i)total+=std::abs(s.sample(0,i));return total;};
        const double printed=checksum(*rendered),clean=checksum(*mod->getAudioSource<Source>()->raw);check(printed<clean*.85,"Print must alter the actual audio");
        p.recording().add(edit,edit.id);waitFor(*owner,[&]{return !p.recording().isBusy();},"repeat Print");check(std::abs(checksum(*mod->runtime->audio())-printed)<1.e-6,"repeat Print must not accumulate");
        check(p.undoUserChange(),"Print undo exists");waitFor(*owner,[&]{return !p.recording().isBusy();},"Undo Print");check(mod->edits.empty(),"Undo removes Print");check(std::abs(checksum(*mod->runtime->audio())-clean)<1.e-6,"Undo returns original samples");
        check(p.undoUserChange(true),"Print redo exists");waitFor(*owner,[&]{return !p.recording().isBusy();},"Redo Print");
        std::cout<<"PASS Print, repeat replacement, document ownership after close, Undo/Redo\n"<<std::flush;
        {
            auto clearEditor=std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());
            auto* clear=dynamic_cast<juce::Button*>(component(*clearEditor,"clearLoad"));check(clear!=nullptr,"Clear button exists");
            auto* unrelated=static_cast<Modification*>(ARA::PlugIn::fromRef(modifications[2]));
            auto untouched=edit;untouched.id="unrelated-track";untouched.start=.03;unrelated->edits={untouched};owner->sync(*unrelated,true);
            clear->onClick();
            waitFor(*owner,[&]{return !view->loaded&&!p.recording().snapshot()&&!mod->runtime->engine->isBusy();},"Clear did not empty the loaded view");
            check(mod->edits.empty()&&unrelated->edits.size()==1,"Clear removes loaded Prints and leaves unrelated event edits intact");
            check(mod->runtime->audio()->original==mod->runtime->audio()->rendered,"Clear restores original source including Noise Fill removal");
            view->notifySelection(&selection);pump();p.pollARA();check(!view->loaded&&view->manualLoadOnly&&!p.recording().snapshot(),"host selection must not auto-reload after Clear");
            check(p.undoUserChange(),"Clear undo");waitFor(*owner,[&]{return view->loaded&&!p.recording().isBusy()&&p.recording().snapshot();},"Clear undo reload");check(mod->edits.size()==1,"Clear undo restores all Print settings");
            check(p.undoUserChange(true),"Clear redo");waitFor(*owner,[&]{return !p.recording().snapshot();},"Clear redo empty");
            p.loadARA();waitFor(*owner,[&]{return view->loaded&&p.recording().snapshot()&&!p.recording().isBusy();},"fresh Load after Clear");check(mod->edits.empty()&&p.recording().snapshot()->edits.empty(),"Load starts clean after Clear");
            check(p.undoUserChange(),"restore pre-Clear edits for remaining archive tests");waitFor(*owner,[&]{return !p.recording().isBusy()&&p.recording().snapshot();},"restore after clear test");
            unrelated->edits.clear();owner->sync(*unrelated,true);
        }
        std::cout<<"PASS Clear, explicit reload gate, original audio, unrelated edits, Undo/Redo\n"<<std::flush;
        check(dc->storeObjectsToArchive(reinterpret_cast<ARA::ARAArchiveWriterHostRef>(&archive),nullptr),"ARA archive store");auto saved=archive.data;
        p.recording().remove(edit.id);waitFor(*owner,[&]{return !p.recording().isBusy();},"Delete Print");check(mod->edits.empty(),"Delete restores original");
        dc->notifyModelUpdates();const int notifications=updates.modifications;
        const auto cachedEngine=std::atomic_load(&mod->runtime->engine);const auto printPassesBeforeRestore=cachedEngine->printPasses();const auto hitsBeforeRestore=cachedEngine->cacheHits();
        dc->beginEditing();check(dc->restoreObjectsFromArchive(reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&archive),nullptr),"ARA archive restore");dc->endEditing();
        waitFor(*owner,[&]{return !p.recording().isBusy();},"archive rerender");dc->notifyModelUpdates();check(updates.modifications==notifications,"archive restore must not echo host edit notifications");check(mod->edits.size()==1,"archive restores Print mask/settings");
        check(mod->edits[0].fillVersion==1&&cachedEngine->printPasses()==printPassesBeforeRestore&&cachedEngine->cacheHits()>hitsBeforeRestore,"1421 ARA archive restores versioned Print from persistent result cache");
        std::cout<<"PASS archive restoration, no host notification echo, Delete\n"<<std::flush;
        // Filtered archives must remap persistent modification IDs without discarding other caches.
        const auto untouched=static_cast<Source*>(ARA::PlugIn::fromRef(sources[1]))->raw;
        dc->beginEditing();ARA::ARAAudioModificationProperties remappedProperties{};remappedProperties.structSize=sizeof(remappedProperties);remappedProperties.name="Copy";remappedProperties.persistentID="remapped-copy";
        auto copy=dc->createAudioModification(sources[0],reinterpret_cast<ARA::ARAAudioModificationHostRef>(&archive),&remappedProperties);
        const ARA::ARAPersistentID archivedIDs[]={"source-0"},currentIDs[]={"remapped-copy"};
        ARA::ARARestoreObjectsFilter filter{};filter.structSize=sizeof(filter);filter.audioModificationIDsCount=1;filter.audioModificationArchiveIDs=archivedIDs;filter.audioModificationCurrentIDs=currentIDs;
        check(dc->restoreObjectsFromArchive(reinterpret_cast<ARA::ARAArchiveReaderHostRef>(&archive),&filter),"filtered archive ID remap");dc->endEditing();
        auto* copied=static_cast<Modification*>(ARA::PlugIn::fromRef(copy));check(copied->edits.size()==1&&copied->edits[0].id==edit.id,"archive remaps persistent modification ID");check(static_cast<Source*>(ARA::PlugIn::fromRef(sources[1]))->raw==untouched,"filtered restore preserves unrelated raw audio");
        dc->beginEditing();dc->destroyAudioModification(copy);dc->endEditing();
        std::cout<<"PASS filtered archive ID remap and untouched cache preservation\n"<<std::flush;
        // One image crosses two independent events and a gap: Delete removes both source edits.
        p.recording().setEdits({});waitFor(*owner,[&]{return !p.recording().isBusy();},"clear test edit");
        auto across=edit;across.id=juce::Uuid().toString();across.start=10.01;across.duration=.4;
        p.recording().add(across);waitFor(*owner,[&]{return !p.recording().isBusy();},"cross-event Print");
        auto* secondMod=static_cast<Modification*>(ARA::PlugIn::fromRef(modifications[1]));check(mod->edits.size()==1&&secondMod->edits.size()==1,"Print spans both events");
        p.recording().remove(across.id);waitFor(*owner,[&]{return !p.recording().isBusy();},"cross-event Delete");check(mod->edits.empty()&&secondMod->edits.empty(),"Delete removes every piece and compensation");
        // Move the event; its source-relative edits move with it, preserving source offset.
        p.recording().add(edit);waitFor(*owner,[&]{return !p.recording().isBusy();},"Print before movement");
        regionProperties[0].startInPlaybackTime=8;dc->beginEditing();dc->updatePlaybackRegionProperties(regions[0],&regionProperties[0]);dc->endEditing();
        waitFor(*owner,[&]{return !p.recording().isBusy()&&p.recording().snapshot()->start()<8.1;},"event movement");
        check(std::abs(p.recording().edits()->front().start-8.02)<1.e-7,"Print follows DAW event move");
        auto* view2=plugins[1]->getEditorView<View>();juce::ignoreUnused(view2);
        view->setEditorOpen(true);
        selection.playbackRegionRefsCount=1;selection.playbackRegionRefs=&regions[2];view->notifySelection(&selection);
        waitFor(*owner,[&]{return !p.recording().isBusy()&&p.recording().snapshot()&&std::abs(p.recording().snapshot()->start()-10.6)<1.e-6;},"explicit selection must ignore renderer ownership");
        selection.playbackRegionRefsCount=2;selection.playbackRegionRefs=regions.data();view->notifySelection(&selection);
        waitFor(*owner,[&]{return !p.recording().isBusy();},"return selection");view->setEditorOpen(false);
        std::cout<<"PASS cross-event Print/Delete, event movement, explicit editor selection across renderers\n"<<std::flush;
        // Failed reads never install half a source; later Load retries on the model thread.
        dc->beginEditing();dc->enableAudioSourceSamplesAccess(sources[1],false);audio.clips[1].fail=true;dc->enableAudioSourceSamplesAccess(sources[1],true);dc->endEditing();
        auto* second=static_cast<Source*>(ARA::PlugIn::fromRef(sources[1]));check(!second->raw,"failed source must not publish partial data");
        waitFor(*owner,[&]{return !p.recording().snapshot();},"unavailable selection must clear stale spectrum/audio");
        audio.clips[1].fail=false;view->load();waitFor(*owner,[&]{return second->raw&&p.recording().snapshot()&&!p.recording().isBusy();},"Load retry");
        auto oldRaw=second->raw;audio.clips[1].gain*=.5f;dc->beginEditing();dc->updateAudioSourceContent(sources[1],nullptr,ARA::kARAContentUpdateEverythingChanged);dc->endEditing();check(second->raw&&second->raw!=oldRaw,"same-length source update must invalidate generation");
        std::cout<<"PASS read failure/retry, same-length content replacement\n"<<std::flush;
        waitFor(*owner,[&]{return p.recording().snapshot()&&!p.recording().isBusy();},"source replacement settled");
        revision1416::ui(p,argc>1?juce::File(argv[1]).getParentDirectory():juce::File("D:/Codex/Temp/SpectrumTag-validation"),[&]{pump();owner->poll();},[&]{return transport.seeks;});
        for(auto& plugin:plugins)plugin->releaseResources();
    }
    dc->beginEditing();for(auto r:regions)dc->destroyPlaybackRegion(r);for(auto m:modifications)dc->destroyAudioModification(m);for(auto s:sources)dc->destroyAudioSource(s);for(auto sequence:sequences)dc->destroyRegionSequence(sequence);dc->destroyMusicalContext(context);dc->endEditing();
    dc->destroyDocumentController();factory->uninitializeARA();check(audio.badThread==0,"host audio accessed on worker thread");
    araReplacementChecks();
    std::cout<<"ALL ARA CHECKS PASSED\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
