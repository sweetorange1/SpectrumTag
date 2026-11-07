#pragma once
#include "FrequencyScale.h"

namespace spectrumtag::timing {
// All frequency modes use this one logical-pixel clock. UI scale is a transform
// of the fixed canvas, so it does not affect audio time.
constexpr double pixelsPerSecond = 60.0;
inline double presetSpeed(int mode, int plotHeight) {
    return std::max(1, plotHeight) * frequency::referenceAspect(mode)
        / frequency::referenceSeconds(mode) / pixelsPerSecond;
}
// Only for loading 1.4.9. Keep the parameter's existing automation range;
// carry any out-of-range remainder in the already explicit local view zoom.
inline void migrateLegacy(int mode, int plotHeight, float& speed, double& zoom) {
    if (mode != frequency::insight && mode != frequency::superVision) return;
    const double value = speed * presetSpeed(mode, plotHeight);
    const float converted = (float) std::clamp(value, 0.1, 4.0);
    zoom *= value / converted;
    speed = converted;
}
}
