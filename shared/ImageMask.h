#pragma once
#include "FrequencyScale.h"
#include <JuceHeader.h>

// One conversion path for the preview, real-time Print and editor-closed export.
namespace spectrumtag::image
{
struct Settings { int mode = 0; float amount = 0.5f; };
inline juce::Image binarize(const juce::Image& original, Settings settings)
{
    if (!original.isValid()) return {};
    const float scale = juce::jmin(1.0f, 2048.0f / juce::jmax(original.getWidth(), original.getHeight()));
    auto src = original.rescaled(juce::jmax(1, juce::roundToInt(original.getWidth()*scale)),
                                juce::jmax(1, juce::roundToInt(original.getHeight()*scale)), juce::Graphics::highResamplingQuality);
    const int w=src.getWidth(), h=src.getHeight();
    std::vector<float> luminance((size_t)w*h), alpha((size_t)w*h);
    std::array<int,256> histogram{};
    juce::Image::BitmapData pixels(src,juce::Image::BitmapData::readOnly);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto c=pixels.getPixelColour(x,y); const float a=c.getFloatAlpha();
        // Perceptual RGB luminance, composited onto white, preserves interior detail in transparent PNGs.
        const float l=a*(0.2126f*c.getFloatRed()+0.7152f*c.getFloatGreen()+0.0722f*c.getFloatBlue())+(1-a);
        luminance[(size_t)y*w+x]=l; alpha[(size_t)y*w+x]=a;
        ++histogram[(size_t)juce::jlimit(0,255,juce::roundToInt(l*255))];
    }
    double sum=0, backgroundSum=0, best=-1; int count=0, threshold=127, bestEnd=127;
    for(int i=0;i<256;++i) sum+=i*(double)histogram[i];
    for(int i=0;i<255;++i) {
        count+=histogram[i]; backgroundSum+=i*(double)histogram[i];
        const int foreground=w*h-count; if(count==0||foreground==0) continue;
        const double d=backgroundSum/count-(sum-backgroundSum)/foreground;
        const double score=(double)count*foreground*d*d;
        if(score>best+1e-6) {best=score;threshold=bestEnd=i;}
        else if(std::abs(score-best)<1e-6) bestEnd=i;
    }
    // Centre an Otsu plateau instead of favouring its darkest edge.
    const float automatic=(threshold+bestEnd+1.0f)/510.0f;
    const float bias=(settings.amount-0.5f)*0.6f;
    std::vector<double> integral, squares;
    const int stride=w+1;
    if(settings.mode==1) {
        integral.assign((size_t)(w+1)*(h+1),0); squares=integral;
        for(int y=0;y<h;++y) {double s=0,q=0; for(int x=0;x<w;++x) {
            const double l=luminance[(size_t)y*w+x]; s+=l;q+=l*l;
            integral[(size_t)(y+1)*stride+x+1]=integral[(size_t)y*stride+x+1]+s;
            squares[(size_t)(y+1)*stride+x+1]=squares[(size_t)y*stride+x+1]+q;
        }}
    }
    juce::Image mask(juce::Image::ARGB,w,h,true);
    juce::Image::BitmapData dst(mask,juce::Image::BitmapData::writeOnly);
    const int radius=juce::jlimit(7,63,juce::jmin(w,h)/16);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        float t=settings.mode==2?settings.amount:automatic+bias;
        if(settings.mode==1) {
            int x0=juce::jmax(0,x-radius),x1=juce::jmin(w,x+radius+1);
            int y0=juce::jmax(0,y-radius),y1=juce::jmin(h,y+radius+1);
            auto area=[&](const std::vector<double>& v) {return v[(size_t)y1*stride+x1]-v[(size_t)y0*stride+x1]-v[(size_t)y1*stride+x0]+v[(size_t)y0*stride+x0];};
            const double n=(x1-x0)*(y1-y0),mean=area(integral)/n;
            const double sd=std::sqrt(juce::jmax(0.0,area(squares)/n-mean*mean));
            t=(float)(mean*(1+0.22*(sd/0.5-1)))+bias; // Sauvola, R=0.5 on [0,1]
        }
        const size_t i=(size_t)y*w+x;
        const bool inside=settings.mode==3 ? alpha[i]>juce::jlimit(0.001f,0.999f,1-settings.amount)
                                           : luminance[i]<=juce::jlimit(0.0f,0.9999f,t);
        dst.setPixelColour(x,y,inside?juce::Colours::black:juce::Colours::transparentBlack);
    }
    return mask;
}
inline juce::Image preview(const juce::Image& mask, bool invert, float ratio=0.0f)
{
    if(!mask.isValid()) return {};
    juce::Image result(juce::Image::RGB,mask.getWidth(),mask.getHeight(),false);
    juce::Image::BitmapData src(mask,juce::Image::BitmapData::readOnly), dst(result,juce::Image::BitmapData::writeOnly);
    ratio=juce::jlimit(0.0f,1.5f,ratio);
    const float reference=juce::jmax(1.0f,ratio);
    for(int y=0;y<mask.getHeight();++y) for(int x=0;x<mask.getWidth();++x) {
        const bool affected=(src.getPixelColour(x,y).getAlpha()>128)!=invert;
        const float gain=affected?ratio:1.0f;
        const auto shade=(juce::uint8)juce::roundToInt(255.0f*std::sqrt(gain/reference));
        dst.setPixelColour(x,y,juce::Colour(shade,shade,shade));
    }
    return result;
}
inline std::vector<float> rasterize(const juce::Image& mask,int rows,int cols,int scale=0,float low=0,float high=1)
{
    if(rows<=0||cols<=0) return {};
    std::vector<float> result((size_t)rows*cols,0);
    if(!mask.isValid()) return result;
    const int w=mask.getWidth(),h=mask.getHeight();
    juce::Image::BitmapData pixels(mask,juce::Image::BitmapData::readOnly);
    for(int r=0;r<rows;++r) {
        float t=rows>1?(float)r/(rows-1):0;
        if(scale!=0&&high>low)t=frequency::fraction(low+t*(high-low),low,high,scale);
        const int y=juce::jlimit(0,h-1,juce::roundToInt((1-t)*(h-1)));
        for(int c=0;c<cols;++c) {
            const int x=cols>1?juce::roundToInt((float)c/(cols-1)*(w-1)):0;
            result[(size_t)r*cols+c]=pixels.getPixelColour(x,y).getAlpha()>128?1.0f:0.0f;
        }
    }
    return result;
}
}
