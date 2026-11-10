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
        // Prefer a heavy grotesque so Chinese text sits at the same visual weight
        // as the bundled Basement Grotesque Black used for English.
        const juce::StringArray preferred { "Noto Sans SC", "Source Han Sans SC", "思源黑体",
                                            "HarmonyOS Sans SC", "MiSans",
                                            "Microsoft YaHei UI", "Microsoft YaHei",
                                            "DengXian", "SimHei" };
#else
        const juce::StringArray preferred { "Noto Sans CJK SC", "Noto Sans SC", "Source Han Sans SC",
                                            "WenQuanYi Micro Hei" };
#endif
        const auto installed = juce::Font::findAllTypefaceNames();
        for (const auto& candidate : preferred)
            if (installed.contains (candidate, true)) return candidate;
        return juce::Font::getDefaultSansSerifFontName();
    }();
    return name;
}

// Heaviest available face of the selected family. English style names cover most
// fonts; the Chinese aliases cover localized Microsoft YaHei installs.
inline const juce::String& chineseFontStyle()
{
    static const juce::String style = [] {
        const juce::StringArray preferred { "Black", "Heavy", "ExtraBold", "Extra Bold",
                                            "Bold", "Semibold", "SemiBold", "Medium",
                                            juce::String::fromUTF8 ("粗体"),
                                            juce::String::fromUTF8 ("中等") };
        const auto styles = juce::Font::findAllTypefaceStyles (chineseFontName());
        for (const auto& candidate : preferred)
            if (styles.contains (candidate, true)) return candidate;
        return juce::String();
    }();
    return style;
}

// One shared heavy typeface for every Chinese string, resolved on first use.
inline juce::Typeface::Ptr chineseTypeface()
{
    static auto typeface = []() -> juce::Typeface::Ptr {
        const auto& style = chineseFontStyle();
        if (style.isNotEmpty())
            return juce::Font (chineseFontName(), style, 18.0f).getTypefacePtr();

        // No recognizable style name (localized face names): let the platform
        // match the bold face instead of falling back to Regular.
        return juce::Font (juce::FontOptions (chineseFontName(), 18.0f, juce::Font::bold))
                   .getTypefacePtr();
    }();
    return typeface;
}

// Optical size compensation, measured by rasterising both faces at 100 px:
//   Basement Grotesque Black : cap "H" 66 px, "x" 51 px, "Range:" line 84 px
//   Noto Sans SC Black       : "国" 64 px, "显示上限" 66 px
// The ideograph box matches the Latin cap height, but Chinese has no ascender
// or descender and a string is far narrower, so it reads smaller beside the wide
// black Latin face. +15 % makes both scripts occupy the same optical size.
inline constexpr float chineseScale = 1.15f;

inline juce::Font chineseFont (float height)
{
    return juce::Font (chineseTypeface()).withHeight (height * chineseScale);
}
}
