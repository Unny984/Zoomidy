#pragma once

#include <chrono>
#include <mutex>

namespace zoomidy {

/// Drives the zoom animation and owns the live magnification.
///
/// The animation is stored as an anchor (a linear 0..1 parameter plus the timestamp it was
/// captured at) rather than as a value that something has to tick. Any thread can therefore ask
/// for the current amount at any moment and get the same answer, which matters because the FOV
/// hook runs on the render thread while the key handlers run on the client thread.
class ZoomState {
public:
    static ZoomState& getInstance();

    /// Hold-mode key down / key up, or the result of a toggle-mode press.
    void setActive(bool active);

    /// Toggle-mode key press.
    void toggleActive();

    [[nodiscard]] bool isActive() const;

    /// Linear 0..1 parameter before easing. 0 is fully zoomed out, 1 is fully zoomed in.
    [[nodiscard]] double linearProgress() const;

    /// Eased 0..1 parameter. This is what the FOV and the sensitivity are derived from.
    [[nodiscard]] double easedProgress() const;

    /// True while any part of the zoom is visible, including the ease-out tail.
    [[nodiscard]] bool isEngaged() const;

    /// The value the rendered FOV is divided by. 1.0 while fully zoomed out.
    ///
    /// Interpolation is geometric (`factor ^ eased`) rather than linear, because perceived zoom
    /// is logarithmic: going 1x -> 2x looks like the same amount of movement as 2x -> 4x. A
    /// linear ramp spends most of its time in the barely-zoomed range and then lurches.
    [[nodiscard]] double currentDivisor() const;

    /// The magnification the current zoom is heading towards, after any scroll adjustment.
    [[nodiscard]] double activeFactor() const;

    /// Applies `notches` of scroll wheel to the magnification. Positive scrolls in.
    void adjustFactorByScroll(int notches);

    /// Drops any scroll adjustment and any in-flight animation. Used when the config changes.
    void reset();

private:
    ZoomState() = default;

    /// Recomputes the anchor so the animation continues from wherever it currently is.
    /// Caller must hold `mMutex`.
    void reanchorLocked(bool active);

    /// The linear parameter derived from the current anchor. Caller must hold `mMutex`.
    [[nodiscard]] double linearProgressLockedHelper() const;

    mutable std::mutex                    mMutex;
    bool                                  mActive       = false;
    double                                mAnchorLinear = 0.0;
    std::chrono::steady_clock::time_point mAnchorTime{};
    double                                mActiveFactor = 0.0; ///< 0 means "not set, use the config value".
};

} // namespace zoomidy
