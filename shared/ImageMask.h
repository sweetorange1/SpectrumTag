#pragma once
#include <algorithm>
#include <cmath>
#include "FrequencyScale.h"
#include <JuceHeader.h>

// One conversion path for the preview, real-time Print and editor-closed export.
namespace spectrumtag::image
{
struct Settings {
    int mode = 0; float amount = 0.5f;
    // 灰度模式：输出连续 alpha（暗=强衰减），而不是二值阈值。
    // contrast/brightness 都是 0..1，0.5 为中性。
    bool grayscale = false; float contrast = 0.5f, brightness = 0.5f;
    bool operator==(const Settings& other) const {
        return mode==other.mode&&std::abs(amount-other.amount)<1.0e-6f
            &&grayscale==other.grayscale&&std::abs(contrast-other.contrast)<1.0e-6f
            &&std::abs(brightness-other.brightness)<1.0e-6f;
    }
};
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
    // 灰度：亮部保留频谱、暗部衰减；对比在 0.5 处为中性，两端为 0.25x/4x。
    const float grayOffset=(juce::jlimit(0.0f,1.0f,settings.brightness)-0.5f)*0.8f;
    const float grayGain=std::pow(4.0f,(juce::jlimit(0.0f,1.0f,settings.contrast)-0.5f)*2.0f);
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
        if(settings.grayscale) {
            const float l=juce::jlimit(0.0f,1.0f,luminance[i]+grayOffset);
            float ink=((1.0f-l)-0.5f)*grayGain+0.5f;
            // 纯黑必须扣净：不受对比/亮度影响，避免“看起来黑但没完全衰减”。
            if(luminance[i]<0.02f)ink=1.0f;
            dst.setPixelColour(x,y,juce::Colours::black.withAlpha(juce::jlimit(0.0f,1.0f,ink)));
            continue;
        }
        const bool inside=settings.mode==3 ? alpha[i]>juce::jlimit(0.001f,0.999f,1-settings.amount)
                                           : luminance[i]<=juce::jlimit(0.0f,0.9999f,t);
        dst.setPixelColour(x,y,inside?juce::Colours::black:juce::Colours::transparentBlack);
    }
    return mask;
}
// 彩色图片（照片/插画）更适合连续灰度；线稿和黑白图继续走二值。
inline bool hasColour(const juce::Image& original)
{
    if(!original.isValid())return false;
    const float scale=juce::jmin(1.0f,256.0f/juce::jmax(original.getWidth(),original.getHeight()));
    auto src=original.rescaled(juce::jmax(1,juce::roundToInt(original.getWidth()*scale)),
                               juce::jmax(1,juce::roundToInt(original.getHeight()*scale)),
                               juce::Graphics::mediumResamplingQuality);
    juce::Image::BitmapData pixels(src,juce::Image::BitmapData::readOnly);
    int chromatic=0,total=0;
    for(int y=0;y<src.getHeight();++y)for(int x=0;x<src.getWidth();++x){
        const auto c=pixels.getPixelColour(x,y);
        if(c.getFloatAlpha()<0.25f)continue;
        ++total;
        const float high=juce::jmax(c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue());
        const float low=juce::jmin(c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue());
        if(high-low>0.12f)++chromatic;
    }
    return total>0&&chromatic*4>=total; // ≥25% 的可见像素带明显色度
}
// 灰度模式的自动对比/亮度：按当前图片的亮度分布拟合端点，暗部偏重，
// 保证纯黑扣净、亮部保留。新图总是重新计算，不继承上一张的参数。
struct GrayTone { float contrast=0.5f, brightness=0.5f; };
inline GrayTone autoGrayTone(const juce::Image& original)
{
    GrayTone tone;
    if(!original.isValid()||original.getWidth()<2||original.getHeight()<2)return tone;
    const float scale=juce::jmin(1.0f,256.0f/juce::jmax(original.getWidth(),original.getHeight()));
    auto src=original.rescaled(juce::jmax(1,juce::roundToInt(original.getWidth()*scale)),
                               juce::jmax(1,juce::roundToInt(original.getHeight()*scale)),
                               juce::Graphics::mediumResamplingQuality);
    juce::Image::BitmapData pixels(src,juce::Image::BitmapData::readOnly);
    std::vector<float> luminance;
    luminance.reserve((size_t)src.getWidth()*src.getHeight());
    for(int y=0;y<src.getHeight();++y)for(int x=0;x<src.getWidth();++x){
        const auto c=pixels.getPixelColour(x,y);
        const float a=c.getFloatAlpha();
        if(a<0.25f)continue;
        // 与 binarize 相同：透明部分合成为白底。
        luminance.push_back(a*(0.299f*c.getFloatRed()+0.587f*c.getFloatGreen()+0.114f*c.getFloatBlue())+(1.0f-a));
    }
    if(luminance.size()<16)return tone;
    std::sort(luminance.begin(),luminance.end());
    const auto quantile=[&](float q){
        const int index=juce::jlimit(0,(int)luminance.size()-1,(int)std::lround(q*(float)(luminance.size()-1)));
        return luminance[(size_t)index];
    };
    // 暗部起点上移、亮部终点略降：默认更重，避免扣图不够清晰。
    float black=quantile(0.10f)+0.08f;
    float white=quantile(0.90f)-0.02f;
    if(white-black<0.18f){const float mid=(black+white)*0.5f;black=mid-0.09f;white=mid+0.09f;}
    black=juce::jlimit(0.0f,0.99f,black);
    white=juce::jlimit(black+0.05f,1.0f,white);
    const float offset=(1.0f-black-white)*0.5f;
    const float gain=1.0f/juce::jmax(0.05f,white-black);
    tone.brightness=juce::jlimit(0.35f,0.65f,0.5f+offset/0.8f);
    tone.contrast=juce::jlimit(0.55f,1.0f,0.5f+std::log2(gain)/4.0f);
    return tone;
}
inline juce::Image preview(const juce::Image& mask, bool invert, float ratio=0.0f, bool continuous=false)
{
    if(!mask.isValid()) return {};
    juce::Image result(juce::Image::RGB,mask.getWidth(),mask.getHeight(),false);
    juce::Image::BitmapData src(mask,juce::Image::BitmapData::readOnly), dst(result,juce::Image::BitmapData::writeOnly);
    ratio=juce::jlimit(0.0f,1.5f,ratio);
    const float reference=juce::jmax(1.0f,ratio);
    for(int y=0;y<mask.getHeight();++y) for(int x=0;x<mask.getWidth();++x) {
        const auto alpha=src.getPixelColour(x,y).getAlpha();
        const float ink=continuous?alpha/255.0f:(alpha>128?1.0f:0.0f);
        const float affected=invert?1.0f-ink:ink;
        const float gain=1.0f-affected+affected*ratio;
        const auto shade=(juce::uint8)juce::roundToInt(255.0f*std::sqrt(juce::jmax(0.0f,gain/reference)));
        dst.setPixelColour(x,y,juce::Colour(shade,shade,shade));
    }
    return result;
}
inline std::vector<float> rasterize(const juce::Image& mask,int rows,int cols,int scale=0,float low=0,float high=1,bool continuous=false)
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
            const auto alpha=pixels.getPixelColour(x,y).getAlpha();
            result[(size_t)r*cols+c]=continuous?alpha/255.0f:(alpha>128?1.0f:0.0f);
        }
    }
    return result;
}
}
