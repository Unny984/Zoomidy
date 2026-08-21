#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace zoomidy::keynames {

/// Turns a human-typed key name into a Windows virtual-key code.
///
/// Accepts a single letter or digit (`f`, `Z`, `3`), a named key (`F5`, `SPACE`, `LSHIFT`,
/// `CAPSLOCK`, `TAB`, ...), or an explicit code (`0x46`, `70`). Returns nothing if the text does
/// not name a key this mod can bind.
[[nodiscard]] std::optional<int> parse(std::string_view text);

/// The display name for a virtual-key code, for showing the current binding back to the user.
/// Unknown codes come back as `0x..` so the value is never lost.
[[nodiscard]] std::string format(int keyCode);

} // namespace zoomidy::keynames
