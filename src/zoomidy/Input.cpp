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

#include "zoomidy/CameraDrift.h"
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

/// Remaining mouse events to dump to the log for `/zoomidy debug`. Counts down to zero so the
/// diagnostic cannot be left on by accident.
int gDebugEventsLeft = 0;

void resetFilters() {
    gResidualX = 0.0;
    gResidualY = 0.0;
}

/// True when the player is actually flying the camera around: pointer locked, no chat box, no
/// inventory, no pause menu. Zooming while a screen is open would be both useless and confusing.
bool isInGameplay() {
    auto client = ll::service::getClientInstance();
    return client && client->getMouseGrabbed();
}

} // namespace

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

namespace {

/// Applies the sensitivity scale and quantises back to the whole numbers the client expects,
/// carrying the sub-count remainder forward so that slow aiming survives a heavy scale.
short filterAxis(short raw, double factor, double& residual) {
    double const value   = static_cast<double>(raw) * factor + residual;
    double const rounded = std::round(value);
    residual             = value - rounded;

    return static_cast<short>(std::clamp(
        rounded,
        static_cast<double>(std::numeric_limits<short>::min()),
        static_cast<double>(std::numeric_limits<short>::max())
    ));
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

void filterMouseInput(ll::event::MouseInputEvent& ev) {
    // The drift's own synthetic events come back through this listener. Filtering them again
    // would scale the sensitivity twice and feed the result straight back into the pool.
    if (drift::isInjecting()) {
        return;
    }

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

    if (factor == 1.0 && !cinematic) {
        return;
    }

    // Any event that carries movement is handled, not just ActionMoveRelative: a capture showed a
    // pointer-locked camera arriving as ActionMove with the pointer position held still and the
    // movement in the delta, and a motionless event is a no-op either way.
    if (ev.dx() == 0 && ev.dy() == 0) {
        return;
    }

    short const rawDx = ev.dx();
    short const rawDy = ev.dy();

    if (cinematic) {
        // Hand the movement to the drift and take it out of this event. The drift pays it back
        // over the following frames, which is the only way the camera can keep turning after the
        // mouse stops -- there are no mouse events left to carry it.
        //
        // Passed raw: the drift applies sensitivity as it pays out, so a flick that starts before
        // the zoom finishes arriving is slowed by the zoom rather than by whatever the scale
        // happened to be when the mouse moved.
        drift::absorb(rawDx, rawDy);
        ev.dx() = 0;
        ev.dy() = 0;
    } else {
        ev.dx() = filterAxis(rawDx, factor, gResidualX);
        ev.dy() = filterAxis(rawDy, factor, gResidualY);
    }
}

/// Runs the filter, and logs what went in and what came out while a capture is armed.
///
/// The logging deliberately wraps the filter rather than sitting inside it. Every interesting way
/// this can go wrong is a way the filter returns early -- no zoom engaged, an action id that is
/// not what was expected, a delta of zero -- and a log line written past all of those can only
/// ever report the cases that already work. Wrapped, a capture that stays empty says the listener
/// is not being called at all, which is a different problem with a different fix.
void onMouseInput(ll::event::MouseInputEvent& ev) {
    if (gDebugEventsLeft <= 0) {
        filterMouseInput(ev);
        return;
    }

    int const   action    = ev.actionButtonId();
    int const   data      = ev.buttonData();
    short const rawDx     = ev.dx();
    short const rawDy     = ev.dy();
    bool const  injecting = drift::isInjecting();

    filterMouseInput(ev);

    auto const& zoom = ZoomState::getInstance();

    --gDebugEventsLeft;
    Zoomidy::getInstance().getSelf().getLogger().info(
        "mouse action={} data={} in=({},{}) out=({},{}) engaged={} active={} factor={:.3f} "
        "divisor={:.3f} cine={} injecting={} drifting={}",
        action,
        data,
        rawDx,
        rawDy,
        ev.dx(),
        ev.dy(),
        zoom.isEngaged(),
        zoom.isActive(),
        sensitivityFactor(),
        zoom.currentDivisor(),
        Zoomidy::getInstance().getConfig().cinematic.enabled,
        injecting,
        drift::isDraining()
    );
    if (gDebugEventsLeft == 0) {
        Zoomidy::getInstance().getSelf().getLogger().info("mouse debug capture finished.");
    }
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
    drift::reset();
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
