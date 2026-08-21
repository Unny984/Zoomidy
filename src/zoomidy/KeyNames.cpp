#include "zoomidy/KeyNames.h"

#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>

namespace zoomidy::keynames {

namespace {

// Only the keys that make sense as a hold-to-zoom binding. Modifier keys are listed by side
// because the mod compares against the raw virtual-key code the client reports.
constexpr std::array<std::pair<std::string_view, int>, 34> kNamedKeys{{
    {"BACKSPACE", 0x08},
    {"TAB",       0x09},
    {"ENTER",     0x0D},
    {"SHIFT",     0x10},
    {"CTRL",      0x11},
    {"ALT",       0x12},
    {"CAPSLOCK",  0x14},
    {"ESC",       0x1B},
    {"SPACE",     0x20},
    {"PAGEUP",    0x21},
    {"PAGEDOWN",  0x22},
    {"END",       0x23},
    {"HOME",      0x24},
    {"LEFT",      0x25},
    {"UP",        0x26},
    {"RIGHT",     0x27},
    {"DOWN",      0x28},
    {"INSERT",    0x2D},
    {"DELETE",    0x2E},
    {"F1",        0x70},
    {"F2",        0x71},
    {"F3",        0x72},
    {"F4",        0x73},
    {"F5",        0x74},
    {"F6",        0x75},
    {"F7",        0x76},
    {"F8",        0x77},
    {"F9",        0x78},
    {"F10",       0x79},
    {"F11",       0x7A},
    {"F12",       0x7B},
    {"LSHIFT",    0xA0},
    {"RSHIFT",    0xA1},
    {"LCTRL",     0xA2},
}};

std::string toUpperTrimmed(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

std::optional<int> parse(std::string_view text) {
    std::string const key = toUpperTrimmed(text);
    if (key.empty()) {
        return std::nullopt;
    }

    // A bare letter or digit maps straight onto its virtual-key code.
    if (key.size() == 1) {
        char const c = key.front();
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<int>(c);
        }
    }

    for (auto const& [name, code] : kNamedKeys) {
        if (key == name) {
            return code;
        }
    }

    // Explicit codes, so an exotic key can still be bound by hand.
    int         value = 0;
    char const* begin = key.data();
    char const* end   = key.data() + key.size();
    int         base  = 10;
    if (key.size() > 2 && key[0] == '0' && key[1] == 'X') {
        begin += 2;
        base   = 16;
    }
    if (auto const [ptr, ec] = std::from_chars(begin, end, value, base); ec == std::errc{} && ptr == end) {
        if (value > 0 && value <= 0xFF) {
            return value;
        }
    }

    return std::nullopt;
}

std::string format(int keyCode) {
    if ((keyCode >= 'A' && keyCode <= 'Z') || (keyCode >= '0' && keyCode <= '9')) {
        return std::string(1, static_cast<char>(keyCode));
    }
    for (auto const& [name, code] : kNamedKeys) {
        if (code == keyCode) {
            return std::string{name};
        }
    }
    return std::format("0x{:02X}", keyCode);
}

} // namespace zoomidy::keynames
