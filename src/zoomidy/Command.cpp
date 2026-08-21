#include "zoomidy/Command.h"

#include <algorithm>
#include <string>

#include "magic_enum/magic_enum.hpp"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Optional.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/command/ClientCommandRegisterEvent.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include "zoomidy/Config.h"
#include "zoomidy/KeyNames.h"
#include "zoomidy/Ui.h"
#include "zoomidy/ZoomState.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy {

// Command parameter structs are reflected with boost::pfr, which needs them to have external
// linkage -- an anonymous namespace would make the type unnameable and fail to compile.
namespace params {

struct KeyParam {
    std::string key;
};

struct MagnificationParam {
    float magnification;
};

struct TransitionParam {
    float                            milliseconds;
    ll::command::Optional<EasingCurve> curve;
};

struct ActivationParam {
    ActivationMode activation;
};

struct HandParam {
    bool hide;
};

struct SensitivityParam {
    SensitivityMode             mode;
    ll::command::Optional<float> multiplier;
};

struct CinematicParam {
    bool                         enabled;
    ll::command::Optional<float> strength;
};

} // namespace params

namespace {

using namespace params;

ll::event::ListenerPtr gRegisterListener;

/// Writes `config` through the mod and drops any in-flight zoom, so a settings change can never
/// leave the camera stuck part way through an animation with stale numbers.
void commit(Config const& config) {
    Zoomidy::getInstance().applyConfig(config);
    ZoomState::getInstance().reset();
}

void reportStatus(CommandOutput& output) {
    auto const& c = Zoomidy::getInstance().getConfig();

    output.success("§lZoomidy§r");
    output.success("  key: {}  activation: {}", keynames::format(c.zoom.keyCode), magic_enum::enum_name(c.zoom.activation));
    output.success("  magnification: {:g}x  (range {:g}-{:g})", c.zoom.factor, c.zoom.minFactor, c.zoom.maxFactor);
    output.success(
        "  transition: {:g} ms  curve: {}",
        c.animation.durationSeconds * 1000.0,
        magic_enum::enum_name(c.animation.curve)
    );
    output.success("  hide hand: {}", c.view.hideHand ? "yes" : "no");
    output.success("  sensitivity: {} x{:g}", magic_enum::enum_name(c.sensitivity.mode), c.sensitivity.multiplier);
    output.success("  cinematic: {} strength {:g}", c.cinematic.enabled ? "on" : "off", c.cinematic.strength);
    output.success("  scroll to adjust: {} step {:g}", c.zoom.scrollToAdjust ? "on" : "off", c.zoom.scrollStep);
    output.success("  config file: {}", Zoomidy::getInstance().getConfigPath().string());
}

void buildCommand() {
    auto& registrar = ll::command::CommandRegistrar::getInstance(true);

    registrar.tryRegisterEnum<ActivationMode>();
    registrar.tryRegisterEnum<SensitivityMode>();
    registrar.tryRegisterEnum<EasingCurve>();

    auto& cmd = registrar.getOrCreateCommand("zoomidy", "Zoomidy zoom settings", CommandPermissionLevel::Any);

    // Bare `/zoomidy` opens the settings form when the client is hosting the world, and
    // otherwise explains why it cannot and points at the sub-commands, which always work.
    cmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
        if (auto const reason = ui::describeUnavailability(); !reason.empty()) {
            output.error("{}", reason);
            reportStatus(output);
            return;
        }
        ui::openSettingsForm();
        output.success("Opening Zoomidy settings.");
    });

    cmd.overload().text("status").execute([](CommandOrigin const&, CommandOutput& output) { reportStatus(output); });

    cmd.overload().text("reload").execute([](CommandOrigin const&, CommandOutput& output) {
        if (Zoomidy::getInstance().loadConfig()) {
            ZoomState::getInstance().reset();
            output.success("Reloaded Zoomidy config.");
        } else {
            output.error("Failed to reload Zoomidy config; see the log for details.");
        }
    });

    cmd.overload<KeyParam>().text("key").required("key").execute(
        [](CommandOrigin const&, CommandOutput& output, KeyParam const& param) {
            auto const parsed = keynames::parse(param.key);
            if (!parsed) {
                output.error("\"{}\" is not a key. Try a letter, F1-F12, SPACE, LSHIFT, or a code like 0x46.", param.key);
                return;
            }
            auto config         = Zoomidy::getInstance().getConfig();
            config.zoom.keyCode = *parsed;
            commit(config);
            output.success("Zoom key set to {}.", keynames::format(*parsed));
        }
    );

    cmd.overload<ActivationParam>().text("activation").required("activation").execute(
        [](CommandOrigin const&, CommandOutput& output, ActivationParam const& param) {
            auto config            = Zoomidy::getInstance().getConfig();
            config.zoom.activation = param.activation;
            commit(config);
            output.success("Activation set to {}.", magic_enum::enum_name(param.activation));
        }
    );

    cmd.overload<MagnificationParam>().text("magnification").required("magnification").execute(
        [](CommandOrigin const&, CommandOutput& output, MagnificationParam const& param) {
            auto config = Zoomidy::getInstance().getConfig();
            config.zoom.factor =
                std::clamp(static_cast<double>(param.magnification), config.zoom.minFactor, config.zoom.maxFactor);
            commit(config);
            output.success("Magnification set to {:g}x.", config.zoom.factor);
        }
    );

    cmd.overload<TransitionParam>().text("transition").required("milliseconds").optional("curve").execute(
        [](CommandOrigin const&, CommandOutput& output, TransitionParam const& param) {
            auto config                      = Zoomidy::getInstance().getConfig();
            config.animation.durationSeconds = std::clamp(static_cast<double>(param.milliseconds), 0.0, 5000.0) / 1000.0;
            if (param.curve.has_value()) {
                config.animation.curve = param.curve.get();
            }
            commit(config);
            output.success(
                "Transition set to {:g} ms on {}.",
                config.animation.durationSeconds * 1000.0,
                magic_enum::enum_name(config.animation.curve)
            );
        }
    );

    cmd.overload<HandParam>().text("hidehand").required("hide").execute(
        [](CommandOrigin const&, CommandOutput& output, HandParam const& param) {
            auto config          = Zoomidy::getInstance().getConfig();
            config.view.hideHand = param.hide;
            commit(config);
            output.success("Hide hand while zoomed: {}.", param.hide ? "on" : "off");
        }
    );

    cmd.overload<SensitivityParam>().text("sensitivity").required("mode").optional("multiplier").execute(
        [](CommandOrigin const&, CommandOutput& output, SensitivityParam const& param) {
            auto config             = Zoomidy::getInstance().getConfig();
            config.sensitivity.mode = param.mode;
            if (param.multiplier.has_value()) {
                config.sensitivity.multiplier = std::clamp(static_cast<double>(param.multiplier.get()), 0.05, 3.0);
            }
            commit(config);
            output.success(
                "Sensitivity set to {} at x{:g}.",
                magic_enum::enum_name(config.sensitivity.mode),
                config.sensitivity.multiplier
            );
        }
    );

    cmd.overload<CinematicParam>().text("cinematic").required("enabled").optional("strength").execute(
        [](CommandOrigin const&, CommandOutput& output, CinematicParam const& param) {
            auto config              = Zoomidy::getInstance().getConfig();
            config.cinematic.enabled = param.enabled;
            if (param.strength.has_value()) {
                config.cinematic.strength = std::clamp(static_cast<double>(param.strength.get()), 0.0, 0.95);
            }
            commit(config);
            output.success("Cinematic camera {} at strength {:g}.", config.cinematic.enabled ? "on" : "off", config.cinematic.strength);
        }
    );
}

} // namespace

void registerCommand() {
    gRegisterListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientCommandRegisterEvent>(
        [](ll::event::ClientCommandRegisterEvent&) { buildCommand(); }
    );
}

void unregisterCommand() {
    if (gRegisterListener) {
        ll::event::EventBus::getInstance().removeListener(gRegisterListener);
        gRegisterListener.reset();
    }
}

} // namespace zoomidy
