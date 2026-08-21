#include "zoomidy/Ui.h"

#include <algorithm>
#include <format>
#include <memory>
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

/// Every control's live value. Held in a shared_ptr so the button callbacks and the completion
/// callback can all read the same state after the form has left the calling stack frame.
struct FormState {
    ll::ui::ObservableString  keyText;
    ll::ui::ObservableNumber  activation;
    ll::ui::ObservableNumber  factor;
    ll::ui::ObservableBoolean scrollToAdjust;
    ll::ui::ObservableNumber  scrollStep;
    ll::ui::ObservableBoolean rememberScrolledFactor;
    ll::ui::ObservableNumber  durationMillis;
    ll::ui::ObservableNumber  curve;
    ll::ui::ObservableBoolean hideHand;
    ll::ui::ObservableNumber  sensitivityMode;
    ll::ui::ObservableNumber  sensitivityMultiplier;
    ll::ui::ObservableBoolean cinematicEnabled;
    ll::ui::ObservableNumber  cinematicStrength;
    ll::ui::ObservableString  keyStatus;

    explicit FormState(Config const& config)
    : keyText(keynames::format(config.zoom.keyCode), kWritable),
      activation(static_cast<double>(config.zoom.activation), kWritable),
      factor(config.zoom.factor, kWritable),
      scrollToAdjust(config.zoom.scrollToAdjust, kWritable),
      scrollStep(config.zoom.scrollStep, kWritable),
      rememberScrolledFactor(config.zoom.rememberScrolledFactor, kWritable),
      durationMillis(config.animation.durationSeconds * 1000.0, kWritable),
      curve(static_cast<double>(config.animation.curve), kWritable),
      hideHand(config.view.hideHand, kWritable),
      sensitivityMode(static_cast<double>(config.sensitivity.mode), kWritable),
      sensitivityMultiplier(config.sensitivity.multiplier, kWritable),
      cinematicEnabled(config.cinematic.enabled, kWritable),
      cinematicStrength(config.cinematic.strength, kWritable),
      keyStatus(std::string{}) {}
};

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
    config.zoom.scrollStep             = state.scrollStep.getData();
    config.zoom.rememberScrolledFactor = state.rememberScrolledFactor.getData();
    config.animation.durationSeconds   = state.durationMillis.getData() / 1000.0;
    config.animation.curve             = static_cast<EasingCurve>(static_cast<int>(state.curve.getData()));
    config.view.hideHand               = state.hideHand.getData();
    config.sensitivity.mode            = static_cast<SensitivityMode>(static_cast<int>(state.sensitivityMode.getData()));
    config.sensitivity.multiplier      = state.sensitivityMultiplier.getData();
    config.cinematic.enabled           = state.cinematicEnabled.getData();
    config.cinematic.strength          = std::clamp(state.cinematicStrength.getData(), 0.0, 0.95);

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
    auto const& config = Zoomidy::getInstance().getConfig();
    auto        state  = std::make_shared<FormState>(config);

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
        .slider("Magnification", state->factor, config.zoom.minFactor, config.zoom.maxFactor, {.description = "How far in the zoom goes. 4 means 4x.", .step = 0.5})
        .divider();

    form.header("Animation")
        .slider(
            "Transition (ms)",
            state->durationMillis,
            0.0,
            1000.0,
            {.description = "Time spent easing in, and again easing out. 0 snaps instantly.", .step = 10.0}
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
            "Multiplier",
            state->sensitivityMultiplier,
            0.05,
            3.0,
            {.description = "Relative mode multiplies this on top; Fixed mode uses it on its own.", .step = 0.05}
        )
        .divider();

    form.header("Cinematic camera")
        .toggle("Smooth the camera while zoomed", state->cinematicEnabled)
        .slider("Smoothing", state->cinematicStrength, 0.0, 0.95, {.description = "Higher is heavier. 0 is no smoothing.", .step = 0.05})
        .divider();

    form.header("Scroll wheel")
        .toggle("Adjust magnification with the wheel", state->scrollToAdjust, {.description = "While zoomed, the wheel changes the magnification instead of the hotbar."})
        .slider("Wheel step", state->scrollStep, 1.05, 2.0, {.description = "How much one notch multiplies the magnification by.", .step = 0.05})
        .toggle("Remember wheel adjustment", state->rememberScrolledFactor, {.description = "Keep the scrolled magnification for the next zoom instead of returning to the slider value."})
        .divider();

    form.button("Apply", [state] { apply(*state); })
        .button("Reset to defaults", [state] {
            Zoomidy::getInstance().applyConfig(Config{});
            ZoomState::getInstance().reset();
            state->keyStatus.setData("§eReset. Reopen the form to see the defaults.");
        })
        .closeButton();

    if (auto shown = form.show([state](ll::ui::CustomForm::Result) { apply(*state); }); !shown) {
        shown.error().log(Zoomidy::getInstance().getSelf().getLogger());
    }
}

} // namespace

std::string describeUnavailability() {
    if (!ll::service::getServerInstance()) {
        return "The settings form needs a world this client is hosting. Join a single-player or "
               "LAN-hosted world, or edit config/config.json and run /zoomidy reload.";
    }
    if (findHostedServerPlayer() == nullptr) {
        return "No player found in the hosted world yet. Try again once you are in the world.";
    }
    return {};
}

void openSettingsForm() {
    ll::thread::ServerThreadExecutor::getDefault().execute([] {
        if (auto* player = findHostedServerPlayer()) {
            buildAndShow(*player);
        }
    });
}

} // namespace zoomidy::ui
