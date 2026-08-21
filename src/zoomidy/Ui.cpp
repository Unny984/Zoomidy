#include "zoomidy/Ui.h"

#include <algorithm>
#include <format>
#include <memory>
#include <utility>
#include <vector>

#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/ui/base/Observable.h"
#include "ll/api/ui/form/CustomForm.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "zoomidy/Config.h"
#include "zoomidy/KeyNames.h"
#include "zoomidy/ZoomState.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::ui {

namespace {

constexpr ll::ui::ObservableOptions kWritable{.clientWritable = true};

// Bounds of the sliders in the form. A slider hands its control a value clamped into its own
// range, and that clamped value is what gets read back on close -- so anything the form shows
// has to be seeded already clamped, or opening the form would quietly rewrite a config.json the
// user edited by hand.
//
// Fractional settings are carried as whole percents rather than as the fraction itself. Bedrock
// prints a slider's raw value, and a 0.05 step walks 0.05, 0.1, ... 1.5000000000000002 because
// neither the step nor the accumulated total is representable in binary floating point. Stepping
// over integers and dividing at the edge keeps the label readable and the value exact.
constexpr double kDurationMinMs   = 0.0;
constexpr double kDurationMaxMs   = 2000.0;
constexpr double kDurationStepMs  = 10.0;
constexpr double kScrollStepMinPc = 105.0;
constexpr double kScrollStepMaxPc = 200.0;
constexpr double kMultiplierMinPc = 5.0;
constexpr double kMultiplierMaxPc = 300.0;
constexpr double kCinematicMinPc  = 0.0;
constexpr double kCinematicMaxPc  = 95.0;
constexpr double kPercentStep     = 5.0;

/// Rounds to the nearest whole percent so the value that lands in the slider is one the slider's
/// own step can actually reach.
double toPercent(double fraction) { return std::round(fraction * 100.0); }

double fromPercent(double percent) { return percent / 100.0; }

/// The magnification slider's range, sanitised so a hand-edited config can never hand `slider()`
/// a minimum above its maximum.
std::pair<double, double> factorRange(Config const& config) {
    double const lo = std::max(1.0, config.zoom.minFactor);
    return {lo, std::max(lo + 0.5, config.zoom.maxFactor)};
}

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
};

/// Pushes a config into the controls. Used to build the form and again by "Reset to defaults",
/// so that the reset is visible immediately *and* survives the form being closed -- the close
/// handler reads these same observables back out.
void seed(FormState& state, Config const& config) {
    auto const [factorMin, factorMax] = factorRange(config);

    state.keyText.setData(keynames::format(config.zoom.keyCode));
    state.activation.setData(static_cast<double>(config.zoom.activation));
    state.factor.setData(std::clamp(config.zoom.factor, factorMin, factorMax));
    state.scrollToAdjust.setData(config.zoom.scrollToAdjust);
    state.scrollStepPercent.setData(std::clamp(toPercent(config.zoom.scrollStep), kScrollStepMinPc, kScrollStepMaxPc));
    state.rememberScrolledFactor.setData(config.zoom.rememberScrolledFactor);
    state.durationMillis.setData(std::clamp(config.animation.durationSeconds * 1000.0, kDurationMinMs, kDurationMaxMs));
    state.curve.setData(static_cast<double>(config.animation.curve));
    state.hideHand.setData(config.view.hideHand);
    state.sensitivityMode.setData(static_cast<double>(config.sensitivity.mode));
    state.sensitivityMultiplierPercent.setData(
        std::clamp(toPercent(config.sensitivity.multiplier), kMultiplierMinPc, kMultiplierMaxPc)
    );
    state.cinematicEnabled.setData(config.cinematic.enabled);
    state.cinematicStrengthPercent.setData(
        std::clamp(toPercent(config.cinematic.strength), kCinematicMinPc, kCinematicMaxPc)
    );
}

/// Reads the controls back into a config. Anything the user typed that does not name a key is
/// reported through `keyStatus` and leaves the existing binding in place.
Config collect(FormState& state, Config const& previous) {
    Config config = previous;

    if (auto const parsed = keynames::parse(state.keyText.getData())) {
        config.zoom.keyCode = *parsed;
        state.keyStatus.setData(std::format("§aZoom key: {}", keynames::format(*parsed)));
    } else {
        state.keyStatus.setData(
            std::format("§c\"{}\" is not a key. Still bound to {}.", state.keyText.getData(), keynames::format(previous.zoom.keyCode))
        );
    }

    config.zoom.activation             = static_cast<ActivationMode>(static_cast<int>(state.activation.getData()));
    config.zoom.factor                 = state.factor.getData();
    config.zoom.scrollToAdjust         = state.scrollToAdjust.getData();
    config.zoom.scrollStep             = fromPercent(state.scrollStepPercent.getData());
    config.zoom.rememberScrolledFactor = state.rememberScrolledFactor.getData();
    config.animation.durationSeconds   = state.durationMillis.getData() / 1000.0;
    config.animation.curve             = static_cast<EasingCurve>(static_cast<int>(state.curve.getData()));
    config.view.hideHand               = state.hideHand.getData();
    config.sensitivity.mode            = static_cast<SensitivityMode>(static_cast<int>(state.sensitivityMode.getData()));
    config.sensitivity.multiplier      = fromPercent(state.sensitivityMultiplierPercent.getData());
    config.cinematic.enabled           = state.cinematicEnabled.getData();
    config.cinematic.strength          = std::clamp(fromPercent(state.cinematicStrengthPercent.getData()), 0.0, 0.95);

    return config;
}

void apply(FormState& state) {
    auto& mod    = Zoomidy::getInstance();
    auto  config = collect(state, mod.getConfig());
    mod.applyConfig(config);
    ZoomState::getInstance().reset();
}

/// The client's own player lives in the client level; the data-driven UI needs the matching
/// player in the *server* level, which only exists while this client is hosting the world.
Player* findHostedServerPlayer() {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }

    if (auto client = ll::service::getClientInstance()) {
        if (auto* local = client->getLocalPlayer()) {
            if (auto* matched = level->getPlayer(local->getUuid())) {
                return matched;
            }
        }
    }

    // A hosted world always has exactly one local player, so falling back to the first one is
    // correct even if the UUID lookup missed.
    Player* first = nullptr;
    level->forEachPlayer([&first](Player& player) {
        first = &player;
        return false;
    });
    return first;
}

void buildAndShow(Player& player) {
    auto const& config            = Zoomidy::getInstance().getConfig();
    auto const [factorMin, factorMax] = factorRange(config);

    auto state = std::make_shared<FormState>();
    seed(*state, config);

    ll::ui::CustomForm form{player, "Zoomidy"};

    form.header("Zoom")
        .textField(
            "Zoom key",
            state->keyText,
            {.description = "A letter or digit (F, Z, 3), a named key (F5, SPACE, LSHIFT), or a virtual-key code (0x46)."}
        )
        .label(state->keyStatus)
        .dropdown(
            "Activation",
            state->activation,
            {
                {.label = "Hold", .value = static_cast<double>(ActivationMode::Hold), .description = "Zoomed while the key is held down."},
                {.label = "Toggle", .value = static_cast<double>(ActivationMode::Toggle), .description = "Each press flips the zoom on or off."},
    }
        )
        .slider("Magnification", state->factor, factorMin, factorMax, {.description = "How far in the zoom goes. 4 means 4x.", .step = 0.5})
        .divider();

    form.header("Animation")
        .slider(
            "Transition (ms)",
            state->durationMillis,
            kDurationMinMs,
            kDurationMaxMs,
            {.description = "Time spent easing in, and again easing out. 0 snaps instantly.", .step = kDurationStepMs}
        )
        .dropdown(
            "Curve",
            state->curve,
            {
                {.label = "Linear", .value = static_cast<double>(EasingCurve::Linear)},
                {.label = "Ease out (quad)", .value = static_cast<double>(EasingCurve::EaseOutQuad)},
                {.label = "Ease in-out (quad)", .value = static_cast<double>(EasingCurve::EaseInOutQuad)},
                {.label = "Ease out (cubic)", .value = static_cast<double>(EasingCurve::EaseOutCubic)},
                {.label = "Ease in-out (cubic)", .value = static_cast<double>(EasingCurve::EaseInOutCubic)},
                {.label = "Ease out (expo)", .value = static_cast<double>(EasingCurve::EaseOutExpo)},
    }
        )
        .divider();

    form.header("View")
        .toggle("Hide hand while zoomed", state->hideHand, {.description = "Hides the first-person arm and held item so they do not fill the view."})
        .divider();

    form.header("Sensitivity")
        .dropdown(
            "Mode",
            state->sensitivityMode,
            {
                {.label = "Off", .value = static_cast<double>(SensitivityMode::Off), .description = "Leave mouse look alone."},
                {.label = "Relative", .value = static_cast<double>(SensitivityMode::Relative), .description = "Scale with the live magnification, so the world moves at the same apparent speed."},
                {.label = "Fixed", .value = static_cast<double>(SensitivityMode::Fixed), .description = "Apply the multiplier below for as long as the zoom is active."},
    }
        )
        .slider(
            "Multiplier (%)",
            state->sensitivityMultiplierPercent,
            kMultiplierMinPc,
            kMultiplierMaxPc,
            {.description = "100% leaves the mode's own result alone. Relative multiplies this on top; Fixed uses it by itself.",
             .step        = kPercentStep}
        )
        .divider();

    form.header("Cinematic camera")
        .toggle("Smooth the camera while zoomed", state->cinematicEnabled)
        .slider(
            "Smoothing (%)",
            state->cinematicStrengthPercent,
            kCinematicMinPc,
            kCinematicMaxPc,
            {.description = "Higher is heavier. 0% is no smoothing.", .step = kPercentStep}
        )
        .divider();

    form.header("Scroll wheel")
        .toggle("Adjust magnification with the wheel", state->scrollToAdjust, {.description = "While zoomed, the wheel changes the magnification instead of the hotbar."})
        .slider(
            "Wheel step (%)",
            state->scrollStepPercent,
            kScrollStepMinPc,
            kScrollStepMaxPc,
            {.description = "How much one notch multiplies the magnification by. 120% steps by a fifth each time.",
             .step        = kPercentStep}
        )
        .toggle("Remember wheel adjustment", state->rememberScrolledFactor, {.description = "Keep the scrolled magnification for the next zoom instead of returning to the slider value."})
        .divider();

    form.button("Apply", [state] { apply(*state); })
        .button("Reset to defaults", [state] {
            // Re-seeding the controls is what makes the reset stick: the close handler reads
            // these observables back out, so leaving them on the old values would undo it.
            Config const defaults{};
            seed(*state, defaults);
            Zoomidy::getInstance().applyConfig(defaults);
            ZoomState::getInstance().reset();
            state->keyStatus.setData("§eSettings reset to their defaults.");
        })
        .closeButton();

    if (auto shown = form.show([state](ll::ui::CustomForm::Result) { apply(*state); }); !shown) {
        shown.error().log(Zoomidy::getInstance().getSelf().getLogger());
    }
}

} // namespace

std::string describeUnavailability() {
    // Deliberately shallow: this runs on the client thread, so it only checks for things that
    // are safe to read from here. Walking the server's player list is left to the server thread
    // in `openSettingsForm`.
    if (!ll::service::getServerInstance() || !ll::service::getLevel()) {
        return "The settings form needs a world this client is hosting. Join a single-player or "
               "LAN-hosted world, or use the /zoomidy sub-commands, which work anywhere.";
    }
    return {};
}

void openSettingsForm() {
    ll::thread::ServerThreadExecutor::getDefault().execute([] {
        if (auto* player = findHostedServerPlayer()) {
            buildAndShow(*player);
            return;
        }
        Zoomidy::getInstance().getSelf().getLogger().warn(
            "Could not open the settings form: no player in the hosted world."
        );
    });
}

} // namespace zoomidy::ui
