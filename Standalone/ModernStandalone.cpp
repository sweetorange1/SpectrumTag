#include <JuceHeader.h>
#include "../PluginEditor.h"
#include "AudioFileSession.h"
#include "SpectrumVideo.h"
#include "ExportProgress.h"
#include "../shared/PlatformFonts.h"
#include <iostream>
#include "../tests/Revision1416Checks.h"
#include "../tests/SpectrumVideoChecks.h"
using namespace spectrumtag;
using namespace spectrumtag::standalone;
namespace {
class VideoSettings final : public juce::Component {
public:
    VideoSettings(VideoOptions value,std::function<void(VideoOptions)> accepted):options(value),onAccept(std::move(accepted)){
        options.normalise();
        const bool zh=options.chinese;
        formatLabel.setText(juce::String::fromUTF8(zh?"视频格式":"Video format"),juce::dontSendNotification);
        aspectLabel.setText(juce::String::fromUTF8(zh?"画面比例":"Aspect ratio"),juce::dontSendNotification);
        resolutionLabel.setText(juce::String::fromUTF8(zh?"分辨率":"Resolution"),juce::dontSendNotification);
        fpsLabel.setText(juce::String::fromUTF8(zh?"帧率":"Frame rate"),juce::dontSendNotification);
        format.addItemList({"MP4 (H.264)","MOV (H.264)","MKV (H.264)","WebM (VP9)"},1);
        for(int i=0;i<4;++i)aspect.addItem(juce::StringArray{"4:3","3:4","16:9","9:16"}[i]+"  "+juce::String::fromUTF8(i%2?(zh?"竖屏":"Portrait"):(zh?"横屏":"Landscape")),i+1);
        fps.addItemList({"30 fps","60 fps"},1);
        format.setSelectedId(options.format+1,juce::dontSendNotification);aspect.setSelectedId(options.aspect+1,juce::dontSendNotification);fps.setSelectedId(options.fps==60?2:1,juce::dontSendNotification);
        updateResolutions();aspect.onChange=[this]{updateResolutions();};
        aspect.setComponentID("videoAspect");resolution.setComponentID("videoResolution");go.setComponentID("videoDestination");
        for(auto* label:{&formatLabel,&aspectLabel,&resolutionLabel,&fpsLabel}){if(zh)label->setFont(juce::Font(juce::FontOptions(spectrumtag::ui::chineseFontName(),16.f,juce::Font::plain)));addAndMakeVisible(label);}
        for(auto* combo:{&format,&aspect,&resolution,&fps})addAndMakeVisible(combo);
        go.setButtonText(juce::String::fromUTF8(zh?"选择保存位置…":"Choose destination…"));addAndMakeVisible(go);
        go.onClick=[this]{auto next=options;next.format=format.getSelectedId()-1;next.aspect=aspect.getSelectedId()-1;next.resolution=resolution.getSelectedId()-1;next.fps=fps.getSelectedId()==2?60:30;next.remember();onAccept(next);if(auto* window=findParentComponentOfClass<juce::DialogWindow>())window->exitModalState(0);};setSize(440,260);
    }
    void resized()override{auto area=getLocalBounds().reduced(18);for(auto pair:{std::make_pair(&formatLabel,&format),std::make_pair(&aspectLabel,&aspect),std::make_pair(&resolutionLabel,&resolution),std::make_pair(&fpsLabel,&fps)}){auto row=area.removeFromTop(42);pair.first->setBounds(row.removeFromLeft(130));pair.second->setBounds(row.reduced(0,5));}area.removeFromTop(12);go.setBounds(area.removeFromTop(34));}
private:
    void updateResolutions(){
        const int selected=resolution.getSelectedId()>0?resolution.getSelectedId():options.resolution+1;
        auto next=options;next.aspect=aspect.getSelectedId()-1;next.normalise();
        resolution.clear(juce::dontSendNotification);
        for(int i=0;i<4;++i){next.resolution=i;resolution.addItem(juce::String(next.width())+" x "+juce::String(next.height()),i+1);}
        resolution.setSelectedId(selected,juce::dontSendNotification);
    }
    VideoOptions options;std::function<void(VideoOptions)> onAccept;juce::Label formatLabel,aspectLabel,resolutionLabel,fpsLabel;juce::ComboBox format,aspect,resolution,fps;juce::TextButton go;
};
juce::File defaultMediaFolder() {
#if JUCE_WINDOWS
    if (juce::File("D:/").isDirectory()) return juce::File("D:/Codex/Outputs");
#endif
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}
juce::File defaultExportFolder(){return juce::File::getSpecialLocation(juce::File::userDesktopDirectory);}
class Player final : public juce::AudioAppComponent,private juce::Timer {
public:
    SpectrumTagAudioProcessor processor;
    explicit Player(bool enableAudio=true){
        processor.filePlayerMode=true;processor.filePlayerVersion=ProjectInfo::versionString;processor.onLoadAudioFile=[this]{chooseAudio();};processor.onDropAudioFile=[this](const juce::File& f){load(f);};
        processor.onFileSeek=[this](double seconds){const auto s=processor.recording().snapshot();if(s){const double t=juce::jlimit(s->start(),s->end(),seconds);seek=t;processor.publishFileTransport(playing,t);}};
        session=std::make_unique<Session>(processor,enableAudio);editor.reset(processor.createEditor());addAndMakeVisible(*editor);
        for(auto* button:{&play,&stop,&exportButton,&device})addAndMakeVisible(button);addAndMakeVisible(status);
        play.onClick=[this]{if(auto audio=processor.recording().snapshot()){if(position>=audio->end())seek=audio->start();playing=!playing;}};
        stop.onClick=[this]{playing=false;processor.publishFileTransport(false,position);};exportButton.onClick=[this]{chooseExport();};
        device.onClick=[this]{auto* settings=new juce::AudioDeviceSelectorComponent(deviceManager,0,0,1,2,false,false,true,false);settings->setSize(500,400);juce::DialogWindow::LaunchOptions options;options.content.setOwned(settings);options.dialogTitle=processor.getEditorState().chinese?juce::String::fromUTF8("音频设备"):"Audio device";options.dialogBackgroundColour=juce::Colour(0xff1a1c1e);options.useNativeTitleBar=true;options.resizable=false;options.launchAsync();};
        setSize(1240,722);if(enableAudio)setAudioChannels(0,2);startTimerHz(30);
    }
    ~Player()override{stopTimer();movie.cancel();exportProgress.reset();shutdownAudio();editor.reset();session.reset();processor.onFileSeek={};processor.onLoadAudioFile={};processor.onDropAudioFile={};}
    void prepareToPlay(int size,double sr)override{rate=sr;processor.setRateAndBufferSizeDetails(sr,size);processor.prepareToPlay(sr,size);}
    void releaseResources()override{processor.releaseResources();}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output)override{
        output.clearActiveBufferRegion();const auto audio=processor.recording().snapshot();
        const auto request=seek.exchange(std::numeric_limits<double>::quiet_NaN());if(std::isfinite(request))position=request;
        double t=position.load();const bool run=playing.load()&&audio;
        if(run){for(int n=0;n<output.numSamples;++n){if(t>=audio->end()){playing=false;break;}for(int c=0;c<output.buffer->getNumChannels();++c)output.buffer->setSample(c,output.startSample+n,sampleAt(*audio,juce::jmin(c,audio->channels-1),t,rate));t+=1./rate;}position=t;}
        processor.publishFileTransport(playing.load()&&audio!=nullptr,t);
    }
    void resized()override{
        auto bar=getLocalBounds().removeFromTop(42).reduced(8,5);play.setBounds(bar.removeFromLeft(80));bar.removeFromLeft(6);stop.setBounds(bar.removeFromLeft(80));bar.removeFromLeft(6);exportButton.setBounds(bar.removeFromLeft(90));bar.removeFromLeft(6);device.setBounds(bar.removeFromLeft(100));bar.removeFromLeft(12);status.setBounds(bar);if(editor)editor->setBounds(getLocalBounds().withTrimmedTop(42));
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff1a1c1e));}
    bool keyPressed(const juce::KeyPress& key)override{if(movie.busy())return true;if(key==juce::KeyPress::spaceKey){play.onClick();return true;}return editor&&editor->keyPressed(key);}
private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;std::unique_ptr<Session> session;std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton play,stop,exportButton,device;juce::Label status;std::atomic<bool> playing{false};std::atomic<double> position{0},seek{std::numeric_limits<double>::quiet_NaN()};double rate=48000;uint64_t seen=0;
    SpectrumVideoExport movie;SpectrumVideoExport::Stage seenMovieStage=SpectrumVideoExport::idle;juce::File movieTarget;
    std::unique_ptr<ExportProgressWindow> exportProgress;
    void load(const juce::File& file){if(session->busy()||movie.busy())return;playing=false;movieMessage.clear();session->load(file);}
    void chooseAudio(){if(session->busy()||movie.busy())return;chooser=std::make_unique<juce::FileChooser>(processor.getEditorState().chinese?juce::String::fromUTF8("加载音频 / 视频"):"Load audio / video",defaultMediaFolder(),"*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.wma;*.opus;*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<Player>(this)](const juce::FileChooser& c){if(safe&&c.getResult().existsAsFile())safe->load(c.getResult());});}
    void chooseExport(){
        if(movie.busy()){movie.cancel();return;}
        if(session->busy()||processor.recording().isBusy()||!processor.recording().snapshot())return;
        const bool zh=processor.getEditorState().chinese;juce::PopupMenu menu;
        menu.addItem(3,juce::String::fromUTF8(zh?"导出频谱视频…":"Export spectrum video…"));menu.addSeparator();
        const bool video=session->current().video;
        menu.addItem(1,juce::String::fromUTF8(video?(zh?"导出视频（保留画面）":"Export video (keep picture)"):(zh?"导出音频":"Export audio")),session->exportable());
        if(video)menu.addItem(2,juce::String::fromUTF8(zh?"仅导出音频（原音轨格式）":"Export audio only (source codec)"),session->exportable());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportButton),[safe=juce::Component::SafePointer<Player>(this)](int choice){if(safe){if(choice==3)safe->chooseMovieSettings();else if(choice>0)safe->chooseExportFile(choice==2);}});
    }
    void chooseMovieSettings(){
        auto options=VideoOptions::remembered();const auto state=processor.getEditorState();options.palette=state.palette;options.chinese=state.chinese;options.scale=processor.getFrequencyScale();options.speed=processor.getAPVTS().getRawParameterValue(ParameterIDs::speed)->load();const float tops[]{20000,24000,22050,48000};options.top=tops[juce::jlimit(0,3,(int)processor.getAPVTS().getRawParameterValue(ParameterIDs::frequencyRange)->load())];
        juce::DialogWindow::LaunchOptions dialog;dialog.content.setOwned(new VideoSettings(options,[safe=juce::Component::SafePointer<Player>(this)](VideoOptions value){if(safe)safe->chooseMovieFile(value);}));dialog.dialogTitle=juce::String::fromUTF8(options.chinese?"导出频谱视频":"Export spectrum video");dialog.dialogBackgroundColour=juce::Colour(0xff1a1c1e);dialog.useNativeTitleBar=true;dialog.resizable=false;dialog.launchAsync();
    }
    void chooseMovieFile(VideoOptions options){
        auto name=session->current().file.getFileNameWithoutExtension();if(name.isEmpty())name="Untitled";
        chooser=std::make_unique<juce::FileChooser>(juce::String::fromUTF8(options.chinese?"保存频谱视频":"Save spectrum video"),defaultExportFolder().getNonexistentChildFile(name+"_Spectrum",options.extension()),"*"+options.extension());
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<Player>(this),options](const juce::FileChooser& c){if(!safe||c.getResult()==juce::File{})return;auto file=c.getResult();if(file.getFileExtension().toLowerCase()!=options.extension()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SpectrumTag",juce::String::fromUTF8(options.chinese?"请使用所选视频格式的扩展名：":"Please use the selected video extension: ")+options.extension());return;}if(file==safe->session->current().file){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SpectrumTag",juce::String::fromUTF8(options.chinese?"请选择其他文件名，保留原文件。":"Choose a different name to preserve the source file."));return;}
            if(safe->session->busy()||safe->processor.recording().isBusy())return;safe->playing=false;safe->movieTarget=file;
            if(safe->movie.start(safe->processor.recording().snapshot(),options,file)){safe->editor->setEnabled(false);safe->exportProgress=std::make_unique<ExportProgressWindow>(options.chinese,safe.getComponent(),[safe]{if(safe)safe->movie.cancel();});safe->exportProgress->update(safe->movie.stage(),safe->movie.progress(),file.getFullPathName(),{});}});
    }
    void chooseExportFile(bool audioOnly){
        movieMessage.clear();
        const auto info=session->current();const auto ext=info.file==juce::File{}?juce::String(".wav"):info.video&&audioOnly?audioExtension(info):info.file.getFileExtension();
        if(ext.isEmpty()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SpectrumTag",processor.getEditorState().chinese?juce::String::fromUTF8("暂不支持以原音轨编码导出此文件。原文件保持不变。"):"This source audio codec cannot be exported without changing format.");return;}
        auto target=defaultExportFolder().getNonexistentChildFile((info.file==juce::File{}?juce::String("Untitled"):info.file.getFileNameWithoutExtension())+"_SpectrumTag",ext);
        chooser=std::make_unique<juce::FileChooser>(processor.getEditorState().chinese?juce::String::fromUTF8(audioOnly?"导出原格式音轨":"按原格式导出"):audioOnly?"Export audio in source codec":"Export in original format",target,"*"+ext);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<Player>(this),audioOnly](const juce::FileChooser& c){if(safe&&c.getResult()!=juce::File{})safe->session->exportTo(c.getResult(),audioOnly);});
    }
    void timerCallback()override{
        if(exportProgress){if(exportProgress->isVisible())exportProgress->update(movie.stage(),movie.progress(),movieTarget.getFullPathName(),movie.error());else exportProgress.reset();}
        if(!movie.busy())session->ensureBlankCanvas();
        if(session->loadedRevision()!=seen){seen=session->loadedRevision();position=0;seek=0;if(!session->restoredLastSession())processor.resetFileView();processor.publishFileTransport(false,0);}
        const bool chinese=processor.getEditorState().chinese;auto tr=[&](const char* zh,const char* en){return juce::String::fromUTF8(chinese?zh:en);};
        play.setButtonText(playing?tr("暂停","Pause"):tr("播放","Play"));stop.setButtonText(tr("停止","Stop"));exportButton.setButtonText(tr("导出","Export"));device.setButtonText(tr("音频设备","Audio device"));
        auto text=session->status();if(movie.busy()){const auto stage=movie.stage();text=(stage==SpectrumVideoExport::preparing?tr("准备声音…","Preparing audio…"):stage==SpectrumVideoExport::finishing?tr("正在完成视频…","Finalising video…"):tr("导出频谱视频 ","Exporting spectrum video ")+juce::String(juce::roundToInt(movie.progress()*100))+"%");exportButton.setButtonText(tr("取消导出","Cancel export"));}
        else if(seenMovieStage!=movie.stage()){if(movie.stage()==SpectrumVideoExport::complete)text=tr("已导出：","Exported: ")+movieTarget.getFullPathName();else if(movie.stage()==SpectrumVideoExport::cancelled)text=tr("已取消导出","Export cancelled");else if(movie.stage()==SpectrumVideoExport::failed)text=movie.error();movieMessage=text;}
        if(!movie.busy()&&movieMessage.isNotEmpty())text=movieMessage;seenMovieStage=movie.stage();
        status.setText(text,juce::dontSendNotification);status.setColour(juce::Label::textColourId,juce::Colours::lightgrey);editor->setEnabled(!movie.busy());device.setEnabled(!movie.busy());
        auto audio=processor.recording().snapshot();const bool hasContent=audio&&(!session->current().generated||!audio->edits.empty());
        play.setEnabled(!movie.busy()&&!session->busy()&&hasContent);exportButton.setEnabled(movie.busy()||(!session->busy()&&!processor.recording().isBusy()&&hasContent));
    }
    juce::String movieMessage;
};
int filelessUiTest(const juce::File& folder){try{
    folder.createDirectory();auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto wait=[&](auto condition){const auto end=juce::Time::getMillisecondCounter()+30000;do{juce::MessageManager::getInstance()->runDispatchLoopUntil(10);juce::Timer::callPendingTimersSynchronously();if(condition())return;}while((int)(end-juce::Time::getMillisecondCounter())>0);throw std::runtime_error("fileless UI wait timed out");};
    Player panel(false);auto& processor=panel.processor;wait([&]{return processor.recording().snapshot()&&!processor.recording().isBusy();});
    auto* image=revision1416::box(panel);auto* print=dynamic_cast<juce::Button*>(revision1416::find(panel,"printButton"));auto* fill=dynamic_cast<juce::Slider*>(revision1416::find(panel,"noiseFill"));auto* view=revision1416::find(panel,"spectrumView");
    need(image&&print&&fill&&view,"blank canvas controls exist");wait([&]{return print->isEnabled()&&fill->isEnabled();});
    juce::Image picture(juce::Image::RGB,96,128,true);{juce::Graphics g(picture);g.fillAll(juce::Colours::white);g.setColour(juce::Colours::black);g.fillRect(15,16,66,24);g.fillRect(36,30,24,78);}const auto path=folder.getChildFile("letter-T.png");{auto out=path.createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(picture,*out),"UI image fixture");}
    image->loadImageFromFile(path);fill->setValue(.65,juce::sendNotificationSync);wait([&]{return image->conversionReady();});
    print->onClick();wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.size()==1&&!processor.recording().isBusy()&&(int)view->getProperties()["rasterRevision"]==s->revision;});
    const auto audio=processor.recording().snapshot();need(audio->sourceId.startsWith("generated:")&&audio->end()>.1,"UI Print extends source-free timeline");double energy=0;for(int64_t i=audio->first;i<audio->last;++i)energy+=std::pow(audio->sample(0,i),2);need(energy>.01,"UI Print generates real sound");
    panel.keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier,0));wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.empty()&&!processor.recording().isBusy();});
    panel.keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier,0));wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.size()==1&&!processor.recording().isBusy();});
    need(processor.recording().snapshot()->last==audio->last,"UI redo restores generated extent");
    auto* clear=dynamic_cast<juce::Button*>(revision1416::find(panel,"clearLoad"));clear->onClick();wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.empty()&&s->last==1&&!processor.recording().isBusy()&&!processor.isClearPending();});
    panel.keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier,0));wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.size()==1&&!processor.recording().isBusy();});
    need(processor.recording().snapshot()->last==audio->last,"UI undo Clear restores generated tags");
    wait([&]{auto s=processor.recording().snapshot();if((int)view->getProperties()["rasterRevision"]!=s->revision)return false;const auto image=view->createComponentSnapshot(view->getLocalBounds());int coloured=0;for(int y=40;y<image.getHeight()-100;y+=2)for(int x=80;x<image.getWidth()-20;x+=2){auto c=image.getPixelAt(x,y);coloured+=c.getSaturation()>.25f&&c.getBrightness()>.15f;}return coloured>100;});
    auto ui=panel.createComponentSnapshot(panel.getLocalBounds());{auto out=folder.getChildFile("standalone-fileless.png").createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(ui,*out),"UI screenshot");}
    panel.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    const auto immediate=view->createComponentSnapshot(view->getLocalBounds());int immediateResidual=0;
    {auto out=folder.getChildFile("deleted-immediately.png").createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(immediate,*out),"immediate deletion screenshot");}
    for(int y=40;y<immediate.getHeight()-100;y+=2)for(int x=80;x<immediate.getWidth()-20;x+=2){const auto c=immediate.getPixelAt(x,y);immediateResidual+=c.getSaturation()>.25f&&c.getBrightness()>.15f;}
    need(immediateResidual==0,"Delete immediately clears cached pixels before playback or another timer tick");
    wait([&]{auto s=processor.recording().snapshot();return s&&s->edits.empty()&&!processor.recording().isBusy()&&(int)view->getProperties()["rasterRevision"]==s->revision;});
    const auto deleted=processor.recording().snapshot();const auto deletedImage=view->createComponentSnapshot(view->getLocalBounds());
    {auto out=folder.getChildFile("deleted.png").createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(deletedImage,*out),"deleted screenshot");}
    int residual=0;for(int y=40;y<deletedImage.getHeight()-100;y+=2)for(int x=80;x<deletedImage.getWidth()-20;x+=2){const auto c=deletedImage.getPixelAt(x,y);residual+=c.getSaturation()>.25f&&c.getBrightness()>.15f;}
    folder.getChildFile("deletion-state.txt").replaceWithText("last="+juce::String(deleted->last)+" residual="+juce::String(residual)+" preview="+view->getProperties()["previewActive"].toString());
    need(residual==0&&!(bool)view->getProperties()["previewActive"],"Delete removes generated Print from actual visible spectrum");
    need(deleted->frameAt(deleted->end()+.01)==nullptr,"removed tail cannot read retained boundary FFT bins");
    for(bool zh:{true,false}){
        VideoOptions options;options.chinese=zh;VideoOptions accepted;int accepts=0;
        VideoSettings settings(options,[&](auto value){accepted=value;++accepts;});
        auto* aspect=dynamic_cast<juce::ComboBox*>(revision1416::find(settings,"videoAspect"));
        auto* resolution=dynamic_cast<juce::ComboBox*>(revision1416::find(settings,"videoResolution"));
        auto* destination=dynamic_cast<juce::Button*>(revision1416::find(settings,"videoDestination"));
        need(aspect&&resolution&&destination&&aspect->getSelectedId()==3&&resolution->getText()=="1920 x 1080","export UI defaults to the existing 16:9 resolution");
        const char* expected[]{"1440 x 1080","1080 x 1440","1920 x 1080","1080 x 1920"};
        for(int i=0;i<4;++i){aspect->setSelectedId(i+1,juce::sendNotificationSync);need(resolution->getSelectedId()==2&&resolution->getText()==expected[i],"aspect changes update dimensions without changing quality tier");}
        destination->onClick();need(accepts==1&&accepted.aspect==VideoOptions::ratio9x16&&accepted.width()==1080&&accepted.height()==1920,"export callback uses selected portrait size");
        need(VideoOptions::remembered().aspect==VideoOptions::ratio9x16,"export UI remembers aspect");
        VideoSettings restored(VideoOptions::remembered(),[](auto){});need(dynamic_cast<juce::ComboBox*>(revision1416::find(restored,"videoAspect"))->getSelectedId()==4,"reopened export UI restores portrait selection");
        for(int i=0;i<settings.getNumChildComponents();++i)need(settings.getLocalBounds().contains(settings.getChildComponent(i)->getBounds()),"export controls fit the taller dialog");
        auto settingsImage=settings.createComponentSnapshot(settings.getLocalBounds());auto out=folder.getChildFile(zh?"export-settings-zh.png":"export-settings-en.png").createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(settingsImage,*out),"bilingual export settings screenshot");
    }
    int cancels=0,closes=0;ExportProgressContent progress(true,[&]{++cancels;});progress.onClose=[&]{++closes;};
    progress.update(SpectrumVideoExport::frames,.42f,"C:/Example/movie.mp4",{});need(progress.getProperties()["percent"].toString()=="42%","progress popup shows numeric progress");
    {auto out=folder.getChildFile("export-progress.png").createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(progress.createComponentSnapshot(progress.getLocalBounds()),*out),"progress popup screenshot");}
    auto* cancel=dynamic_cast<juce::Button*>(revision1416::find(progress,"cancelVideoExport"));need(cancel&&cancel->isEnabled(),"popup Cancel is enabled");cancel->onClick();progress.requestClose();need(cancels==1&&!cancel->isEnabled()&&closes==0,"Cancel and title-bar close issue only one cancellation");
    progress.update(SpectrumVideoExport::cancelled,.42f,"C:/Example/movie.mp4",{});cancel->onClick();need(closes==1,"cancelled dialog may close");progress.update(SpectrumVideoExport::complete,1,"C:/Example/movie.mp4",{});need(progress.getProperties()["percent"].toString()=="100%","completed popup reaches 100 percent");
    need(defaultExportFolder()==juce::File::getSpecialLocation(juce::File::userDesktopDirectory),"exports default to the actual desktop folder");
    folder.getChildFile("ui-checks.log").replaceWithText("PASS blank startup, real UI Print, Ctrl+Z/Shift+Ctrl+Z, Clear/undo, immediate/stopped Delete pixels, FFT tail extent, Desktop exports, progress popup/percentage/cancel/close\n");return 0;
}catch(const std::exception& e){folder.getChildFile("ui-failure.txt").replaceWithText(e.what());return 1;}}
int viewTest(const juce::File& folder){try{
    folder.createDirectory();auto report=folder.getChildFile("view-checks.log").createOutputStream();
    auto log=[&](const juce::String& text){report->writeText(text+"\n",false,false,nullptr);report->flush();};
    auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto pump=[] {juce::MessageManager::getInstance()->runDispatchLoopUntil(10);juce::Timer::callPendingTimersSynchronously();};
    auto wait=[&](auto condition){const auto deadline=juce::Time::getMillisecondCounter()+20000;do{pump();if(condition())return;}while((int)(deadline-juce::Time::getMillisecondCounter())>0);throw std::runtime_error("view test timeout");};
    const auto wav=folder.getChildFile("long-input.wav");
    {auto stream=wav.createOutputStream();juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),8000,1,16,{},0));need(writer!=nullptr,"view WAV fixture");stream.release();juce::AudioBuffer<float> audio(1,8000);for(int n=0;n<8000;++n)audio.setSample(0,n,.1f*std::sin(2*juce::MathConstants<float>::pi*440*n/8000));for(int i=0;i<60;++i)writer->writeFromAudioSampleBuffer(audio,0,8000);}
    auto span=[](juce::Component& view){return (double)view.getProperties()["displayEnd"]-(double)view.getProperties()["displayStart"];};
    {Player panel(false);auto* view=revision1416::find(panel,"spectrumView");
        panel.processor.onDropAudioFile(wav);wait([&]{return panel.processor.recording().snapshot()&&!panel.processor.recording().isBusy()&&(bool)view->getProperties()["recordMode"]&&panel.processor.getStateRevision()>0;});
        for(int i=0;i<10;++i)pump();const double expected=(view->getWidth()-70)/60.;
        log("Fresh Load: window="+juce::String(span(*view),6)+" expected="+juce::String(expected,6));
        need(std::abs(span(*view)-expected)<.001,"fresh Load must use Speed without hidden Fit zoom");
        auto* fit=dynamic_cast<juce::Button*>(revision1416::find(panel,"fitSpectrum"));fit->onClick();wait([&]{return std::abs(span(*view)-60)<.001;});
        auto* time=dynamic_cast<juce::Label*>(revision1416::find(panel,"timeSpan"));need(time&&time->getText().contains(juce::String::fromUTF8("×")),"Fit has its own displayed scale value");
        auto* speed=dynamic_cast<juce::Slider*>(revision1416::find(panel,"speed"));need(std::abs(speed->getValue()-1)<.0001,"Fit does not rewrite normal Speed");
        speed->setValue(2,juce::sendNotificationSync);wait([&]{return std::abs(span(*view)-expected*.5)<.001;});
        speed->setValue(1,juce::sendNotificationSync);fit->onClick();pump();
        const auto oldSource=panel.processor.recording().snapshot()->sourceId;const auto oldState=panel.processor.getStateRevision();
        panel.processor.onDropAudioFile(wav);wait([&]{auto s=panel.processor.recording().snapshot();return s&&s->sourceId!=oldSource&&!panel.processor.recording().isBusy()&&panel.processor.getStateRevision()>oldState;});for(int i=0;i<10;++i)pump();
        need(std::abs(span(*view)-expected)<.001,"loading again clears previous Fit zoom");
        log("PASS initial Load and replacement use absolute Speed; explicit Fit remains available");
    }
    for(bool audioBeforeEditor:{false,true}){
        SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session session(p);
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        if(!audioBeforeEditor)editor.reset(p.createEditor());
        session.load(wav);wait([&]{return !session.busy();});p.resetFileView();
        if(audioBeforeEditor)editor.reset(p.createEditor());for(int i=0;i<10;++i)pump();
        auto* view=revision1416::find(*editor,"spectrumView");const double expected=(view->getWidth()-70)/60.;
        need(std::abs(span(*view)-expected)<.001,"Load timing must not depend on snapshot/editor creation order");
        auto* speed=dynamic_cast<juce::Slider*>(revision1416::find(*editor,"speed"));speed->setValue(1.5,juce::sendNotificationSync);wait([&]{return std::abs(span(*view)-expected/1.5)<.001;});
        const auto end=(double)view->getProperties()["displayEnd"],window=span(*view);editor.reset();need(session.saveLastSession(),"save view session");
        {SpectrumTagAudioProcessor restored;restored.filePlayerMode=true;Session recall(restored,true);
            if(audioBeforeEditor)wait([&]{return !recall.busy();});
            std::unique_ptr<juce::AudioProcessorEditor> reopened(restored.createEditor());
            wait([&]{return !recall.busy();});for(int i=0;i<10;++i)pump();auto* v=revision1416::find(*reopened,"spectrumView");
            need(std::abs(span(*v)-window)<.001&&std::abs((double)v->getProperties()["displayEnd"]-end)<.001,"session recall must preserve time scale and position");
            need(restored.getFrequencyScale()==frequency::mel,"reopen keeps Mel without toggling preset");
            auto* fit=dynamic_cast<juce::Button*>(revision1416::find(*reopened,"fitSpectrum"));fit->onClick();wait([&]{return std::abs(span(*v)-60)<.001;});
        }
        {SpectrumTagAudioProcessor restored;restored.filePlayerMode=true;Session recall(restored,true);std::unique_ptr<juce::AudioProcessorEditor> reopened(restored.createEditor());wait([&]{return !recall.busy();});for(int i=0;i<10;++i)pump();
            need(std::abs(span(*revision1416::find(*reopened,"spectrumView"))-60)<.001,"intentional saved Fit survives restart");}
        log(juce::String("PASS session restore, explicit Fit recall, Mel default; audioBeforeEditor=")+juce::String((int)audioBeforeEditor));
    }
    log("ALL STARTUP VIEW CHECKS PASSED");return 0;
}catch(const std::exception& e){folder.getChildFile("view-failure.txt").replaceWithText(e.what());return 1;}}
int tailTest(const juce::File& folder){try{
    folder.createDirectory();auto report=folder.getChildFile("tail-checks.log").createOutputStream();
    auto log=[&](const juce::String& text){report->writeText(text+"\n",false,false,nullptr);report->flush();};
    auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto wait=[&](auto condition){const auto deadline=juce::Time::getMillisecondCounter()+30000;do{juce::MessageManager::getInstance()->runDispatchLoopUntil(10);juce::Timer::callPendingTimersSynchronously();if(condition())return;}while((int)(deadline-juce::Time::getMillisecondCounter())>0);throw std::runtime_error("tail test timeout");};
    const auto wav=folder.getChildFile("source.wav");{auto stream=wav.createOutputStream();juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000,2,24,{},0));need(writer!=nullptr,"tail WAV fixture");stream.release();juce::AudioBuffer<float> audio(2,96000);for(int n=0;n<96000;++n)for(int c=0;c<2;++c)audio.setSample(c,n,.2f*std::sin((float)(2*juce::MathConstants<double>::pi*(440+c*100)*n/48000)));writer->writeFromAudioSampleBuffer(audio,0,96000);}
    SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session session(p);session.load(wav);wait([&]{return !session.busy();});const auto original=p.recording().snapshot();need(original&&original->last==96000,"tail source loaded");
    const auto before=p.recording().document();PrintEdit e;e.start=3.2;e.duration=.6;e.fftSize=1024;e.rows=513;e.cols=64;e.low=.1f;e.high=.8f;e.compensation=.8f;e.mask=std::make_shared<const std::vector<float>>(e.rows*e.cols,0.f);
    p.recording().add(e);need(p.recording().flush(),"tail render");const auto printed=p.recording().snapshot();
    need(printed->last==182400&&printed->fileSourceLast==96000,"tail extends export range with original end preserved");
    double energy=0;for(int n=155000;n<181000;++n)energy+=std::pow(printed->sample(0,n),2);need(energy>.01,"blank tail generates audible Noise Fill");
    for(int n=0;n<96000;++n)for(int c=0;c<2;++c)need(printed->sample(c,n)==original->sample(c,n),"tail Print must not alter source song");
    for(int n=96000;n<153600;++n)need(printed->sample(0,n)==0,"gap before tail remains exact silence");
    need(!printed->contains(120000)&&printed->contains(170000),"tail coverage confined to Print");
    const auto* frame=printed->frameAt(3.5);need(frame&&*std::max_element(frame->level.begin(),frame->level.begin()+frame->bins)>40,"tail has actual spectrum");
    const auto output=folder.getChildFile("extended.wav");session.exportTo(output);wait([&]{return !session.busy();});juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output));
    need(reader&&reader->lengthInSamples==182400&&reader->bitsPerSample==24&&reader->sampleRate==48000&&reader->numChannels==2,"export includes tail in original WAV format");
    juce::AudioBuffer<float> samples(2,20000);reader->read(&samples,0,20000,155000,true,true);double exportedEnergy=0;for(int n=0;n<20000;++n)exportedEnergy+=std::pow(samples.getSample(0,n),2);need(exportedEnergy>.01,"export contains printed sound beyond source end");
    log("PASS blank-tail Print: actual sound/spectrum, silent gap, unchanged source and extended 24-bit stereo WAV export");
    const auto after=p.recording().document();const auto analyses=p.recording().analysisPasses();p.recording().remove(e.id);need(p.recording().flush(),"tail delete");need(p.recording().snapshot()->last==96000&&p.recording().snapshot()->sample(0,170000)==0,"Delete removes tail and noise");
    need(p.recording().analysisPasses()==analyses,"Delete tail uses original spectrum without new FFT");
    p.recording().restoreDocument(after);need(p.recording().flush(),"tail redo");need(p.recording().snapshot()->last==182400&&p.recording().snapshot()->sample(0,170000)==printed->sample(0,170000),"Redo restores exact tail");
    p.recording().restoreDocument(before);need(p.recording().flush(),"tail undo");need(p.recording().snapshot()->last==96000,"Undo restores original length");p.recording().restoreDocument(after);need(p.recording().flush(),"tail redo for save");
    need(session.saveLastSession(),"save extended session");
    {SpectrumTagAudioProcessor reopened;reopened.filePlayerMode=true;Session recall(reopened,true);wait([&]{return !recall.busy();});auto s=reopened.recording().snapshot();need(s&&s->last==182400&&s->fileSourceLast==96000&&s->sample(0,170000)==printed->sample(0,170000),"reopen restores exact extension");need(reopened.recording().cacheHits()==1&&reopened.recording().printPasses()==0,"tail reopen uses render cache");reopened.recording().remove(e.id);need(reopened.recording().flush()&&reopened.recording().snapshot()->last==96000,"reopened Delete remembers original duration");}
    log("PASS Delete without FFT, Undo/Redo, cached session recall and Delete after reopen");
    auto head=e;head.id=juce::Uuid().toString();head.start=-.55;head.duration=.3;
    p.recording().add(head);need(p.recording().flush(),"negative-time Print");const auto extended=p.recording().snapshot();
    need(extended->first==-26400&&extended->last==182400,"Print before zero extends the timeline to negative time");
    double headEnergy=0;for(int n=-25000;n<-13000;++n)headEnergy+=std::pow(extended->sample(0,n),2);need(headEnergy>.01,"negative-time Print is audible");
    for(int n=0;n<96000;++n)need(extended->sample(0,n)==original->sample(0,n),"prefix preserves original audio position");
    need(session.saveLastSession(),"save prefix and tail");
    {SpectrumTagAudioProcessor reopened;reopened.filePlayerMode=true;Session recall(reopened,true);wait([&]{return !recall.busy();});auto s=reopened.recording().snapshot();need(s&&s->first==-26400&&s->last==182400&&s->sample(0,-20000)==extended->sample(0,-20000),"negative Print recalled exactly");need(reopened.recording().cacheHits()==1,"negative Print cache hit");reopened.recording().remove(head.id);need(reopened.recording().flush()&&reopened.recording().snapshot()->first==0,"Delete prefix restores original zero");reopened.recording().remove(e.id);need(reopened.recording().flush()&&reopened.recording().snapshot()->last==96000,"Delete both restores full original range");}
    {Player panel(false);panel.processor.onDropAudioFile(wav);wait([&]{return panel.processor.recording().snapshot()&&!panel.processor.recording().isBusy();});panel.processor.recording().add(head);need(panel.processor.recording().flush(),"player negative Print");panel.processor.onFileSeek(-.4);juce::AudioBuffer<float> block(2,64);panel.getNextAudioBlock({&block,0,64});need(std::abs(panel.processor.audition().position()+.4)<1.e-8,"standalone playhead accepts negative time");}
    log("PASS negative-time Print, unchanged song position, cached recall, Delete and negative playhead seek");
    {juce::String error;const auto video=folder.getChildFile("original.mp4"),outputVideo=folder.getChildFile("extended.mp4");
        need(codecRun({"-v","error","-f","lavfi","-i","testsrc2=size=160x90:rate=10:duration=3","-i",wav.getFullPathName(),"-map","0:v:0","-map","1:a:0","-c:v","libx264","-c:a","aac","-y",video.getFullPathName()},error),"extended video fixture");
        session.load(video);wait([&]{return !session.busy();});p.recording().add(head);p.recording().add(e);need(p.recording().flush(),"video head/tail render");
        session.exportTo(outputVideo);wait([&]{return !session.busy();});log(session.status());need(outputVideo.existsAsFile(),"video with black prefix exports");
        juce::String inputFrames,outputFrames;need(codecRun({"-v","error","-i",video.getFullPathName(),"-map","0:v:0","-an","-f","framemd5","-"},inputFrames)&&codecRun({"-v","error","-i",outputVideo.getFullPathName(),"-map","0:v:0","-an","-f","framemd5","-"},outputFrames),"read video frame hashes");
        auto hashes=[](const juce::String& text){juce::StringArray result;for(const auto& line:juce::StringArray::fromLines(text))if(line.startsWith("0,"))result.add(line.fromLastOccurrenceOf(",",false,false).trim());return result;};
        auto a=hashes(inputFrames),b=hashes(outputFrames);need(a.size()==30&&b.size()==44,"black frames extend output duration to 4.4 seconds");
        for(int i=0;i<a.size();++i)need(a[i]==b[i+6],"original decoded picture is pixel-identical after black prefix");
        for(int i=1;i<6;++i)need(b[i]==b[0],"prefix black frames are constant");for(int i=36;i<b.size();++i)need(b[i]==b[0],"tail is also black");
        const auto decoded=folder.getChildFile("video-audio.wav");need(codecRun({"-v","error","-i",outputVideo.getFullPathName(),"-map","0:a:0","-c:a","pcm_f32le","-y",decoded.getFullPathName()},error),"decode extended soundtrack");
        std::unique_ptr<juce::AudioFormatReader> audio(formats.createReaderFor(decoded));need(audio&&audio->lengthInSamples>=211200,"extended video audio duration");
        juce::AudioBuffer<float> region(2,48000);audio->read(&region,0,48000,28800,true,true);auto rendered=p.recording().snapshot();double difference=0,signal=0;for(int n=1000;n<47000;++n){const double v=rendered->sample(0,n);signal+=v*v;difference+=std::pow(region.getSample(0,n)-v,2);}need(difference/signal<.03,"original audio and picture shift together by 0.6 seconds");
        const auto audioOnly=folder.getChildFile("extended-audio.m4a");session.exportTo(audioOnly,true);wait([&]{return !session.busy();});need(audioOnly.existsAsFile(),"extended video audio-only original codec export");
        log("PASS video black prefix/tail, lossless original picture frames, audio-picture sync, extended audio-only export");
        session.load(wav);wait([&]{return !session.busy();});
    }
    // The same edit must remain clipped to real capture in the realtime engine.
    {RecordingEngine realtime;realtime.importAudio(wav,0);need(realtime.flush(),"realtime fixture");realtime.add(e);need(realtime.flush(),"realtime boundary");need(realtime.snapshot()->last==96000&&!realtime.snapshot()->contains(170000),"ordinary VST3 capture must not gain standalone tail coverage");}
    // Use the real Print button with its image wholly outside the loaded song.
    p.recording().restoreDocument(before);need(p.recording().flush(),"UI tail reset");p.resetFileView();std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    const auto imageFile=folder.getChildFile("tail-image.png");{juce::Image img(juce::Image::RGB,100,100,true);juce::Graphics g(img);g.fillAll(juce::Colours::white);auto out=imageFile.createOutputStream();juce::PNGImageFormat png;need(out&&png.writeImageToStream(img,*out),"tail image");}
    static_cast<SpectrumTagAudioProcessorEditor*>(editor.get())->filesDropped({imageFile.getFullPathName()},200,200);
    auto* box=revision1416::box(*editor);box->setNormalisedBounds({.5f,.2f,.15f,.3f});auto* noise=dynamic_cast<juce::Slider*>(revision1416::find(*editor,"noiseFill"));need(noise!=nullptr,"Noise Fill control");noise->setValue(.8,juce::sendNotificationSync);wait([&]{return box->conversionReady();});
    auto* button=dynamic_cast<juce::Button*>(revision1416::find(*editor,"printButton"));need(button!=nullptr,"Print control");button->onClick();wait([&]{return !p.recording().isBusy()&&!p.recording().edits()->empty();});need(p.recording().edits()->front().start>2&&p.recording().snapshot()->end()>2,"UI Print accepts image wholly after song");
    editor->keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier,0));wait([&]{return !p.recording().isBusy()&&p.recording().edits()->empty();});need(p.recording().snapshot()->last==96000,"real UI Undo removes extension");
    editor->keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier,0));wait([&]{return !p.recording().isBusy()&&!p.recording().edits()->empty();});need(p.recording().snapshot()->end()>2,"real UI Redo restores extension");
    log("PASS real UI Print outside song, Ctrl+Z / Ctrl+Shift+Z, realtime isolation");log("ALL TAIL CHECKS PASSED");return 0;
}catch(const std::exception& e){folder.getChildFile("tail-failure.txt").replaceWithText(e.what());return 1;}}
int missingTest(const juce::File& folder){try{
    folder.createDirectory();auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto wait=[&](Session& s){for(int i=0;i<3000&&s.busy();++i){juce::MessageManager::getInstance()->runDispatchLoopUntil(5);juce::Thread::sleep(2);}need(!s.busy(),"missing test timeout");};
    const auto wav=folder.getChildFile("input.wav");{auto out=wav.createOutputStream();juce::WavAudioFormat f;std::unique_ptr<juce::AudioFormatWriter> writer(f.createWriterFor(out.get(),8000,1,16,{},0));need(writer!=nullptr,"missing WAV fixture");out.release();juce::AudioBuffer<float> audio(1,800);audio.clear();writer->writeFromAudioSampleBuffer(audio,0,800);}
    {SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session s(p);s.load(wav);wait(s);auto state=p.getEditorState();state.imagePath=folder.getChildFile("missing-image.png").getFullPathName();p.setEditorState(state);need(s.saveLastSession(),"save missing-image fixture");}
    {SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session s(p,true);wait(s);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());need(p.recording().snapshot()&&!revision1416::box(*editor)->hasImage(),"missing image is blank, audio stays available");}
    const auto saved=Session::sessionFile().loadFileAsString();auto xml=juce::XmlDocument::parse(saved);need(xml!=nullptr,"read session fixture");
    juce::File cache(xml->getChildByName("SpectrumTagState")->getStringAttribute("recordingFile"));need(cache.moveFileTo(folder.getChildFile("moved-cache.sptrec")),"move own cache fixture");
    {SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session s(p,true);wait(s);need(!p.recording().snapshot()&&s.status().isEmpty(),"missing audio quietly leaves blank view");}
    need(Session::sessionFile().loadFileAsString()==saved,"blank failed restore must not overwrite prior session");
    {SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session s(p,true);s.load(wav);wait(s);need(p.recording().snapshot()!=nullptr&&s.exportable(),"user can load normally after missing restore");}
    {SpectrumTagAudioProcessor p;p.filePlayerMode=true;Session s(p,true);wait(s);need(s.restoredLastSession()&&p.recording().snapshot(),"new Load replaces missing session normally");}
    folder.getChildFile("missing-checks.log").replaceWithText("PASS missing image stays blank; missing audio/cache is silent and blank; old session retained; new Load and subsequent restore work\n");return 0;
}catch(const std::exception& e){folder.getChildFile("missing-failure.txt").replaceWithText(e.what());return 1;}}
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
    need(session.saveLastSession(),"standalone session persisted");
    {SpectrumTagAudioProcessor reopened;reopened.filePlayerMode=true;Session recalled(reopened,true);wait([&]{return !recalled.busy();});
        need(recalled.restoredLastSession()&&recalled.current().file==wav&&recalled.exportable(),"standalone remembers source and format");
        auto restored=reopened.recording().snapshot();need(restored&&restored->edits.size()==1&&sameAudioEdit(restored->edits[0],edit),"standalone remembers Print edits");
        need(reopened.recording().cacheHits()==1&&reopened.recording().printPasses()==0&&reopened.recording().analysisPasses()==0,"standalone reopen skips DSP");
        for(int n=0;n<96000;n+=19)need(restored->sample(0,n)==wet->sample(0,n),"standalone restored audio exact");
        reopened.recording().clear();need(reopened.recording().flush(),"standalone Clear before quit");
    }
    {SpectrumTagAudioProcessor cleared;cleared.filePlayerMode=true;Session recalled(cleared,true);wait([&]{return !recalled.busy();});need(!cleared.recording().snapshot(),"standalone Clear is retained next launch");}
    log("PASS standalone last-session source, Print, exact audio, cached reopen and Clear persistence");
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
        if(args.size()>=2&&args[0]=="--video-test"){setApplicationReturnValue(spectrumVideoChecks(juce::File(args[1].unquoted())));quit();return;}
        if(args.size()>=2&&args[0]=="--fileless-ui-test"){setApplicationReturnValue(filelessUiTest(juce::File(args[1].unquoted())));quit();return;}
        if(args.size()>=2&&args[0]=="--self-test"){setApplicationReturnValue(selfTest(juce::File(args[1].unquoted())));quit();return;}
        if(args.size()>=2&&args[0]=="--view-test"){setApplicationReturnValue(viewTest(juce::File(args[1].unquoted())));quit();return;}
        if(args.size()>=2&&args[0]=="--tail-test"){setApplicationReturnValue(tailTest(juce::File(args[1].unquoted())));quit();return;}
        if(args.size()>=2&&args[0]=="--missing-test"){setApplicationReturnValue(missingTest(juce::File(args[1].unquoted())));quit();return;}
        window=std::make_unique<Window>();
    }
    void shutdown()override{window.reset();}void systemRequestedQuit()override{quit();}
private:
    class Window final : public juce::DocumentWindow {public:Window():juce::DocumentWindow("SpectrumTag "+juce::String(ProjectInfo::versionString),juce::Colour(0xff1a1c1e),allButtons){setUsingNativeTitleBar(true);setContentOwned(new Player(),true);setResizable(true,false);setResizeLimits(930,552,3100,1742);if(auto* c=getConstrainer())c->setFixedAspectRatio(1240./722);centreWithSize(1240,722);setVisible(true);}void closeButtonPressed()override{juce::JUCEApplication::getInstance()->systemRequestedQuit();}};
    std::unique_ptr<Window> window;
};
}
START_JUCE_APPLICATION(App)
