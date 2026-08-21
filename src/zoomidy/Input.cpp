#include "zoomidy/Input.h"

#include <algorithm>
#include <chrono>
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

/// The virtual-key code currently held down for the zoom, or 0 when nothing is.
///
/// This stores the code rather than a bare flag on purpose. The release has to be matched
/// against the key that actually went down, not against whatever the binding says *now* -- the
/// binding can be changed while the key is held, and a release that fails to match would leave
/// the mod convinced the key is still down and refuse every later press.
int gHeldKeyCode = 0;

/// Sub-pixel remainder of the sensitivity scaling. The client hands us whole-number mouse
/// deltas, so a 0.25x scale applied naively would throw away every movement of 1 or 2 counts and
/// make slow aiming impossible. Carrying the remainder into the next event keeps the total
/// motion exact.
double gResidualX = 0.0;
double gResidualY = 0.0;

/// Motion the cinematic filter is still paying out over subsequent events.
double gCinematicX = 0.0;
double gCinematicY = 0.0;

/// When the last motion event arrived, so the cinematic filter can work in real time instead of
/// per event. Mouse events land every 3-5 ms; a fixed per-event release fraction therefore decays
/// to nothing within a few milliseconds and produces no visible lag whatsoever.
std::chrono::steady_clock::time_point gLastMotion{};
bool                                  gHasLastMotion = false;

/// Lag at the maximum smoothing setting, in seconds. The setting scales this linearly, so the
/// default 60% works out to a 300 ms time constant -- heavy enough to feel, short enough to aim.
constexpr double kCinematicMaxTimeConstant = 0.5;

/// Remaining mouse events to dump to the log for `/zoomidy debug`. Counts down to zero so the
/// diagnostic cannot be left on by accident.
int gDebugEventsLeft = 0;

void resetFilters() {
    gResidualX     = 0.0;
    gResidualY     = 0.0;
    gCinematicX    = 0.0;
    gCinematicY    = 0.0;
    gHasLastMotion = false;
}

/// The fraction of the outstanding motion the cinematic filter releases on this event.
///
/// Derived from how long it has been since the last one, so the perceived lag stays the same
/// whether the mouse reports at 125 Hz or 1000 Hz. A long gap drives this towards 1, which drains
/// whatever the filter still owes rather than stranding it.
double cinematicRelease(double strength) {
    double const tau = std::clamp(strength, 0.0, 1.0) * kCinematicMaxTimeConstant;
    if (tau <= 0.0) {
        return 1.0;
    }

    auto const now = std::chrono::steady_clock::now();
    double     dt  = kCinematicMaxTimeConstant * 8.0;
    if (gHasLastMotion) {
        dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - gLastMotion).count();
    }
    gLastMotion    = now;
    gHasLastMotion = true;

    return 1.0 - std::exp(-std::max(0.0, dt) / tau);
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
short filterAxis(short raw, double factor, double release, double& residual, double& carry) {
    double value = static_cast<double>(raw) * factor;

    if (release < 1.0) {
        // A one-pole low-pass that conserves total movement: whatever is held back this event is
        // released over the following ones, which is what gives the camera its weight.
        carry += value;
        value  = carry * release;
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
    auto&       zoom   = ZoomState::getInstance();

    if (!ev.isDown()) {
        // Matched against the held code, not the configured one, and handled even outside
        // gameplay -- releasing the key after opening chat still has to clear the held state.
        if (gHeldKeyCode == 0 || ev.keyCode() != gHeldKeyCode) {
            return;
        }
        gHeldKeyCode = 0;
        if (config.zoom.activation == ActivationMode::Hold) {
            zoom.setActive(false);
        }
        return;
    }

    if (ev.keyCode() != config.zoom.keyCode) {
        return;
    }

    // Auto-repeat sends a stream of key-downs while the key is held; only the first one counts,
    // otherwise a toggle binding would flicker on and off many times a second.
    if (gHeldKeyCode != 0 || !isInGameplay()) {
        return;
    }
    gHeldKeyCode = ev.keyCode();

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

    int const action = ev.actionButtonId();

    if (action == ::MouseAction::ActionWheel) {
        if (config.zoom.scrollToAdjust && zoom.isActive()) {
            // buttonData carries the signed notch count. Zero means the event says nothing about
            // direction, so it must not be read as a scroll-out; the notch is still swallowed so
            // the hotbar selection does not move while zoomed.
            if (int const notches = ev.buttonData(); notches != 0) {
                zoom.adjustFactorByScroll(std::clamp(notches, -5, 5));
            }
            ev.cancel();
        }
        return;
    }

    double const factor    = sensitivityFactor();
    bool const   cinematic = config.cinematic.enabled;
    double const strength  = std::clamp(config.cinematic.strength, 0.0, 0.95);

    if (gDebugEventsLeft > 0) {
        --gDebugEventsLeft;
        Zoomidy::getInstance().getSelf().getLogger().info(
            "mouse action={} data={} x={} y={} dx={} dy={} factor={:.3f} divisor={:.3f} cine={} strength={:.2f}",
            action,
            static_cast<int>(ev.buttonData()),
            ev.x(),
            ev.y(),
            ev.dx(),
            ev.dy(),
            factor,
            zoom.currentDivisor(),
            cinematic,
            strength
        );
        if (gDebugEventsLeft == 0) {
            Zoomidy::getInstance().getSelf().getLogger().info("mouse debug capture finished.");
        }
    }

    if (factor == 1.0 && !cinematic) {
        return;
    }

    // Any event that carries movement is filtered, not just ActionMoveRelative. Which action id
    // the client uses for a pointer-locked camera is not something the headers pin down, and a
    // motionless event is a no-op through the filter anyway, so matching on the delta rather than
    // on the action is both safer and cheaper than guessing.
    if (ev.dx() == 0 && ev.dy() == 0) {
        return;
    }

    // Both axes share one release fraction, and it is computed once: asking for it twice would
    // advance the clock in between and tilt the smoothing towards whichever axis went second.
    double const release = cinematic ? cinematicRelease(strength) : 1.0;

    ev.dx() = filterAxis(ev.dx(), factor, release, gResidualX, gCinematicX);
    ev.dy() = filterAxis(ev.dy(), factor, release, gResidualY, gCinematicY);
}

void onClientTick(ll::event::ClientLevelTickEvent&) {
    if (isInGameplay()) {
        return;
    }

    // Alt-tabbing or opening a screen can swallow the key-up entirely. Forgetting the held key
    // in every mode is what keeps that from disabling the zoom key for the rest of the session.
    gHeldKeyCode = 0;

    // A held zoom is dropped with it. Toggled zoom is deliberately left alone: it survives
    // menus, like Zoomify's does.
    auto const& config = Zoomidy::getInstance().getConfig();
    auto&       zoom   = ZoomState::getInstance();
    if (config.zoom.activation == ActivationMode::Hold && zoom.isActive()) {
        zoom.setActive(false);
    }
}

/// Leaving the world while zoomed would otherwise carry the zoom into the next one, because
/// nothing else clears the state between sessions.
void onExitLevel(ll::event::ClientExitLevelEvent&) {
    gHeldKeyCode = 0;
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
    gHeldKeyCode = 0;
    resetFilters();
}

int enableInputDebug(bool enabled) {
    constexpr int kDebugEventBudget = 60;
    gDebugEventsLeft                = enabled ? kDebugEventBudget : 0;
    return gDebugEventsLeft;
}

} // namespace zoomidy
