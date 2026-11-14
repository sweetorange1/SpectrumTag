#pragma once
// 2.0.2：右键是没有菜单的上下文操作。
// 状态1（未打印，有源图）→ 打印；状态3（聚焦）→ 退回图片模式；
// 没有聚焦对象时 → 聚焦离点击位置最近的已打印图形。
static void revision201Checks(const juce::File& dir){
    using namespace spectrumtag;
    SpectrumTagAudioProcessor p;Transport host;p.setPlayHead(&host);p.setRateAndBufferSizeDetails(48000,256);p.prepareToPlay(48000,256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());tickUI();
    auto* view=componentWithID(*editor,"spectrumView");auto* box=imageBox(*editor);
    require(view&&box,"201 editor parts");
    auto rightClick=[&](juce::Point<float> at){
        view->mouseDown(mouseAt(*view,at,true));view->mouseUp(mouseAt(*view,at,true));tickUI();
    };
    auto drop=[&]{
        dynamic_cast<juce::FileDragAndDropTarget*>(editor.get())->filesDropped({dir.getChildFile("transparent-detail.png").getFullPathName()},0,0);tickUI();
    };
    auto boxCentre=[&]{return view->getLocalPoint(box,box->getLocalBounds().getCentre()).toFloat();};
    // 空工作区：右键什么也不做。
    rightClick({view->getWidth()*.5f,view->getHeight()*.4f});
    require(!box->hasImage()&&(!p.recording().edits()||p.recording().edits()->empty()),"201 right-click on an empty workspace does nothing");
    p.recording().setArmed(true);
    juce::AudioBuffer<float> source(2,48000*6);
    for(int c=0;c<2;++c)for(int i=0;i<source.getNumSamples();++i)source.setSample(c,i,.1f*std::sin(i*.017f));
    p.recording().capture(source,0,48000,true);p.recording().setArmed(false);
    require(p.recording().flush(),"201 source capture");tickUI();fitSpectrum(*view);tickUI();
    // 状态1：右键等同按打印按钮，不需要先点按钮。
    drop();require(box->hasImage(),"201 a drop becomes a placing object");
    box->setNormalisedBounds({.05f,.10f,.20f,.40f});tickUI();
    png(dir.getChildFile("201-placing.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    // 同样一张图压在亮频谱上：墨迹应该把频谱压暗而不是盖上白底。
    box->setNormalisedBounds({.05f,.86f,.20f,.12f});tickUI();
    png(dir.getChildFile("201-placing-bright.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    // 反转：墨迹改成形状之外，同样只压暗频谱。
    set(p,ParameterIDs::invert,1);tickUI();
    png(dir.getChildFile("201-placing-invert.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    set(p,ParameterIDs::invert,0);tickUI();
    box->setNormalisedBounds({.05f,.10f,.20f,.40f});tickUI();
    rightClick({view->getWidth()*.5f,view->getHeight()*.4f});
    require(p.recording().flush(),"201 right-click print render");
    auto edits=p.recording().edits();require(edits&&edits->size()==1,"201 right-click on a placing object prints it");
    require(!box->hasImage(),"201 a printed object gives up its source image");
    const auto earlyId=edits->front().id;
    // 第二个对象打印到更晚的位置，仍然一次右键完成。
    drop();box->setNormalisedBounds({.55f,.10f,.20f,.40f});tickUI();
    const auto centreB=boxCentre();
    rightClick({view->getWidth()*.5f,view->getHeight()*.4f});
    require(p.recording().flush(),"201 second right-click print render");
    edits=p.recording().edits();require(edits&&edits->size()==2,"201 a second right-click prints the second object");
    auto idAt=[&](bool latest){
        auto list=p.recording().edits();require(list&&!list->empty(),"201 prints exist");
        const PrintEdit* best=&list->front();
        for(const auto& e:*list)if(latest?e.start>best->start:e.start<best->start)best=&e;
        return best->id;
    };
    const auto lateId=idAt(true);require(earlyId!=lateId,"201 both prints keep distinct ids");
    // 未聚焦：右键聚焦离点击处最近的已打印图形。点在第二个对象上方一点、
    // 不在任何图形内部，证明选的是“最近”的矩形而不是命中测试。
    const auto justAboveB=juce::Point<float>(centreB.x,centreB.y-(float)box->getHeight()*.5f-40.f);
    rightClick(justAboveB);
    require(p.getEditorState().placementId==lateId,"201 right-click focuses the nearest printed object");
    require(!box->hasImage(),"201 a focused print shows no source image");
    // 状态3：右键把聚焦对象退回状态1的图片模式（聚焦时右键不挑位置）。
    rightClick({view->getWidth()*.5f,view->getHeight()*.4f});
    require(p.getEditorState().placementId.isEmpty(),"201 right-click drops the focus");
    require(box->hasImage()&&juce::File(box->getImagePath()).existsAsFile(),"201 focus revert restores the source image");
    edits=p.recording().edits();require(edits&&edits->size()==1&&edits->front().id==earlyId,"201 focus revert removes only that print");
    // 退回后把图片放到较早位置再右键：重新打印（状态1 → 状态2）。
    box->setNormalisedBounds({.05f,.10f,.20f,.40f});tickUI();
    fitSpectrum(*view);tickUI();
    const auto centreA=boxCentre();
    rightClick({view->getWidth()*.5f,view->getHeight()*.4f});
    require(p.recording().flush(),"201 reprint render");
    edits=p.recording().edits();require(edits&&edits->size()==2,"201 right-click prints the reverted image again");
    // 再点较早的对象：聚焦的是光标下的那个，而不是总取最新的。
    rightClick(centreA);
    require(p.getEditorState().placementId==idAt(false),"201 right-click focuses the object under the cursor");
    require(!box->hasImage()&&!box->isVisible(),"201 focusing hides the unprinted guide");
    png(dir.getChildFile("201-right-click-focus.png"),editor->createComponentSnapshot(editor->getLocalBounds()));
    std::cout<<"2.0.2 RIGHT-CLICK CHECKS PASSED; print / focus nearest / revert to image mode\n"<<std::flush;
}
