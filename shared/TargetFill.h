#pragma once
#include "Recording.h"

namespace spectrumtag {
// Magnitude-constrained noise synthesis. Four projections reduce random holes in
// bright pixels while keeping the original audio untouched. Work is bounded to
// four-second chunks with enough halo for every overlap/add projection.
template<class Read,class White,class Valid,class Cancel,class Progress>
bool targetFill(const PrintEdit& edit,double sr,int64_t origin,int64_t beginFrame,int64_t lastFrame,
                int64_t sliceStart,int64_t sliceEnd,const std::vector<float>& window,
                const std::vector<float>& norm,const std::vector<int>& rows,const std::vector<float>& edges,
                Read read,White white,Valid valid,Cancel cancel,Progress progress,std::vector<float>& result){
    constexpr int passes=4;
    const int N=edit.fftSize,hop=N/16,bins=N/2+1;
    const int64_t start=llround(edit.start*sr),end=llround((edit.start+edit.duration)*sr);
    const float floor=juce::Decibels::decibelsToGain(-66.f+42.f*edit.compensation);
    float energy=0;for(float v:window)energy+=v*v;
    const float floorEnergy=floor*floor*energy;
    juce::dsp::FFT fft((int)std::log2(N));std::vector<float> data(2*N),phase(2*N);
    const int64_t chunkSize=juce::jmax<int64_t>(N*8,(int64_t)(sr*4)/hop*hop);
    for(int64_t chunk=sliceStart;chunk<sliceEnd;chunk+=chunkSize){
        if(cancel())return false;
        const auto chunkEnd=juce::jmin(sliceEnd,chunk+chunkSize);
        const int64_t first=juce::jmax(beginFrame,(int64_t)std::floor((double)(chunk-passes*N-origin)/hop)*hop+origin);
        const int64_t last=juce::jmin(lastFrame,(int64_t)std::ceil((double)(chunkEnd+passes*N-origin)/hop)*hop+origin);
        const int frames=(int)juce::jmax<int64_t>(0,(last-first+hop-1)/hop);if(!frames)continue;
        const size_t length=(size_t)((frames-1)*hop+N);
        std::vector<float> target((size_t)frames*bins),previous(length),next(length);
        for(int pass=0;pass<passes;++pass){
            std::fill(next.begin(),next.end(),0.f);
            for(int f=0;f<frames;++f){
                if(cancel())return false;
                const int64_t frame=first+(int64_t)f*hop;const size_t at=(size_t)f*hop;
                std::fill(phase.begin(),phase.end(),0.f);
                for(int i=0;i<N;++i)phase[i]=(pass?previous[at+i]:white(frame+i))*window[i];
                fft.performRealOnlyForwardTransform(phase.data());
                if(pass==0){
                    std::fill(data.begin(),data.end(),0.f);for(int i=0;i<N;++i)data[i]=read(frame+i)*window[i];fft.performRealOnlyForwardTransform(data.data());
                    const int col=juce::jlimit(0,edit.cols-1,(int)((frame+N*.5-start)/(edit.duration*sr)*edit.cols));
                    for(int k=0;k<bins;++k)if(rows[k]>=0){const float mask=(*edit.mask)[(size_t)rows[k]*edit.cols+col],affected=edit.invert?1-mask:mask;
                        target[(size_t)f*bins+k]=std::sqrt(juce::jmax(0.f,floorEnergy-data[2*k]*data[2*k]-data[2*k+1]*data[2*k+1]))*(1-affected)*edges[k];}
                }
                for(int k=0;k<bins;++k){const float magnitude=std::hypot(phase[2*k],phase[2*k+1]);const float goal=target[(size_t)f*bins+k];
                    if(magnitude>1.e-20f){const float gain=goal/magnitude;phase[2*k]*=gain;phase[2*k+1]*=gain;}
                    else {phase[2*k]=goal;phase[2*k+1]=0;}}
                fft.performRealOnlyInverseTransform(phase.data());
                for(int i=0;i<N;++i)next[at+i]+=phase[i]*window[i]/norm[i%hop];
                progress((uint64_t)N*(pass==0?2:1));
            }
            for(size_t i=0;i<length;++i){const auto sample=first+(int64_t)i;if(sample<start||sample>=end||!valid(sample))next[i]=0;}
            previous.swap(next);
        }
        for(auto sample=chunk;sample<chunkEnd;++sample)if(sample>=first&&sample<first+(int64_t)length)result[(size_t)(sample-sliceStart)]=previous[(size_t)(sample-first)];
    }
    return true;
}
}
