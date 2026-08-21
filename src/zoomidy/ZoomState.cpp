#include "zoomidy/ZoomState.h"

#include <algorithm>
#include <cmath>

#include "zoomidy/Easing.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy {

namespace {

double clampFactor(double factor, ZoomSettings const& settings) {
    double const lo = std::max(1.0, settings.minFactor);
    double const hi = std::max(lo, settings.maxFactor);
    return std::clamp(factor, lo, hi);
}

} // namespace

ZoomState& ZoomState::getInstance() {
    static ZoomState instance;
    return instance;
}

void ZoomState::reanchorLocked(bool active) {
    mAnchorLinear = linearProgressLockedHelper();
    mAnchorTime   = std::chrono::steady_clock::now();
    mActive       = active;
}

// linearProgress() and reanchorLocked() need the same maths, but reanchorLocked() already holds
// the lock. Keep the shared arithmetic in one place and let both call it.
double ZoomState::linearProgressLockedHelper() const {
    double const duration = std::max(0.0, Zoomidy::getInstance().getConfig().animation.durationSeconds);
    if (duration <= 0.0) {
        return mActive ? 1.0 : 0.0;
    }
    auto const elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - mAnchorTime)
            .count();
    double const delta = elapsed / duration;
    return std::clamp(mActive ? mAnchorLinear + delta : mAnchorLinear - delta, 0.0, 1.0);
}

void ZoomState::setActive(bool active) {
    std::lock_guard lock{mMutex};
    if (mActive == active) {
        return;
    }
    reanchorLocked(active);

    if (active) {
        auto const& settings = Zoomidy::getInstance().getConfig().zoom;
        if (mActiveFactor <= 0.0 || !settings.rememberScrolledFactor) {
            mActiveFactor = clampFactor(settings.factor, settings);
        }
    }
}

void ZoomState::toggleActive() {
    bool next;
    {
        std::lock_guard lock{mMutex};
        next = !mActive;
    }
    setActive(next);
}

bool ZoomState::isActive() const {
    std::lock_guard lock{mMutex};
    return mActive;
}

double ZoomState::linearProgress() const {
    std::lock_guard lock{mMutex};
    return linearProgressLockedHelper();
}

double ZoomState::easedProgress() const {
    auto const& config = Zoomidy::getInstance().getConfig();
    return easing::apply(config.animation.curve, linearProgress());
}

bool ZoomState::isEngaged() const { return linearProgress() > 0.0; }

double ZoomState::currentDivisor() const {
    double const eased = easedProgress();
    if (eased <= 0.0) {
        return 1.0;
    }
    double const factor = activeFactor();
    if (factor <= 1.0) {
        return 1.0;
    }
    return std::pow(factor, eased);
}

double ZoomState::activeFactor() const {
    auto const& settings = Zoomidy::getInstance().getConfig().zoom;
    std::lock_guard lock{mMutex};
    return clampFactor(mActiveFactor > 0.0 ? mActiveFactor : settings.factor, settings);
}

void ZoomState::adjustFactorByScroll(int notches) {
    if (notches == 0) {
        return;
    }
    auto const& settings = Zoomidy::getInstance().getConfig().zoom;
    double const step    = std::max(1.01, settings.scrollStep);

    std::lock_guard lock{mMutex};
    double const current = mActiveFactor > 0.0 ? mActiveFactor : settings.factor;
    mActiveFactor        = clampFactor(current * std::pow(step, static_cast<double>(notches)), settings);
}

void ZoomState::reset() {
    std::lock_guard lock{mMutex};
    mActive       = false;
    mAnchorLinear = 0.0;
    mAnchorTime   = std::chrono::steady_clock::now();
    mActiveFactor = 0.0;
}

} // namespace zoomidy
