#pragma once
#include "Recording.h"
#include "FrequencyScale.h"

namespace spectrumtag {
// A fast expected-power preview, not an audio render. Always start from the
// immutable original spectrum: reducing Fill must remove the previous noise.
// STFT phase, overlap/window leakage and stereo correlation are approximated;
// the exact rendered spectrum replaces this image after the gesture finishes.
inline juce::Image printPreview(const RecordingSnapshot& source,const PrintEdit& edit,int palette) {
    if(!edit.valid()||source.empty())return {};
    const int w=juce::jmin(512,edit.cols),h=juce::jmin(512,edit.rows);
    const float nyquist=(float)source.sampleRate*.5f;
    std::vector<double> times((size_t)w);
    std::vector<float> frequencies((size_t)h);
    std::vector<const SpectrumFrame*> columns((size_t)w);
    for(int x=0;x<w;++x){times[x]=edit.start+(x+.5)*edit.duration/w;
        if(source.contains(llround(times[x]*source.sampleRate)))columns[x]=source.frameAt(times[x],false);
    }
    for(int y=0;y<h;++y)frequencies[y]=frequency::interpolate(1.f-(float)y/juce::jmax(1,h-1),edit.low*nyquist,edit.high*nyquist,edit.scaleMode)/nyquist;
    struct Layer {
        const PrintEdit* edit=nullptr;float floorPower=0;
        std::vector<int> rows,cols;
        std::vector<float> edges,envelopes;
    };
    std::vector<Layer> layers;
    auto add=[&](const PrintEdit& e){
        if(!e.valid()||e.start>=edit.start+edit.duration||e.start+e.duration<=edit.start||e.low>=edit.high||e.high<=edit.low)return;
        Layer layer;layer.edit=&e;
        layer.floorPower=e.compensation>0?std::pow(10.f,(-66.f+42.f*e.compensation)/10.f):0;
        layer.rows.resize(h,-1);layer.cols.resize(w,-1);layer.edges.resize(h);layer.envelopes.resize(w);
        for(int y=0;y<h;++y){const float f=frequencies[y];if(f<e.low||f>e.high)continue;
            layer.rows[y]=juce::jlimit(0,e.rows-1,juce::roundToInt((f-e.low)/(e.high-e.low)*(e.rows-1)));
            const float edge=juce::jlimit(0.f,1.f,juce::jmin(f-e.low,e.high-f)*e.fftSize*.25f);
            layer.edges[y]=edge*edge;
        }
        const int64_t start=llround(e.start*source.sampleRate),end=llround((e.start+e.duration)*source.sampleRate);
        const double fade=juce::jmax(1.,juce::jmin(source.sampleRate*.005,(end-start)*.1));
        for(int x=0;x<w;++x){const auto at=llround(times[x]*source.sampleRate);if(at<start||at>=end)continue;
            layer.cols[x]=juce::jlimit(0,e.cols-1,(int)((times[x]-e.start)/e.duration*e.cols));
            const float envelope=std::sin(juce::jlimit(0.f,1.f,(float)(juce::jmin(at-start,end-1-at)/fade))*juce::MathConstants<float>::halfPi);
            layer.envelopes[x]=envelope*envelope;
        }
        layers.push_back(std::move(layer));
    };
    bool replaced=false;
    for(const auto& existing:source.edits){if(existing.id==edit.id){add(edit);replaced=true;}else add(existing);}
    if(!replaced)add(edit);
    // LUTs keep palette conversion and exponentials out of the pixel loop.
    static const auto powers=[] {std::array<float,256> values{};for(int i=1;i<256;++i)values[i]=std::pow(10.f,(i*96.f/255.f-96.f)/10.f);return values;}();
    std::array<juce::PixelRGB,256> colours;
    for(int i=0;i<256;++i){auto c=spectrumColour(palette,i/255.f);colours[i].setARGB(255,c.getRed(),c.getGreen(),c.getBlue());}
    // macOS native RGB images are actually ARGB. Keep this CPU-side preview in
    // packed software RGB so PixelRGB stores cannot corrupt alpha or neighbours.
    juce::Image image(juce::Image::RGB,w,h,false,juce::SoftwareImageType{});juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
    for(int y=0;y<h;++y){
        auto* row=reinterpret_cast<juce::PixelRGB*>(pixels.getLinePointer(y));
        for(int x=0;x<w;++x){const auto* frame=columns[x];float energy=0;
            if(frame&&frame->bins>1){
                const float bin=juce::jlimit(0.f,(float)frame->bins-1,frequencies[y]*(frame->bins-1));
                const int lo=(int)bin,hi=juce::jmin(frame->bins-1,lo+1);
                const int level=juce::jlimit(0,255,juce::roundToInt(frame->level[lo]+(bin-lo)*(frame->level[hi]-frame->level[lo])));
                energy=powers[level];
                for(const auto& layer:layers){const int r=layer.rows[y],c=layer.cols[x];if(r<0||c<0)continue;
                    const auto& e=*layer.edit;const float mask=(*e.mask)[(size_t)r*e.cols+c],affected=e.invert?1-mask:mask;
                    const float gain=1-affected+affected*e.ratio;
                    // Display frames are channel averages. Independent noise
                    // power falls by channel count in that average.
                    const float fill=juce::jmax(0.f,layer.floorPower/(e.fillVersion==0?juce::jmax(1,source.channels):1)-energy);
                    energy=energy*gain*gain+fill*(1-affected)*(1-affected)*layer.edges[y]*layer.envelopes[x];
                }
            }
            const int level=energy>0?juce::jlimit(0,255,juce::roundToInt((10.f*std::log10(energy)+96.f)*255.f/96.f)):0;
            row[x]=colours[level];
        }
    }
    return image;
}
}
