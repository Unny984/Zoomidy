#pragma once

#include <cmath>

#include "zoomidy/Config.h"

namespace zoomidy::easing {

/// Maps a linear 0..1 parameter onto the selected curve. Every curve maps 0 to 0 and 1 to 1,
/// so the endpoints of the zoom are exact no matter which one is picked.
[[nodiscard]] inline double apply(EasingCurve curve, double t) {
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= 1.0) {
        return 1.0;
    }
    switch (curve) {
    case EasingCurve::Linear:
        return t;
    case EasingCurve::EaseOutQuad:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case EasingCurve::EaseInOutQuad:
        return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
    case EasingCurve::EaseOutCubic:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case EasingCurve::EaseInOutCubic:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case EasingCurve::EaseOutExpo:
        return 1.0 - std::pow(2.0, -10.0 * t);
    }
    return t;
}

} // namespace zoomidy::easing
