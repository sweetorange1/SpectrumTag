#pragma once
static void timelineChecks(const juce::File& dir){
    using namespace spectrumtag;
    // Three takes deliberately arrive out of chronological order, as when the
    // user jumps between DAW events. Never concatenate across their gaps.
    RecordingEngine recording;recording.setArmed(true);
    auto capture=[&](double start,int length,float value){
        const int counts[]={31,256,127,1024,7};int at=0,iteration=0;
        while(at<length){const int count=juce::jmin(counts[iteration++%5],length-at);juce::AudioBuffer<float> audio(2,count);for(int c=0;c<2;++c)for(int i=0;i<count;++i)audio.setSample(c,i,value+(float)(at+i)*1e-7f);recording.capture(audio,start+at/48000.0,48000);at+=count;}
    };
    capture(10,48000,.1f);capture(14,24000,.3f);require(recording.flush(),"separated clips captured");
    auto two=recording.snapshot();require(two&&two->start()==10&&two->end()==14.5,"two clips retain absolute timeline positions");
    require(!two->contains(12*48000)&&two->sample(0,12*48000)==0,"unrecorded two-second gap is not collapsed");
    require(std::abs(two->sample(0,14*48000+37)-(.3f+37e-7f))<1e-7,"second clip begins at exact DAW sample");
    capture(8.25,24000,.5f);require(recording.flush(),"backward capture prepend");auto three=recording.snapshot();
    require(three->start()==8.25&&three->end()==14.5,"earlier third clip extends timeline without shifting later clips");
    require(std::abs(three->sample(0,10*48000+41)-(.1f+41e-7f))<1e-7,"first clip survives prepend at original position");
    require(std::abs(three->sample(0,14*48000+73)-(.3f+73e-7f))<1e-7,"second clip survives prepend at original position");
    require(!three->contains(9*48000)&&!three->contains(13*48000),"all gaps remain at host positions");
    // A pass over actual silence is captured at its host location, not appended.
    juce::AudioBuffer<float> gap(2,48000);gap.clear();recording.capture(gap,12,48000);require(recording.flush(),"silent region capture");auto withSilence=recording.snapshot();
    require(withSilence->contains(12*48000)&&withSilence->sample(0,12*48000)==0&&withSilence->end()==14.5,"played DAW gap keeps timeline alignment");
    capture(10.25,4096,-.2f);require(recording.flush(),"overdub same position");auto revised=recording.snapshot();
    require(revised->sample(0,(int64_t)(10.25*48000))==-.2f&&revised->end()==14.5,"record over existing time overwrites in place without appending");
    recording.setArmed(false);const auto file=recording.save();RecordingEngine loaded;loaded.restore(file,true);require(loaded.flush(),"multi-clip state recall");auto recalled=loaded.snapshot();require(recalled&&recalled->first==revised->first&&recalled->last==revised->last,"project recalls complete timeline span");
    for(int64_t i=revised->first;i<revised->last;i+=113)require(recalled->sample(0,i)==revised->sample(0,i)&&recalled->contains(i)==revised->contains(i),"project recalls gap coverage and audio positions");
    juce::AudioBuffer<float> importBuffer(2,44100);importBuffer.clear();importBuffer.setSample(0,22050,.5f);wav(dir.getChildFile("44100-import.wav"),importBuffer,44100);
    loaded.importAudio(dir.getChildFile("44100-import.wav"),4,48000);require(loaded.flush(),"different-rate import");auto converted=loaded.snapshot();
    require(converted&&converted->sampleRate==48000&&converted->first==192000&&converted->last==240000,"import resamples to host rate without changing one-second duration");
    int64_t peak=0;float magnitude=0;for(int64_t i=converted->first;i<converted->last;++i)if(std::abs(converted->sample(0,i))>magnitude){magnitude=std::abs(converted->sample(0,i));peak=i;}
    require(std::abs(peak-216000)<=4,"resampled impulse stays at matching DAW time");
    std::cout<<"TIMELINE CHECKS PASSED; three clips, gaps, backward capture, overwrite, recall, 44.1-to-48 kHz import\n";
}
