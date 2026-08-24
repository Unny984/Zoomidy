/// The settings form as built against LeviLamina's pre-observable form API, for 26.10 and
/// earlier. `UiFormObservable.cpp` covers the newer SDKs; exactly one of the two compiles to
/// anything.
///
/// The older API is a different interaction model rather than a smaller version of the same one:
/// a form is filled in, submitted once, and read back as a map of results. There is no way to
/// hang a callback off an in-form button and no way to write to a control after the form has
/// been sent. Two things follow, and both are deliberate rather than oversights:
///
///   - "Apply" and "Reset to defaults" cannot be buttons. Apply becomes the submit button, and
///     the reset becomes a toggle that is honoured on submit.
///   - The key field cannot answer back inside the form, so whether the typed key was understood
///     is reported to the player in chat once the form closes.
#if !__has_include("ll/api/ui/form/CustomForm.h")

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "ll/api/form/CustomForm.h"

#include "mc/world/actor/player/Player.h"

#include "zoomidy/Config.h"
#include "zoomidy/UiForm.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::ui {

namespace {

/// Control names. Only ever used to pair an `append*` call with its entry in the result map, so
/// they never reach the player.
constexpr char kKey[]        = "key";
constexpr char kActivation[] = "activation";
constexpr char kFactor[]     = "factor";
constexpr char kDuration[]   = "duration";
constexpr char kCurve[]      = "curve";
constexpr char kHideHand[]   = "hideHand";
constexpr char kSensMode[]   = "sensMode";
constexpr char kSensMul[]    = "sensMul";
constexpr char kCineOn[]     = "cineOn";
constexpr char kCineAmount[] = "cineAmount";
constexpr char kScrollOn[]   = "scrollOn";
constexpr char kScrollStep[] = "scrollStep";
constexpr char kRemember[]   = "remember";
constexpr char kReset[]      = "reset";

/// The labels of an option table, which is what `appendDropdown` wants.
std::vector<std::string> labelsOf(std::span<Option const> options) {
    std::vector<std::string> labels;
    labels.reserve(options.size());
    for (auto const& option : options) {
        labels.emplace_back(option.label);
    }
    return labels;
}

/// The position of a value in an option table, which is what `appendDropdown` wants as its
/// default. Falls back to the first entry for a config holding an enumerator this build does not
/// know about.
size_t indexOfValue(std::span<Option const> options, double value) {
    for (size_t i = 0; i < options.size(); ++i) {
        if (options[i].value == value) {
            return i;
        }
    }
    return 0;
}

/// Every control's tooltip. The old API has one tooltip per control and no per-option text, so
/// the option descriptions are folded into the dropdown's own tooltip rather than dropped.
std::string tooltipOf(std::span<Option const> options) {
    std::string tooltip;
    for (auto const& option : options) {
        if (option.description.empty()) {
            continue;
        }
        if (!tooltip.empty()) {
            tooltip += '\n';
        }
        tooltip += std::string{option.label} + ": " + std::string{option.description};
    }
    return tooltip;
}

/// Reads one entry out of a result map.
///
/// The variant an element parses into is a property of the element, but which alternative a
/// given SDK picks is not something this file can check at compile time, so every reader accepts
/// whatever it is handed and converts. Anything missing or unreadable keeps the value the form
/// was seeded with, which is the existing config -- a control that cannot be read must not be
/// able to rewrite a setting.
ll::form::CustomFormElementResult const* find(ll::form::CustomFormResult const& result, std::string const& name) {
    if (!result) {
        return nullptr;
    }
    auto const it = result->find(name);
    return it == result->end() ? nullptr : &it->second;
}

double numberOf(ll::form::CustomFormResult const& result, std::string const& name, double fallback) {
    auto const* entry = find(result, name);
    if (!entry) {
        return fallback;
    }
    if (auto const* d = std::get_if<double>(entry)) {
        return *d;
    }
    if (auto const* u = std::get_if<uint64>(entry)) {
        return static_cast<double>(*u);
    }
    return fallback;
}

bool booleanOf(ll::form::CustomFormResult const& result, std::string const& name, bool fallback) {
    auto const* entry = find(result, name);
    if (!entry) {
        return fallback;
    }
    if (auto const* u = std::get_if<uint64>(entry)) {
        return *u != 0;
    }
    if (auto const* d = std::get_if<double>(entry)) {
        return *d != 0.0;
    }
    return fallback;
}

std::string stringOf(ll::form::CustomFormResult const& result, std::string const& name, std::string fallback) {
    auto const* entry = find(result, name);
    if (!entry) {
        return fallback;
    }
    if (auto const* s = std::get_if<std::string>(entry)) {
        return *s;
    }
    return fallback;
}

/// The value a dropdown selected.
///
/// A dropdown reports itself either by the chosen option's text or by its position, depending on
/// the SDK, so both are accepted rather than betting on one.
double optionOf(
    ll::form::CustomFormResult const& result,
    std::string const&                name,
    std::span<Option const>           options,
    double                            fallback
) {
    auto const* entry = find(result, name);
    if (!entry) {
        return fallback;
    }
    if (auto const* label = std::get_if<std::string>(entry)) {
        for (auto const& option : options) {
            if (option.label == *label) {
                return option.value;
            }
        }
        return fallback;
    }
    if (auto const* index = std::get_if<uint64>(entry)) {
        return *index < options.size() ? options[*index].value : fallback;
    }
    if (auto const* index = std::get_if<double>(entry)) {
        auto const i = static_cast<size_t>(*index);
        return i < options.size() ? options[i].value : fallback;
    }
    return fallback;
}

void onSubmit(Player& player, ll::form::CustomFormResult const& result) {
    // An empty result means the player closed the form rather than submitting it.
    if (!result) {
        return;
    }

    if (booleanOf(result, kReset, false)) {
        Config const defaults{};
        applyConfig(defaults);
        player.sendMessage("§eZoomidy settings reset to their defaults.");
        return;
    }

    Values const seeded = toValues(Zoomidy::getInstance().getConfig());

    Values values;
    values.keyText                = stringOf(result, kKey, seeded.keyText);
    values.activation             = optionOf(result, kActivation, kActivationOptions, seeded.activation);
    values.factor                 = numberOf(result, kFactor, seeded.factor);
    values.scrollToAdjust         = booleanOf(result, kScrollOn, seeded.scrollToAdjust);
    values.scrollStepPercent      = numberOf(result, kScrollStep, seeded.scrollStepPercent);
    values.rememberScrolledFactor = booleanOf(result, kRemember, seeded.rememberScrolledFactor);
    values.durationMillis         = numberOf(result, kDuration, seeded.durationMillis);
    values.curve                  = optionOf(result, kCurve, kCurveOptions, seeded.curve);
    values.hideHand               = booleanOf(result, kHideHand, seeded.hideHand);
    values.sensitivityMode        = optionOf(result, kSensMode, kSensitivityOptions, seeded.sensitivityMode);
    values.sensitivityMultiplierPercent = numberOf(result, kSensMul, seeded.sensitivityMultiplierPercent);
    values.cinematicEnabled             = booleanOf(result, kCineOn, seeded.cinematicEnabled);
    values.cinematicStrengthPercent     = numberOf(result, kCineAmount, seeded.cinematicStrengthPercent);

    // Stands in for the live label the newer API can offer: it is the only way to say whether
    // what was typed into the key field was understood.
    player.sendMessage(applyValues(values));
}

} // namespace

void buildAndShow(Player& player) {
    auto const& config                = Zoomidy::getInstance().getConfig();
    auto const [factorMin, factorMax] = factorRange(config);
    Values const values               = toValues(config);

    ll::form::CustomForm form;
    form.setTitle("Zoomidy");

    form.appendHeader("Zoom")
        .appendInput(kKey, "Zoom key", "F", values.keyText, std::string{kKeyDescription})
        .appendDropdown(
            kActivation,
            "Activation",
            labelsOf(kActivationOptions),
            indexOfValue(kActivationOptions, values.activation),
            tooltipOf(kActivationOptions)
        )
        .appendSlider(kFactor, "Magnification", factorMin, factorMax, 0.5, values.factor, std::string{kFactorDescription})
        .appendDivider();

    form.appendHeader("Animation")
        .appendSlider(
            kDuration,
            "Transition (ms)",
            kDurationMinMs,
            kDurationMaxMs,
            kDurationStepMs,
            values.durationMillis,
            std::string{kDurationDescription}
        )
        .appendDropdown(kCurve, "Curve", labelsOf(kCurveOptions), indexOfValue(kCurveOptions, values.curve))
        .appendDivider();

    form.appendHeader("View")
        .appendToggle(kHideHand, "Hide hand while zoomed", values.hideHand, std::string{kHideHandDescription})
        .appendDivider();

    form.appendHeader("Sensitivity")
        .appendDropdown(
            kSensMode,
            "Mode",
            labelsOf(kSensitivityOptions),
            indexOfValue(kSensitivityOptions, values.sensitivityMode),
            tooltipOf(kSensitivityOptions)
        )
        .appendSlider(
            kSensMul,
            "Multiplier (%)",
            kMultiplierMinPc,
            kMultiplierMaxPc,
            kPercentStep,
            values.sensitivityMultiplierPercent,
            std::string{kMultiplierDescription}
        )
        .appendDivider();

    form.appendHeader("Cinematic camera")
        .appendToggle(kCineOn, "Smooth the camera while zoomed", values.cinematicEnabled)
        .appendSlider(
            kCineAmount,
            "Smoothing (%)",
            kCinematicMinPc,
            kCinematicMaxPc,
            kPercentStep,
            values.cinematicStrengthPercent,
            std::string{kCinematicDescription}
        )
        .appendDivider();

    form.appendHeader("Scroll wheel")
        .appendToggle(
            kScrollOn,
            "Adjust magnification with the wheel",
            values.scrollToAdjust,
            std::string{kScrollAdjustDescription}
        )
        .appendSlider(
            kScrollStep,
            "Wheel step (%)",
            kScrollStepMinPc,
            kScrollStepMaxPc,
            kPercentStep,
            values.scrollStepPercent,
            std::string{kScrollStepDescription}
        )
        .appendToggle(
            kRemember,
            "Remember wheel adjustment",
            values.rememberScrolledFactor,
            std::string{kRememberDescription}
        )
        .appendDivider();

    // Stands in for the newer API's "Reset to defaults" button. Everything above is ignored when
    // this is on, which the tooltip has to say because the form cannot grey the controls out.
    form.appendHeader("Reset")
        .appendToggle(
            kReset,
            "Reset everything to defaults",
            false,
            "Applies the default settings and ignores everything else on this screen."
        );

    form.setSubmitButton("Apply");

    form.sendTo(player, [](Player& submitter, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        onSubmit(submitter, result);
    });
}

} // namespace zoomidy::ui

#endif
