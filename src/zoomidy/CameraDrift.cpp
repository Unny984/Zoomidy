#include "zoomidy/CameraDrift.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"

#include "mc/deps/input/ControllerIDtoClientMap.h"
#include "mc/deps/input/InputEventQueue.h"
#include "mc/deps/input/Mouse.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseMapper.h"

#include "zoomidy/Input.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::drift {

namespace {

/// Motion the camera still owes, in raw mouse counts.
///
/// Deliberately *not* scaled for sensitivity on the way in. The zoom animates over 200 ms by
/// default, so movement banked before the zoom engaged would otherwise be paid out at the
/// unzoomed sensitivity and send the camera flying once the zoom had arrived. The scale belongs
/// at the moment the movement is applied, not at the moment it is recorded.
double gPendingX = 0.0;
double gPendingY = 0.0;

/// Sub-count remainders, so a slow drain never rounds away to nothing.
double gResidualX = 0.0;
double gResidualY = 0.0;

std::chrono::steady_clock::time_point gLastFrame{};
bool                                  gHasLastFrame = false;

/// Set while this file is feeding the mouse device, so the input listener can recognise its own
/// event coming back around and pass it straight through.
bool gInjecting = false;

/// How long the coast lasts at the maximum smoothing setting, in seconds.
constexpr double kMaxTimeConstant = 0.5;

/// A ceiling on how much motion can be owed at once. Nothing should ever approach this; it is
/// here so that a mistake somewhere upstream cannot turn into a camera that spins on its own.
constexpr double kMaxPending = 4000.0;

/// Below this the remaining motion is not worth a synthetic event, so the drain stops rather than
/// trickling forever.
constexpr double kSettled = 0.25;

/// The longest frame the drain will account for. A hitch would otherwise release most of what is
/// owed in a single frame, which is exactly the jump this whole mechanism exists to avoid.
constexpr double kMaxFrameSeconds = 0.1;

short quantise(double value, double& residual) {
    double const carried = value + residual;
    double const rounded = std::round(carried);
    residual             = carried - rounded;
    return static_cast<short>(std::clamp(
        rounded,
        static_cast<double>(std::numeric_limits<short>::min()),
        static_cast<double>(std::numeric_limits<short>::max())
    ));
}

/// Releases one frame's worth of the outstanding motion back into the game as a mouse event.
void drainOneFrame() {
    auto const& config = Zoomidy::getInstance().getConfig();
    if (!config.cinematic.enabled) {
        reset();
        return;
    }

    // Never synthesise movement once the pointer is released. Whatever is still owed belongs to a
    // turn the player was making in the world, and pushing it into an open chat box or menu is at
    // best pointless and at worst confusing.
    auto client = ll::service::getClientInstance();
    if (!client || !client->getMouseGrabbed()) {
        reset();
        return;
    }

    if (std::abs(gPendingX) < kSettled && std::abs(gPendingY) < kSettled) {
        gPendingX     = 0.0;
        gPendingY     = 0.0;
        gHasLastFrame = false;
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    double     dt  = 1.0 / 60.0;
    if (gHasLastFrame) {
        dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - gLastFrame).count();
    }
    gLastFrame    = now;
    gHasLastFrame = true;
    dt            = std::clamp(dt, 0.0, kMaxFrameSeconds);

    double const tau     = std::clamp(config.cinematic.strength, 0.0, 1.0) * kMaxTimeConstant;
    double const release = tau <= 0.0 ? 1.0 : 1.0 - std::exp(-dt / tau);

    // Both axes are released by the same fraction. That keeps the drain linear and therefore
    // keeps it from bending the direction of the turn -- a circular movement has to come out
    // circular, which a per-axis rule would not give.
    double const rawOutX = gPendingX * release;
    double const rawOutY = gPendingY * release;
    gPendingX -= rawOutX;
    gPendingY -= rawOutY;

    // Scaled here, against the zoom as it stands this frame, rather than against whatever it was
    // when the movement came in.
    double const factor = sensitivityFactor();

    short const dx = quantise(rawOutX * factor, gResidualX);
    short const dy = quantise(rawOutY * factor, gResidualY);
    if (dx == 0 && dy == 0) {
        return;
    }

    // Shaped like the events the client sends for a pointer-locked camera, confirmed by capture:
    // action 0 with the pointer position held still and the movement carried in the delta.
    gInjecting = true;
    ::Mouse::feed(::MouseAction::ActionMove, 0, ::Mouse::getX(), ::Mouse::getY(), dx, dy);
    gInjecting = false;
}

} // namespace

/// Runs once per frame, immediately before the mapper drains the queue, so anything pushed here
/// is picked up in the same frame. This is also the thread the real mouse events arrive on, which
/// is why the drain lives here rather than on a render hook.
LL_TYPE_INSTANCE_HOOK(
    ZoomidyCameraDriftHook,
    ll::memory::HookPriority::Normal,
    MouseMapper,
    &MouseMapper::$tick,
    bool,
    ::InputEventQueue&                                          eventQueue,
    ::Bedrock::NotNullNonOwnerPtr<::ControllerIDtoClientMap> const& clientMap
) {
    drainOneFrame();
    return origin(eventQueue, clientMap);
}

void absorb(double dx, double dy) {
    gPendingX = std::clamp(gPendingX + dx, -kMaxPending, kMaxPending);
    gPendingY = std::clamp(gPendingY + dy, -kMaxPending, kMaxPending);
}

bool isInjecting() { return gInjecting; }

bool isDraining() { return std::abs(gPendingX) >= kSettled || std::abs(gPendingY) >= kSettled; }

void reset() {
    gPendingX     = 0.0;
    gPendingY     = 0.0;
    gResidualX    = 0.0;
    gResidualY    = 0.0;
    gHasLastFrame = false;
}

void registerHook() { ll::memory::HookRegistrar<ZoomidyCameraDriftHook>::hook(); }

void unregisterHook() {
    ll::memory::HookRegistrar<ZoomidyCameraDriftHook>::unhook();
    reset();
}

} // namespace zoomidy::drift
