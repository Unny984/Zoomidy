#pragma once

namespace zoomidy {

/// Starts listening for the zoom key and for mouse movement.
void registerInputListeners();

void unregisterInputListeners();

/// Logs the raw mouse events the client delivers while zoomed, so the exact shape of the camera
/// input can be confirmed rather than guessed at. Switches itself back off after a short burst so
/// an enabled session cannot flood the log. Returns the number of events that will be recorded.
int enableInputDebug(bool enabled);

} // namespace zoomidy
