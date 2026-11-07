#pragma once
#include "SpectrumVideo.h"
#include "../shared/PlatformFonts.h"
namespace spectrumtag::standalone {
class ExportProgressContent final : public juce::Component {
public:
    ExportProgressContent(bool language,std::function<void()> cancel):bar(value),chinese(language),onCancel(std::move(cancel)){
        for(auto* label:{&stageLabel,&destinationLabel}){addAndMakeVisible(label);label->setFont(juce::Font(juce::FontOptions(chinese?ui::chineseFontName():juce::Font::getDefaultSansSerifFontName(),16.f,juce::Font::plain)));label->setColour(juce::Label::textColourId,juce::Colour(0xffe5edf4));}
        bar.setPercentageDisplay(false);bar.setColour(juce::ProgressBar::backgroundColourId,juce::Colour(0xff303a40));bar.setColour(juce::ProgressBar::foregroundColourId,juce::Colour(0xff66cbb1));addAndMakeVisible(bar);
        action.setComponentID("cancelVideoExport");addAndMakeVisible(action);action.onClick=[this]{requestClose();};setSize(540,218);update(SpectrumVideoExport::preparing,0,{},{});
    }
    void update(SpectrumVideoExport::Stage stage,float progress,const juce::String& path,const juce::String& error){
        finished=stage==SpectrumVideoExport::complete||stage==SpectrumVideoExport::cancelled||stage==SpectrumVideoExport::failed;
        value=stage==SpectrumVideoExport::complete?1.:juce::jlimit(0.,.99,(double)progress);
        const auto percent=juce::String(juce::roundToInt(value*100))+"%";bar.setTextToDisplay(percent);getProperties().set("percent",percent);getProperties().set("finished",finished);
        juce::String text;if(cancelling&&!finished)text=tr("正在取消…","Cancelling…");else switch(stage){
            case SpectrumVideoExport::complete:text=tr("导出完成","Export complete");break;
            case SpectrumVideoExport::cancelled:text=tr("已取消导出","Export cancelled");break;
            case SpectrumVideoExport::failed:text=tr("导出失败","Export failed");break;
            case SpectrumVideoExport::frames:text=tr("正在生成频谱画面","Rendering spectrum frames");break;
            case SpectrumVideoExport::finishing:text=tr("正在完成视频文件","Finalising video file");break;
            default:text=tr("正在准备音频","Preparing audio");break;
        }
        stageLabel.setText(text,juce::dontSendNotification);getProperties().set("stageText",text);
        destinationLabel.setText(stage==SpectrumVideoExport::failed?error:path,juce::dontSendNotification);destinationLabel.setTooltip(stage==SpectrumVideoExport::failed?error:path);
        action.setButtonText(finished?tr("关闭","Close"):tr("取消导出","Cancel export"));action.setEnabled(finished||!cancelling);
    }
    void requestClose(){if(finished){if(onClose)onClose();}else if(!cancelling){cancelling=true;action.setEnabled(false);stageLabel.setText(tr("正在取消…","Cancelling…"),juce::dontSendNotification);onCancel();}}
    std::function<void()> onClose;
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff1a2024));}
    void resized()override{auto area=getLocalBounds().reduced(22);stageLabel.setBounds(area.removeFromTop(30));area.removeFromTop(12);bar.setBounds(area.removeFromTop(28));area.removeFromTop(10);destinationLabel.setBounds(area.removeFromTop(40));area.removeFromTop(12);action.setBounds(area.removeFromTop(34).removeFromRight(130));}
private:
    double value=0;juce::ProgressBar bar;juce::Label stageLabel,destinationLabel;juce::TextButton action;bool chinese=false,finished=false,cancelling=false;std::function<void()> onCancel;
    juce::String tr(const char* zh,const char* en)const{return juce::String::fromUTF8(chinese?zh:en);}
};
class ExportProgressWindow final : public juce::DialogWindow {
public:
    ExportProgressWindow(bool chinese,juce::Component* parent,std::function<void()> cancel):DialogWindow(juce::String::fromUTF8(chinese?"导出频谱视频":"Export spectrum video"),juce::Colour(0xff1a2024),true){
        setUsingNativeTitleBar(true);content=new ExportProgressContent(chinese,std::move(cancel));setContentOwned(content,true);setResizable(false,false);
        content->onClose=[this]{exitModalState(0);setVisible(false);};centreAroundComponent(parent,getWidth(),getHeight());setVisible(true);enterModalState(true,nullptr,false);
    }
    void closeButtonPressed()override{content->requestClose();}
    void update(SpectrumVideoExport::Stage stage,float progress,const juce::String& path,const juce::String& error){content->update(stage,progress,path,error);}
private:ExportProgressContent* content=nullptr;
};
}
