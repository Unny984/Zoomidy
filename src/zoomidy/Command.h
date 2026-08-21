#pragma once

namespace zoomidy {

/// Arranges for `/zoomidy` to be registered into the client's command registry every time the
/// registry is rebuilt, which happens on each world or server join.
void registerCommand();

void unregisterCommand();

} // namespace zoomidy
