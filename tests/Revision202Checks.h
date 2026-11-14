#pragma once
// 2.0.1 灰度模式：彩色图片自动切连续灰度，线稿保持二值；
// 手动可切换，灰度对比/亮度参与掩模，参数随工程保存。
static void revision202Checks(const juce::File& dir){
    using namespace spectrumtag;
    juce::Image colour(juce::Image::RGB,96,96,false);
    for(int y=0;y<96;++y)for(int x=0;x<96;++x)
        colour.setPixelAt(x,y,juce::Colour::fromFloatRGBA(x/95.0f,y/95.0f,0.5f,1.0f));
    png(dir.getChildFile("202-colour.png"),colour);
    SpectrumTagAudioProcessor p;Transport host;p.setPlayHead(&host);p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
    require(view&&box,"202 editor parts");
    auto style=[&]{return (int)*p.getAPVTS().getRawParameterValue(ParameterIDs::imageStyle);};
    auto drop=[&](const juce::File& file){
        dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({file.getFullPathName()},0,0);tickUI();
    };
    auto mean=[](const std::vector<float>& values){
        double sum=0;for(float v:values)sum+=v;return values.empty()?0.0:sum/(double)values.size();
    };
    // 线稿：保持二值，掩模只有 0/1。
    drop(dir.getChildFile("transparent-detail.png"));
    require(style()==0,"202 line art stays B/W");
    for(float v:box->generateMask(129,96))require(v==0.0f||v==1.0f,"202 B/W mask stays binary");
    // 彩色图片：自动切到灰度，掩模出现连续值。
    drop(dir.getChildFile("202-colour.png"));
    require(style()==1,"202 colour image switches to grayscale automatically");
    const auto grayMask=box->generateMask(257,160);
    int fractions=0;for(float v:grayMask){require(v>=0.0f&&v<=1.0f,"202 mask value in range");if(v>0.02f&&v<0.98f)++fractions;}
    require(fractions>(int)grayMask.size()/20,"202 grayscale mask keeps continuous values");
    // 自动参数按当前图片拟合：换一张色调完全不同的图，参数重算而不是继承。
    const auto toneA=box->grayTone();
    require(std::abs((float)*p.getAPVTS().getRawParameterValue(ParameterIDs::imageGrayContrast)-toneA.contrast)<1e-3f,"202 auto contrast comes from the image");
    require(toneA.contrast>=0.55f,"202 auto contrast is heavy by default");
    // 压在亮频谱带上：暗部应完全变黑，亮部保留。
    box->setNormalisedBounds({.10f,.80f,.20f,.18f});tickUI();
    png(dir.getChildFile("202-grayscale-auto.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    box->setNormalisedBounds({.30f,.20f,.20f,.40f});tickUI();
    juce::Image flat(juce::Image::RGB,96,96,false);
    for(int y=0;y<96;++y)for(int x=0;x<96;++x)
        flat.setPixelAt(x,y,x<48?juce::Colours::black:juce::Colour::fromFloatRGBA(1.0f,0.55f,0.0f,1.0f));
    png(dir.getChildFile("202-flat.png"),flat);
    set(p,ParameterIDs::imageGrayContrast,0.05f);set(p,ParameterIDs::imageGrayBrightness,0.05f);tickUI();
    drop(dir.getChildFile("202-flat.png"));
    const auto toneB=box->grayTone();
    require(std::abs((float)*p.getAPVTS().getRawParameterValue(ParameterIDs::imageGrayContrast)-toneB.contrast)<1e-3f,"202 new image does not inherit the old tone");
    require(std::abs(toneB.contrast-toneA.contrast)>0.02f||std::abs(toneB.brightness-toneA.brightness)>0.02f,"202 a different image gets a different tone");
    const auto flatMask=box->generateMask(32,32);
    for(int r=0;r<32;++r)for(int c=0;c<8;++c)require(flatMask[(size_t)r*32+c]==1.0f,"202 pure black is fully cut even in grayscale");
    std::cout<<"202_TONE gradient contrast="<<toneA.contrast<<" brightness="<<toneA.brightness
        <<" flat contrast="<<toneB.contrast<<" brightness="<<toneB.brightness<<"\n";
    // 回到主测试图，参数跟着这张图重新计算。
    drop(dir.getChildFile("202-colour.png"));
    require(std::abs((float)*p.getAPVTS().getRawParameterValue(ParameterIDs::imageGrayContrast)-toneA.contrast)<1e-3f,"202 tone follows the current image");
    // 手动切回黑白：同一张彩色图重新按阈值取形状。
    set(p,ParameterIDs::imageStyle,0);tickUI();
    for(float v:box->generateMask(129,96))require(v==0.0f||v==1.0f,"202 manual switch returns to binary");
    // 灰度参数参与掩模：对比改变分布，亮度整体提亮。
    set(p,ParameterIDs::imageStyle,1);set(p,ParameterIDs::imageGrayContrast,0.9f);tickUI();
    const auto highContrast=box->generateMask(257,160);
    require(highContrast!=grayMask,"202 gray contrast changes the mask");
    set(p,ParameterIDs::imageGrayContrast,0.1f);tickUI();
    const auto lowContrast=box->generateMask(257,160);
    require(lowContrast!=highContrast,"202 gray contrast works in both directions");
    set(p,ParameterIDs::imageGrayContrast,0.5f);set(p,ParameterIDs::imageGrayBrightness,0.8f);tickUI();
    const auto brighter=box->generateMask(257,160);
    require(mean(brighter)<mean(grayMask),"202 gray brightness lifts the image");
    require(mean(highContrast)!=mean(lowContrast),"202 contrast is not a no-op");
    // 打印：Edit 记录灰度参数，掩模保留连续值，工程保存/恢复一致。
    p.recording().setArmed(true);
    juce::AudioBuffer<float> source(2,48000*3);
    for(int c=0;c<2;++c)for(int i=0;i<source.getNumSamples();++i)source.setSample(c,i,.1f*std::sin(i*.019f));
    p.recording().capture(source,0,48000,true);p.recording().setArmed(false);
    require(p.recording().flush(),"202 source capture");tickUI();fitSpectrum(*view);tickUI();
    box->setNormalisedBounds({.10f,.10f,.20f,.40f});tickUI();
    png(dir.getChildFile("202-grayscale-placing.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    auto* print=dynamic_cast<juce::Button*>(componentWithID(*editor,"printButton"));require(print!=nullptr,"202 Print button");
    print->onClick();require(p.recording().flush(),"202 grayscale print render");tickUI();
    auto edits=p.recording().edits();require(edits&&!edits->empty(),"202 printed grayscale object");
    const auto printed=edits->back();
    require(printed.imageStyle==1&&std::abs(printed.grayContrast-.5f)<1e-6f&&std::abs(printed.grayBrightness-.8f)<1e-6f,"202 edit stores grayscale settings");
    int printedFractions=0;for(float v:*printed.mask)if(v>0.02f&&v<0.98f)++printedFractions;
    require(printedFractions>(int)printed.mask->size()/50,"202 printed mask is continuous");
    juce::MemoryBlock state;p.getStateInformation(state);
    SpectrumTagAudioProcessor recalled;recalled.setStateInformation(state.getData(),(int)state.getSize());
    require(recalled.recording().flush(),"202 recall render");tickUI();
    require(recalled.recording().edits()&&!recalled.recording().edits()->empty(),"202 recalled edit exists");
    require(sameEdit(printed,recalled.recording().edits()->back()),"202 grayscale settings survive save/load");
    png(dir.getChildFile("202-grayscale-print.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    std::cout<<"2.0.1 GRAYSCALE CHECKS PASSED; auto style, continuous mask, contrast/brightness and save/load\n"<<std::flush;
}
