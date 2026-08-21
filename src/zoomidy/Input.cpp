#include "zoomidy/Input.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/deps/input/MouseAction.h"

#include "zoomidy/ZoomState.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy {

namespace {

ll::event::ListenerPtr gKeyListener;
ll::event::ListenerPtr gMouseListener;
ll::event::ListenerPtr gTickListener;
ll::event::ListenerPtr gExitListener;

/// The zoom key is currently physically down. Guards against key auto-repeat flipping a toggle
/// binding on and off many times a second.
bool gKeyHeld = false;

/// Sub-pixel remainder of the sensitivity scaling. The client hands us whole-number mouse
/// deltas, so a 0.25x scale applied naively would throw away every movement of 1 or 2 counts and
/// make slow aiming impossible. Carrying the remainder into the next event keeps the total
/// motion exact.
double gResidualX = 0.0;
double gResidualY = 0.0;

/// Motion the cinematic filter is still paying out over subsequent frames.
double gCinematicX = 0.0;
double gCinematicY = 0.0;

void resetFilters() {
    gResidualX  = 0.0;
    gResidualY  = 0.0;
    gCinematicX = 0.0;
    gCinematicY = 0.0;
}

/// True when the player is actually flying the camera around: pointer locked, no chat box, no
/// inventory, no pause menu. Zooming while a screen is open would be both useless and confusing.
bool isInGameplay() {
    auto client = ll::service::getClientInstance();
    return client && client->getMouseGrabbed();
}

/// How much the mouse look should be scaled down right now, following the zoom animation.
double sensitivityFactor() {
    auto const& settings = Zoomidy::getInstance().getConfig().sensitivity;
    auto&       zoom     = ZoomState::getInstance();

    double factor = 1.0;
    switch (settings.mode) {
    case SensitivityMode::Off:
        return 1.0;
    case SensitivityMode::Relative:
        // Follows the live magnification, so at 4x the camera turns at a quarter speed and the
        // apparent speed of the world under the crosshair stays the same as unzoomed.
        factor = 1.0 / zoom.currentDivisor();
        break;
    case SensitivityMode::Fixed:
        // Ramps from 1 to the multiplier over the ease-in, so there is no jolt at the start.
        factor = 1.0 + (settings.multiplier - 1.0) * zoom.easedProgress();
        return std::max(0.01, factor);
    }

    return std::max(0.01, factor * settings.multiplier);
}

/// Applies the sensitivity scale, then the cinematic smoothing, then quantises back to the whole
/// numbers the client expects while carrying the remainder forward.
short filterAxis(short raw, double factor, bool cinematic, double strength, double& residual, double& carry) {
    double value = static_cast<double>(raw) * factor;

    if (cinematic) {
        // A one-pole low-pass that conserves total movement: whatever is held back this event is
        // released over the following ones, which is what gives the camera its weight.
        carry += value;
        value  = carry * (1.0 - strength);
        carry -= value;
    }

    value   += residual;
    double const rounded = std::round(value);
    residual = value - rounded;

    return static_cast<short>(
        std::clamp(rounded, static_cast<double>(std::numeric_limits<short>::min()), static_cast<double>(std::numeric_limits<short>::max()))
    );
}

/// Note that the event is never cancelled. Suppressing the key would mean that binding the zoom
/// to a key Minecraft already uses — chat, escape, a movement key — would take that function away
/// with no way to get it back from inside the game. Letting the key through costs nothing when it
/// is bound to something vanilla does not use, which is the normal case.
void onKeyInput(ll::event::KeyInputEvent& ev) {
    auto const& config = Zoomidy::getInstance().getConfig();
    if (ev.keyCode() != config.zoom.keyCode) {
        return;
    }

    auto& zoom = ZoomState::getInstance();

    if (!ev.isDown()) {
        // Key-up is handled even outside gameplay, otherwise releasing the key after opening
        // chat would leave the mod believing it is still held.
        if (!gKeyHeld) {
            return;
        }
        gKeyHeld = false;
        if (config.zoom.activation == ActivationMode::Hold) {
            zoom.setActive(false);
        }
        return;
    }

    // Auto-repeat sends a stream of key-downs while the key is held; only the first one counts,
    // otherwise a toggle binding would flicker on and off many times a second.
    if (!isInGameplay() || gKeyHeld) {
        return;
    }
    gKeyHeld = true;

    if (config.zoom.activation == ActivationMode::Toggle) {
        zoom.toggleActive();
    } else {
        zoom.setActive(true);
    }
}

void onMouseInput(ll::event::MouseInputEvent& ev) {
    auto&       zoom   = ZoomState::getInstance();
    auto const& config = Zoomidy::getInstance().getConfig();

    if (!zoom.isEngaged()) {
        resetFilters();
        return;
    }

    char const action = ev.actionButtonId();

    if (action == ::MouseAction::ActionWheel) {
        if (config.zoom.scrollToAdjust && zoom.isActive()) {
            zoom.adjustFactorByScroll(ev.buttonData() > 0 ? 1 : -1);
            // Swallow the notch so the hotbar selection does not move with it.
            ev.cancel();
        }
        return;
    }

    if (action != ::MouseAction::ActionMoveRelative) {
        return;
    }

    double const factor    = sensitivityFactor();
    bool const   cinematic = config.cinematic.enabled;
    double const strength  = std::clamp(config.cinematic.strength, 0.0, 0.95);

    if (factor == 1.0 && !cinematic) {
        return;
    }

    ev.dx() = filterAxis(ev.dx(), factor, cinematic, strength, gResidualX, gCinematicX);
    ev.dy() = filterAxis(ev.dy(), factor, cinematic, strength, gResidualY, gCinematicY);
}

void onClientTick(ll::event::ClientLevelTickEvent&) {
    auto const& config = Zoomidy::getInstance().getConfig();
    auto&       zoom   = ZoomState::getInstance();

    // Opening chat or a container swallows the key-up, so drop a held zoom once the pointer is
    // released. Toggled zoom is deliberately left alone: it survives menus, like Zoomify's does.
    if (config.zoom.activation == ActivationMode::Hold && zoom.isActive() && !isInGameplay()) {
        gKeyHeld = false;
        zoom.setActive(false);
    }
}

/// Leaving the world while zoomed would otherwise carry the zoom into the next one, because
/// nothing else clears the state between sessions.
void onExitLevel(ll::event::ClientExitLevelEvent&) {
    gKeyHeld = false;
    resetFilters();
    ZoomState::getInstance().reset();
}

} // namespace

void registerInputListeners() {
    auto& bus = ll::event::EventBus::getInstance();

    gKeyListener   = bus.emplaceListener<ll::event::KeyInputEvent>(onKeyInput);
    gMouseListener = bus.emplaceListener<ll::event::MouseInputEvent>(onMouseInput);
    gTickListener  = bus.emplaceListener<ll::event::ClientLevelTickEvent>(onClientTick);
    gExitListener  = bus.emplaceListener<ll::event::ClientExitLevelEvent>(onExitLevel);
}

void unregisterInputListeners() {
    auto& bus = ll::event::EventBus::getInstance();

    for (auto* listener : {&gKeyListener, &gMouseListener, &gTickListener, &gExitListener}) {
        if (*listener) {
            bus.removeListener(*listener);
            listener->reset();
        }
    }
    gKeyHeld = false;
    resetFilters();
}

} // namespace zoomidy
