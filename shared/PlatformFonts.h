#pragma once
#include <JuceHeader.h>

namespace spectrumtag::ui {
// Resolve once on the UI thread; never enumerate OS fonts from audio callbacks.
inline const juce::String& chineseFontName()
{
    static const juce::String name = [] {
#if JUCE_MAC
        const juce::StringArray preferred { "PingFang SC", "Hiragino Sans GB", "Heiti SC", "Songti SC" };
#elif JUCE_WINDOWS
        const juce::StringArray preferred { "Microsoft YaHei", "Microsoft YaHei UI", "DengXian", "SimHei" };
#else
        const juce::StringArray preferred { "Noto Sans CJK SC", "Noto Sans SC", "WenQuanYi Micro Hei" };
#endif
        const auto installed = juce::Font::findAllTypefaceNames();
        for (const auto& candidate : preferred)
            if (installed.contains(candidate, true)) return candidate;
        return juce::Font::getDefaultSansSerifFontName();
    }();
    return name;
}
}
