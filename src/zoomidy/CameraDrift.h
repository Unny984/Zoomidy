#pragma once

namespace zoomidy::drift {

/// Gives the camera weight by holding movement back and paying it out over the following frames,
/// including frames where the mouse reported nothing at all.
///
/// This is what makes the camera coast after a flick instead of stopping dead. Earlier attempts
/// filtered the mouse events in place, which cannot work: a camera that keeps turning after the
/// mouse stops has to be driven by something other than mouse events, and there are none. So the
/// outstanding motion is drained on a per-frame hook and pushed back in as a synthetic event.
///
/// Only active while the cinematic option is on. With it off, nothing here runs and no events are
/// synthesised.

/// Takes a raw mouse delta into the pool of motion still owed. The caller must zero the event's
/// own delta afterwards, or the movement is applied twice.
///
/// Pass the delta unscaled. Sensitivity is applied when the motion is paid out, so that movement
/// banked before the zoom engaged is slowed down by the zoom that has arrived since.
void absorb(double dx, double dy);

/// True while the per-frame drain is pushing its own event through the mouse device, so the input
/// listener can tell a synthetic event from a real one and leave it alone.
[[nodiscard]] bool isInjecting();

/// Whether any motion is still owed.
[[nodiscard]] bool isDraining();

/// Throws away everything owed. Used when leaving a world, and when the mod is disabled.
void reset();

void registerHook();
void unregisterHook();

} // namespace zoomidy::drift
