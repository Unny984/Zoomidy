#pragma once

namespace zoomidy {

/// Installs the FOV and first-person-hand hooks.
void registerHooks();

/// Removes them again. Safe to call even if `registerHooks` was never called.
void unregisterHooks();

} // namespace zoomidy
