#pragma once

#include <string>

namespace zoomidy::ui {

/// Why the settings form cannot be opened right now, or an empty string if it can.
///
/// The form is a data-driven UI screen, and Bedrock only knows how to show one of those to a
/// player that belongs to a *server*. On a client that is hosting its own world there is one, so
/// the form opens; when connected to somebody else's server there is not.
[[nodiscard]] std::string describeUnavailability();

/// Builds and shows the settings form. Hops to the server thread on its own.
/// Call `describeUnavailability()` first and report that instead if it returns anything.
void openSettingsForm();

} // namespace zoomidy::ui
