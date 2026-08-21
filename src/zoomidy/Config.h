#pragma once

namespace zoomidy {

/// Interpolation curve used while easing between the normal FOV and the zoomed FOV.
enum class EasingCurve : int {
    Linear,
    EaseOutQuad,
    EaseInOutQuad,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutExpo,
};

/// How the mouse look sensitivity reacts to being zoomed in.
enum class SensitivityMode : int {
    Off,      ///< Leave the sensitivity alone.
    Relative, ///< Divide by the live zoom amount, so a 4x zoom means 1/4 the turn speed.
    Fixed,    ///< Multiply by `multiplier` for as long as the zoom is active.
};

/// How the zoom key behaves.
enum class ActivationMode : int {
    Hold,   ///< Zoomed in for as long as the key is held. Zoomify's default.
    Toggle, ///< Each press flips the zoom on or off.
};

struct ZoomSettings {
    /// Windows virtual-key code of the zoom key. 0x46 is `F`.
    ///
    /// Stored here rather than in Minecraft's own keybind list because the vanilla list is only
    /// read when the input handler is built, which would mean a restart after every change.
    int keyCode = 0x46;

    ActivationMode activation = ActivationMode::Hold;

    /// Base magnification. The rendered FOV is divided by this, so 4.0 means "4x".
    double factor = 4.0;

    double minFactor = 1.5;
    double maxFactor = 50.0;

    /// Let the scroll wheel change the magnification while zoomed in.
    bool scrollToAdjust = true;

    /// How much one wheel notch changes the magnification, as a ratio.
    /// 1.2 means each notch multiplies/divides the magnification by 1.2.
    double scrollStep = 1.2;

    /// Keep a scroll-wheel adjustment for the next zoom instead of snapping back to `factor`.
    bool rememberScrolledFactor = false;
};

struct AnimationSettings {
    /// Seconds spent easing in, and again easing out. 0 disables the animation.
    double durationSeconds = 0.2;

    EasingCurve curve = EasingCurve::EaseOutQuad;
};

struct ViewSettings {
    /// Hide the first-person hand and held item while zoomed in.
    bool hideHand = true;
};

struct SensitivitySettings {
    SensitivityMode mode = SensitivityMode::Relative;

    /// Extra factor applied on top of `mode`. 1.0 leaves the mode's result untouched.
    double multiplier = 1.0;
};

struct CinematicSettings {
    /// Smooth the camera while zoomed in, like the Java "cinematic camera".
    bool enabled = false;

    /// 0 is no smoothing, values approaching 1 are very heavy. Clamped to [0, 0.95].
    double strength = 0.6;
};

struct Config {
    int version = 1;

    ZoomSettings        zoom{};
    AnimationSettings   animation{};
    ViewSettings        view{};
    SensitivitySettings sensitivity{};
    CinematicSettings   cinematic{};
};

} // namespace zoomidy
