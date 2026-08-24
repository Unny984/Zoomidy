#include "zoomidy/Ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "zoomidy/Config.h"
#include "zoomidy/KeyNames.h"
#include "zoomidy/UiForm.h"
#include "zoomidy/ZoomState.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::ui {

namespace {

/// Rounds to the nearest whole percent so the value that lands in the slider is one the slider's
/// own step can actually reach.
double toPercent(double fraction) { return std::round(fraction * 100.0); }

double fromPercent(double percent) { return percent / 100.0; }

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

} // namespace

std::pair<double, double> factorRange(Config const& config) {
    double const lo = std::max(1.0, config.zoom.minFactor);
    return {lo, std::max(lo + 0.5, config.zoom.maxFactor)};
}

Values toValues(Config const& config) {
    auto const [factorMin, factorMax] = factorRange(config);

    Values values;
    values.keyText                = keynames::format(config.zoom.keyCode);
    values.activation             = static_cast<double>(config.zoom.activation);
    values.factor                 = std::clamp(config.zoom.factor, factorMin, factorMax);
    values.scrollToAdjust         = config.zoom.scrollToAdjust;
    values.scrollStepPercent = std::clamp(toPercent(config.zoom.scrollStep), kScrollStepMinPc, kScrollStepMaxPc);
    values.rememberScrolledFactor = config.zoom.rememberScrolledFactor;
    values.durationMillis = std::clamp(config.animation.durationSeconds * 1000.0, kDurationMinMs, kDurationMaxMs);
    values.curve          = static_cast<double>(config.animation.curve);
    values.hideHand       = config.view.hideHand;
    values.sensitivityMode = static_cast<double>(config.sensitivity.mode);
    values.sensitivityMultiplierPercent =
        std::clamp(toPercent(config.sensitivity.multiplier), kMultiplierMinPc, kMultiplierMaxPc);
    values.cinematicEnabled = config.cinematic.enabled;
    values.cinematicStrengthPercent =
        std::clamp(toPercent(config.cinematic.strength), kCinematicMinPc, kCinematicMaxPc);
    return values;
}

Config fromValues(Values const& values, Config const& previous, std::string& status) {
    Config config = previous;

    if (auto const parsed = keynames::parse(values.keyText)) {
        config.zoom.keyCode = *parsed;
        status              = std::format("§aZoom key: {}", keynames::format(*parsed));
    } else {
        status = std::format(
            "§c\"{}\" is not a key. Still bound to {}.",
            values.keyText,
            keynames::format(previous.zoom.keyCode)
        );
    }

    config.zoom.activation             = static_cast<ActivationMode>(static_cast<int>(values.activation));
    config.zoom.factor                 = values.factor;
    config.zoom.scrollToAdjust         = values.scrollToAdjust;
    config.zoom.scrollStep             = fromPercent(values.scrollStepPercent);
    config.zoom.rememberScrolledFactor = values.rememberScrolledFactor;
    config.animation.durationSeconds   = values.durationMillis / 1000.0;
    config.animation.curve             = static_cast<EasingCurve>(static_cast<int>(values.curve));
    config.view.hideHand               = values.hideHand;
    config.sensitivity.mode            = static_cast<SensitivityMode>(static_cast<int>(values.sensitivityMode));
    config.sensitivity.multiplier      = fromPercent(values.sensitivityMultiplierPercent);
    config.cinematic.enabled           = values.cinematicEnabled;
    config.cinematic.strength          = std::clamp(fromPercent(values.cinematicStrengthPercent), 0.0, 0.95);

    return config;
}

std::string applyValues(Values const& values) {
    auto&       mod = Zoomidy::getInstance();
    std::string status;
    auto        config = fromValues(values, mod.getConfig(), status);
    mod.applyConfig(config);
    ZoomState::getInstance().reset();
    return status;
}

void applyConfig(Config const& config) {
    Zoomidy::getInstance().applyConfig(config);
    ZoomState::getInstance().reset();
}

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

void onServerThread(std::function<void(Player&)> action) {
    ll::thread::ServerThreadExecutor::getDefault().execute([action = std::move(action)] {
        if (auto* player = findHostedServerPlayer()) {
            action(*player);
            return;
        }
        Zoomidy::getInstance().getSelf().getLogger().warn(
            "Could not open the settings form: no player in the hosted world."
        );
    });
}

void openSettingsForm() {
    onServerThread([](Player& player) { buildAndShow(player); });
}

} // namespace zoomidy::ui
