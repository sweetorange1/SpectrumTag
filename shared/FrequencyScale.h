#pragma once
#include <algorithm>
#include <cmath>

namespace spectrumtag::frequency {
// Stored Print modes are stable: 0 linear, 1 logarithmic, 2 insight, 3 retired Super Vision (legacy read only), 4 Mel, 5 Bark, 6 Extended Log.
// insight uses the HTK Mel shape log(1 + Hz / 700). The Mel multiplier
// cancels in normalised coordinates. Calibrated against the supplied Insight
// spectrogram ruler; iZotope's public Insight manual does not specify a formula.
constexpr int linear=0, logarithmic=1, insight=2, superVision=3, mel=4, bark=5, extendedLog=6;
// Reference Super Vision plot: 1186 x 560 pixels; Duration = 5 s.
constexpr double superVisionAspect=1186.0/560.0, superVisionSeconds=5.0;
// Insight reference: 840 x 528 plot, approximately 18 s from its 17..1 s ruler.
constexpr double insightAspect=840.0/528.0, insightSeconds=18.0;
inline double referenceAspect(int scale){return scale==insight?insightAspect:superVisionAspect;}
inline double referenceSeconds(int scale){return scale==insight?insightSeconds:superVisionSeconds;}
inline float minimum(int scale){return scale==mel||scale==bark?0.f:scale==extendedLog?10.f:20.f;}
inline float fraction(float hz,float low,float high,int scale) {
    if(high<=low)return 0;
    hz=std::clamp(hz,low,high);
    // Praat's documented BarkSpectrogram mapping; factor 7 cancels here.
    if(scale==bark){const auto lo=std::asinh(low/650.f);return (std::asinh(hz/650.f)-lo)/(std::asinh(high/650.f)-lo);}
    if((scale==logarithmic||scale==superVision||scale==extendedLog)&&low>0)return std::log(hz/low)/std::log(high/low);
    if(scale==insight||scale==mel)return std::log1p((hz-low)/(700.f+low))/std::log1p((high-low)/(700.f+low));
    return (hz-low)/(high-low);
}
inline float interpolate(float t,float low,float high,int scale) {
    t=std::clamp(t,0.f,1.f);
    if(scale==bark){const auto lo=std::asinh(low/650.f);return 650.f*std::sinh(lo+t*(std::asinh(high/650.f)-lo));}
    if((scale==logarithmic||scale==superVision||scale==extendedLog)&&low>0)return std::exp(std::log(low)+t*std::log(high/low));
    if(scale==insight||scale==mel)return low+(700.f+low)*std::expm1(t*std::log1p((high-low)/(700.f+low)));
    return low+t*(high-low);
}
inline float toY(float hz,float top,int scale) {return 1.f-fraction(hz,minimum(scale),std::max(21.f,top),scale);}
inline float fromY(float y,float top,int scale) {return interpolate(1.f-y,minimum(scale),std::max(21.f,top),scale);}
}
