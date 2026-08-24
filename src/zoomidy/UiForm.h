#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "zoomidy/Config.h"

class Player;

namespace zoomidy::ui {

// Bounds of the sliders in the form. A slider hands its control a value clamped into its own
// range, and that clamped value is what gets read back on close -- so anything the form shows
// has to be seeded already clamped, or opening the form would quietly rewrite a config.json the
// user edited by hand.
//
// Fractional settings are carried as whole percents rather than as the fraction itself. Bedrock
// prints a slider's raw value, and a 0.05 step walks 0.05, 0.1, ... 1.5000000000000002 because
// neither the step nor the accumulated total is representable in binary floating point. Stepping
// over integers and dividing at the edge keeps the label readable and the value exact.
inline constexpr double kDurationMinMs   = 0.0;
inline constexpr double kDurationMaxMs   = 2000.0;
inline constexpr double kDurationStepMs  = 10.0;
inline constexpr double kScrollStepMinPc = 105.0;
inline constexpr double kScrollStepMaxPc = 200.0;
inline constexpr double kMultiplierMinPc = 5.0;
inline constexpr double kMultiplierMaxPc = 300.0;
inline constexpr double kCinematicMinPc  = 0.0;
inline constexpr double kCinematicMaxPc  = 95.0;
inline constexpr double kPercentStep     = 5.0;

/// One entry of a dropdown. Shared so that both form backends offer the same wording and, more
/// importantly, the same values: the legacy backend identifies a choice by its position in this
/// table, which only lines up with the modern backend's numeric value if there is one table.
struct Option {
    std::string_view label;
    double           value;
    std::string_view description; ///< Empty when the label says everything.
};

inline constexpr std::array kActivationOptions{
    Option{"Hold", static_cast<double>(ActivationMode::Hold), "Zoomed while the key is held down."},
    Option{"Toggle", static_cast<double>(ActivationMode::Toggle), "Each press flips the zoom on or off."},
};

inline constexpr std::array kCurveOptions{
    Option{"Linear", static_cast<double>(EasingCurve::Linear), ""},
    Option{"Ease out (quad)", static_cast<double>(EasingCurve::EaseOutQuad), ""},
    Option{"Ease in-out (quad)", static_cast<double>(EasingCurve::EaseInOutQuad), ""},
    Option{"Ease out (cubic)", static_cast<double>(EasingCurve::EaseOutCubic), ""},
    Option{"Ease in-out (cubic)", static_cast<double>(EasingCurve::EaseInOutCubic), ""},
    Option{"Ease out (expo)", static_cast<double>(EasingCurve::EaseOutExpo), ""},
};

inline constexpr std::array kSensitivityOptions{
    Option{"Off", static_cast<double>(SensitivityMode::Off), "Leave mouse look alone."},
    Option{
           "Relative", static_cast<double>(SensitivityMode::Relative),
           "Scale with the live magnification, so the world moves at the same apparent speed."},
    Option{
           "Fixed", static_cast<double>(SensitivityMode::Fixed),
           "Apply the multiplier below for as long as the zoom is active."},
};

/// Descriptions of the non-dropdown controls, shared for the same reason as the option tables.
inline constexpr std::string_view kKeyDescription =
    "A letter or digit (F, Z, 3), a named key (F5, SPACE, LSHIFT), or a virtual-key code (0x46).";
inline constexpr std::string_view kFactorDescription     = "How far in the zoom goes. 4 means 4x.";
inline constexpr std::string_view kDurationDescription   = "Time spent easing in, and again easing out. 0 snaps instantly.";
inline constexpr std::string_view kHideHandDescription   = "Hides the first-person arm and held item so they do not fill the view.";
inline constexpr std::string_view kMultiplierDescription =
    "100% leaves the mode's own result alone. Relative multiplies this on top; Fixed uses it by itself.";
inline constexpr std::string_view kCinematicDescription  = "Higher is heavier. 0% is no smoothing.";
inline constexpr std::string_view kScrollAdjustDescription =
    "While zoomed, the wheel changes the magnification instead of the hotbar.";
inline constexpr std::string_view kScrollStepDescription =
    "How much one notch multiplies the magnification by. 120% steps by a fifth each time.";
inline constexpr std::string_view kRememberDescription =
    "Keep the scrolled magnification for the next zoom instead of returning to the slider value.";

/// Every control's value, in the units the controls display them in, and free of any particular
/// form toolkit. The two backends differ only in how they get a `Values` in front of the player
/// and back again; everything that turns one into a `Config` is shared.
struct Values {
    std::string keyText;
    double      activation{};
    double      factor{};
    bool        scrollToAdjust{};
    double      scrollStepPercent{};
    bool        rememberScrolledFactor{};
    double      durationMillis{};
    double      curve{};
    bool        hideHand{};
    double      sensitivityMode{};
    double      sensitivityMultiplierPercent{};
    bool        cinematicEnabled{};
    double      cinematicStrengthPercent{};
};

/// The magnification slider's range, sanitised so a hand-edited config can never hand a slider a
/// minimum above its maximum.
[[nodiscard]] std::pair<double, double> factorRange(Config const& config);

/// A config as the controls should show it, clamped into the ranges they can represent.
[[nodiscard]] Values toValues(Config const& config);

/// The controls read back into a config. Anything typed into the key field that does not name a
/// key leaves the existing binding alone; `status` is set either way, and is worth showing.
[[nodiscard]] Config fromValues(Values const& values, Config const& previous, std::string& status);

/// Applies what the controls hold and returns the message about the key field.
std::string applyValues(Values const& values);

/// Applies a config wholesale. Used by "reset to defaults".
void applyConfig(Config const& config);

/// Builds and shows the settings form. Exactly one definition is compiled, chosen by which form
/// API the LeviLamina being built against provides.
void buildAndShow(Player& player);

} // namespace zoomidy::ui
