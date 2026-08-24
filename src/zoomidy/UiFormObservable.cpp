/// The settings form as built against LeviLamina's observable-backed UI API (26.20 and later).
///
/// Controls are bound to observables that stay live for as long as the form is on screen. That is
/// what lets every control save itself the moment it is touched -- there is no Apply button and
/// nothing to scroll to the bottom for -- and what lets "Reset to defaults" visibly repopulate the
/// form and the key field answer back. `UiFormLegacy.cpp` covers the SDKs that predate this API,
/// where none of that is possible; exactly one of the two compiles to anything.
#if __has_include("ll/api/ui/form/CustomForm.h")

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/ui/base/Observable.h"
#include "ll/api/ui/form/CustomForm.h"

#include "mc/world/actor/player/Player.h"

#include "zoomidy/Config.h"
#include "zoomidy/UiForm.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::ui {

namespace {

constexpr ll::ui::ObservableOptions kWritable{.clientWritable = true};

/// Every control's live value. Held in a shared_ptr so the button callbacks and the completion
/// callback can all read the same state after the form has left the calling stack frame.
struct FormState {
    ll::ui::ObservableString  keyText{std::string{}, kWritable};
    ll::ui::ObservableNumber  activation{0.0, kWritable};
    ll::ui::ObservableNumber  factor{1.0, kWritable};
    ll::ui::ObservableBoolean scrollToAdjust{false, kWritable};
    ll::ui::ObservableNumber  scrollStepPercent{kScrollStepMinPc, kWritable};
    ll::ui::ObservableBoolean rememberScrolledFactor{false, kWritable};
    ll::ui::ObservableNumber  durationMillis{0.0, kWritable};
    ll::ui::ObservableNumber  curve{0.0, kWritable};
    ll::ui::ObservableBoolean hideHand{false, kWritable};
    ll::ui::ObservableNumber  sensitivityMode{0.0, kWritable};
    ll::ui::ObservableNumber  sensitivityMultiplierPercent{100.0, kWritable};
    ll::ui::ObservableBoolean cinematicEnabled{false, kWritable};
    ll::ui::ObservableNumber  cinematicStrengthPercent{0.0, kWritable};
    ll::ui::ObservableString  keyStatus{std::string{}};

    /// Set while `seed` is writing the controls, so the auto-save subscriptions can tell a value
    /// the player changed from one the form put there itself.
    bool seeding{false};
};

/// Pushes a config into the controls. Used to build the form and again by "Reset to defaults",
/// so that the reset is visible immediately *and* survives the form being closed -- the close
/// handler reads these same observables back out.
void seed(FormState& state, Config const& config) {
    Values const values = toValues(config);

    state.seeding = true;
    state.keyText.setData(values.keyText);
    state.activation.setData(values.activation);
    state.factor.setData(values.factor);
    state.scrollToAdjust.setData(values.scrollToAdjust);
    state.scrollStepPercent.setData(values.scrollStepPercent);
    state.rememberScrolledFactor.setData(values.rememberScrolledFactor);
    state.durationMillis.setData(values.durationMillis);
    state.curve.setData(values.curve);
    state.hideHand.setData(values.hideHand);
    state.sensitivityMode.setData(values.sensitivityMode);
    state.sensitivityMultiplierPercent.setData(values.sensitivityMultiplierPercent);
    state.cinematicEnabled.setData(values.cinematicEnabled);
    state.cinematicStrengthPercent.setData(values.cinematicStrengthPercent);
    state.seeding = false;
}

/// Reads the controls back out into the toolkit-independent shape.
Values collect(FormState const& state) {
    Values values;
    values.keyText                      = state.keyText.getData();
    values.activation                   = state.activation.getData();
    values.factor                       = state.factor.getData();
    values.scrollToAdjust               = state.scrollToAdjust.getData();
    values.scrollStepPercent            = state.scrollStepPercent.getData();
    values.rememberScrolledFactor       = state.rememberScrolledFactor.getData();
    values.durationMillis               = state.durationMillis.getData();
    values.curve                        = state.curve.getData();
    values.hideHand                     = state.hideHand.getData();
    values.sensitivityMode              = state.sensitivityMode.getData();
    values.sensitivityMultiplierPercent = state.sensitivityMultiplierPercent.getData();
    values.cinematicEnabled             = state.cinematicEnabled.getData();
    values.cinematicStrengthPercent     = state.cinematicStrengthPercent.getData();
    return values;
}

void apply(FormState& state) { state.keyStatus.setData(applyValues(collect(state))); }

/// Makes every control save itself as soon as the player changes it.
///
/// The subscription holds a *weak* reference back to the state. The observables are members of
/// the state, so a strong one would be a cycle: the state would keep the callbacks alive and the
/// callbacks would keep the state alive, and the form would leak every time it was opened. Held
/// weakly, everything goes when the last callback the form is holding is destroyed.
///
/// `seed` writes the same observables, and a write runs the subscribers, so the guard is what
/// keeps a reset from saving the config once per control on its way through.
void autoApply(std::shared_ptr<FormState> const& state) {
    auto save = [weak = std::weak_ptr<FormState>{state}](auto const&) {
        auto const held = weak.lock();
        if (!held || held->seeding) {
            return;
        }
        apply(*held);
    };

    state->keyText.subscribe(save);
    state->activation.subscribe(save);
    state->factor.subscribe(save);
    state->scrollToAdjust.subscribe(save);
    state->scrollStepPercent.subscribe(save);
    state->rememberScrolledFactor.subscribe(save);
    state->durationMillis.subscribe(save);
    state->curve.subscribe(save);
    state->hideHand.subscribe(save);
    state->sensitivityMode.subscribe(save);
    state->sensitivityMultiplierPercent.subscribe(save);
    state->cinematicEnabled.subscribe(save);
    state->cinematicStrengthPercent.subscribe(save);
}

/// The shared option tables in the toolkit's own shape.
std::vector<ll::ui::DropdownItemData> itemsOf(std::span<Option const> options) {
    std::vector<ll::ui::DropdownItemData> items;
    items.reserve(options.size());
    for (auto const& option : options) {
        ll::ui::DropdownItemData item{.label = std::string{option.label}, .value = option.value};
        if (!option.description.empty()) {
            item.description = std::string{option.description};
        }
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

void buildAndShow(Player& player) {
    auto const& config                = Zoomidy::getInstance().getConfig();
    auto const [factorMin, factorMax] = factorRange(config);

    auto state = std::make_shared<FormState>();
    seed(*state, config);
    autoApply(state);

    ll::ui::CustomForm form{player, "Zoomidy"};

    form.header("Zoom")
        .textField("Zoom key", state->keyText, {.description = std::string{kKeyDescription}})
        .label(state->keyStatus)
        .dropdown("Activation", state->activation, itemsOf(kActivationOptions))
        .slider(
            "Magnification",
            state->factor,
            factorMin,
            factorMax,
            {.description = std::string{kFactorDescription}, .step = 0.5}
        )
        .divider();

    form.header("Animation")
        .slider(
            "Transition (ms)",
            state->durationMillis,
            kDurationMinMs,
            kDurationMaxMs,
            {.description = std::string{kDurationDescription}, .step = kDurationStepMs}
        )
        .dropdown("Curve", state->curve, itemsOf(kCurveOptions))
        .divider();

    form.header("View")
        .toggle("Hide hand while zoomed", state->hideHand, {.description = std::string{kHideHandDescription}})
        .divider();

    form.header("Sensitivity")
        .dropdown("Mode", state->sensitivityMode, itemsOf(kSensitivityOptions))
        .slider(
            "Multiplier (%)",
            state->sensitivityMultiplierPercent,
            kMultiplierMinPc,
            kMultiplierMaxPc,
            {.description = std::string{kMultiplierDescription}, .step = kPercentStep}
        )
        .divider();

    form.header("Cinematic camera")
        .toggle("Smooth the camera while zoomed", state->cinematicEnabled)
        .slider(
            "Smoothing (%)",
            state->cinematicStrengthPercent,
            kCinematicMinPc,
            kCinematicMaxPc,
            {.description = std::string{kCinematicDescription}, .step = kPercentStep}
        )
        .divider();

    form.header("Scroll wheel")
        .toggle(
            "Adjust magnification with the wheel",
            state->scrollToAdjust,
            {.description = std::string{kScrollAdjustDescription}}
        )
        .slider(
            "Wheel step (%)",
            state->scrollStepPercent,
            kScrollStepMinPc,
            kScrollStepMaxPc,
            {.description = std::string{kScrollStepDescription}, .step = kPercentStep}
        )
        .toggle(
            "Remember wheel adjustment",
            state->rememberScrolledFactor,
            {.description = std::string{kRememberDescription}}
        )
        .divider();

    // No Apply button: `autoApply` has already saved everything above by the time the player gets
    // this far down the form.
    form.button(
            "Reset to defaults",
            [state] {
                // Re-seeding the controls is what makes the reset stick: the close handler reads
                // these observables back out, so leaving them on the old values would undo it.
                Config const defaults{};
                seed(*state, defaults);
                applyConfig(defaults);
                state->keyStatus.setData("§eSettings reset to their defaults.");
            }
        )
        .closeButton();

    if (auto shown = form.show([state](ll::ui::CustomForm::Result) { apply(*state); }); !shown) {
        shown.error().log(Zoomidy::getInstance().getSelf().getLogger());
    }
}

} // namespace zoomidy::ui

#endif
