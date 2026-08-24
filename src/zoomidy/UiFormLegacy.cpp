/// The settings screen for LeviLamina 26.10 and earlier, which predate the observable-backed UI
/// API that `UiFormObservable.cpp` is built on. Exactly one of the two compiles to anything.
///
/// It is a menu of buttons rather than a single page of controls, and that is not a stylistic
/// choice. The only screen on these SDKs that can report anything before it closes is the simple
/// form, whose buttons each carry their own callback; a custom form is filled in, submitted once,
/// and read back as a whole. A page of controls therefore cannot save as you go -- it can only
/// save when you scroll to the bottom and press a submit button, which is the thing this screen
/// exists to avoid.
///
/// So every button here applies its change immediately, writes it to disk, and re-opens the menu
/// showing the new value. Nothing is staged, so there is nothing to submit and nothing to lose by
/// closing the screen.
#if !__has_include("ll/api/ui/form/CustomForm.h")

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

#include "zoomidy/Config.h"
#include "zoomidy/KeyNames.h"
#include "zoomidy/UiForm.h"
#include "zoomidy/Zoomidy.h"

namespace zoomidy::ui {

namespace {

void showMain(Player& player);

Config current() { return Zoomidy::getInstance().getConfig(); }

/// Writes a change out and brings the player back to `next`.
///
/// Re-opening goes through `onServerThread` rather than sending the form from here: this runs
/// inside the handler for the response packet of the screen that was just clicked in, and a form
/// sent from there can reach the client before it has finished closing the old one.
void commit(Config const& config, void (*next)(Player&)) {
    applyConfig(config);
    onServerThread(next);
}

std::string onOff(bool on) { return on ? "§aON" : "§8OFF"; }

/// Marks the value a setting currently holds, so a menu of choices says which one is live.
std::string mark(std::string_view text, bool chosen) {
    return chosen ? std::format("§a{} §r§7(current)", text) : std::string{text};
}

// ---------------------------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------------------------

/// A numeric setting, and everything a menu needs to show it and nudge it.
///
/// The accessors are plain function pointers so that the whole table can be a constant: a stepper
/// menu re-opens itself after every press, and each re-open has to point back at the same
/// descriptor without anything having to own it.
struct Stepper {
    std::string_view name;
    std::string_view help;
    std::string_view unit;
    double           coarse;
    double           fine;
    double (*get)(Config const&);
    void (*set)(Config&, double);
    double (*lowest)(Config const&);
    double (*highest)(Config const&);
};

constexpr Stepper kMagnification{
    .name    = "Magnification",
    .help    = "How far in the zoom goes. 4x means the view is four times closer.",
    .unit    = "x",
    .coarse  = 1.0,
    .fine    = 0.5,
    .get     = +[](Config const& c) { return c.zoom.factor; },
    .set     = +[](Config& c, double v) { c.zoom.factor = v; },
    .lowest  = +[](Config const& c) { return factorRange(c).first; },
    .highest = +[](Config const& c) { return factorRange(c).second; },
};

constexpr Stepper kTransition{
    .name    = "Transition",
    .help    = "Time spent easing in, and again easing out. 0 snaps instantly.",
    .unit    = " ms",
    .coarse  = 100.0,
    .fine    = kDurationStepMs,
    .get     = +[](Config const& c) { return c.animation.durationSeconds * 1000.0; },
    .set     = +[](Config& c, double v) { c.animation.durationSeconds = v / 1000.0; },
    .lowest  = +[](Config const&) { return kDurationMinMs; },
    .highest = +[](Config const&) { return kDurationMaxMs; },
};

constexpr Stepper kMultiplier{
    .name = "Sensitivity multiplier",
    .help = "100% leaves the result of the mode alone. Relative multiplies this on top; Fixed "
            "uses it by itself.",
    .unit    = "%",
    .coarse  = 25.0,
    .fine    = kPercentStep,
    .get     = +[](Config const& c) { return c.sensitivity.multiplier * 100.0; },
    .set     = +[](Config& c, double v) { c.sensitivity.multiplier = v / 100.0; },
    .lowest  = +[](Config const&) { return kMultiplierMinPc; },
    .highest = +[](Config const&) { return kMultiplierMaxPc; },
};

constexpr Stepper kSmoothing{
    .name    = "Cinematic smoothing",
    .help    = "Higher is heavier. 0% is no smoothing.",
    .unit    = "%",
    .coarse  = 25.0,
    .fine    = kPercentStep,
    .get     = +[](Config const& c) { return c.cinematic.strength * 100.0; },
    .set     = +[](Config& c, double v) { c.cinematic.strength = v / 100.0; },
    .lowest  = +[](Config const&) { return kCinematicMinPc; },
    .highest = +[](Config const&) { return kCinematicMaxPc; },
};

constexpr Stepper kWheelStep{
    .name    = "Wheel step",
    .help    = "How much one notch multiplies the magnification by. 120% steps by a fifth each time.",
    .unit    = "%",
    .coarse  = 20.0,
    .fine    = kPercentStep,
    .get     = +[](Config const& c) { return c.zoom.scrollStep * 100.0; },
    .set     = +[](Config& c, double v) { c.zoom.scrollStep = v / 100.0; },
    .lowest  = +[](Config const&) { return kScrollStepMinPc; },
    .highest = +[](Config const&) { return kScrollStepMaxPc; },
};

std::string valueText(Stepper const& stepper, Config const& config) {
    return std::format("{:g}{}", stepper.get(config), stepper.unit);
}

void showStepper(Player& player, Stepper const& stepper) {
    Config const config = current();

    ll::form::SimpleForm form{
        std::format("{}: {}", stepper.name, valueText(stepper, config)),
        std::format(
            "{}\n\n§7Range {:g}{} to {:g}{}.",
            stepper.help,
            stepper.lowest(config),
            stepper.unit,
            stepper.highest(config),
            stepper.unit
        )
    };

    // Each press re-reads the config rather than working from the copy above: the menu re-opens
    // after every press, and the value it was built from is one press out of date by the second.
    auto nudge = [&form, &stepper](std::string const& label, double delta) {
        form.appendButton(label, [&stepper, delta](Player&) {
            Config config = current();
            stepper.set(
                config,
                std::clamp(stepper.get(config) + delta, stepper.lowest(config), stepper.highest(config))
            );
            applyConfig(config);
            onServerThread([&stepper](Player& player) { showStepper(player, stepper); });
        });
    };

    nudge(std::format("§c-- {:g}{}", stepper.coarse, stepper.unit), -stepper.coarse);
    nudge(std::format("§c- {:g}{}", stepper.fine, stepper.unit), -stepper.fine);
    nudge(std::format("§a+ {:g}{}", stepper.fine, stepper.unit), stepper.fine);
    nudge(std::format("§a++ {:g}{}", stepper.coarse, stepper.unit), stepper.coarse);

    form.appendDivider();
    form.appendButton("§7Back", [](Player&) { onServerThread(showMain); });

    form.sendTo(player);
}

// ---------------------------------------------------------------------------------------------
// Choices
// ---------------------------------------------------------------------------------------------

/// One of the shared option tables, wired to the setting it drives.
struct Choice {
    std::string_view        name;
    std::span<Option const> options;
    double (*get)(Config const&);
    void (*set)(Config&, double);
};

constexpr Choice kActivationChoice{
    .name    = "Activation",
    .options = kActivationOptions,
    .get     = +[](Config const& c) { return static_cast<double>(c.zoom.activation); },
    .set     = +[](Config& c, double v) { c.zoom.activation = static_cast<ActivationMode>(static_cast<int>(v)); },
};

constexpr Choice kCurveChoice{
    .name    = "Transition curve",
    .options = kCurveOptions,
    .get     = +[](Config const& c) { return static_cast<double>(c.animation.curve); },
    .set     = +[](Config& c, double v) { c.animation.curve = static_cast<EasingCurve>(static_cast<int>(v)); },
};

constexpr Choice kSensitivityChoice{
    .name    = "Sensitivity mode",
    .options = kSensitivityOptions,
    .get     = +[](Config const& c) { return static_cast<double>(c.sensitivity.mode); },
    .set     = +[](Config& c, double v) { c.sensitivity.mode = static_cast<SensitivityMode>(static_cast<int>(v)); },
};

/// The label of whichever option a config currently holds. A config naming an enumerator this
/// build does not know about falls back to the first entry, which is also what the menu writes
/// back if the player picks anything.
std::string_view labelOf(Choice const& choice, Config const& config) {
    double const value = choice.get(config);
    for (auto const& option : choice.options) {
        if (option.value == value) {
            return option.label;
        }
    }
    return choice.options.front().label;
}

void showChoice(Player& player, Choice const& choice) {
    Config const config = current();
    double const value  = choice.get(config);

    ll::form::SimpleForm form{std::string{choice.name}, "§7Picking an option applies it straight away."};

    for (auto const& option : choice.options) {
        // A custom form has one tooltip per control and none per option, so the descriptions had
        // to be crammed into the tooltip of the dropdown itself. A button can carry its own.
        std::string text = mark(option.label, option.value == value);
        if (!option.description.empty()) {
            text += std::format("\n§7{}", option.description);
        }
        double const picked = option.value;
        form.appendButton(text, [&choice, picked](Player&) {
            Config config = current();
            choice.set(config, picked);
            commit(config, showMain);
        });
    }

    form.appendDivider();
    form.appendButton("§7Back", [](Player&) { onServerThread(showMain); });

    form.sendTo(player);
}

// ---------------------------------------------------------------------------------------------
// The zoom key
// ---------------------------------------------------------------------------------------------

// Enough keys to cover the sensible bindings without turning into a keyboard. Anything else is
// still reachable through `/zoomidy key`, which takes a name or a raw code: a menu of buttons has
// no way to offer a text field, and no screen on this SDK can read one back before it closes.
constexpr std::array kLetterKeys{'B', 'C', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'Q', 'R', 'T', 'V', 'X', 'Y', 'Z'};

constexpr std::array kOtherKeys{
    0x20, // SPACE
    0x09, // TAB
    0x14, // CAPSLOCK
    0xA0, // LSHIFT
    0xA2, // LCTRL
    0x12, // ALT
    0x2D, // INSERT
    0x24, // HOME
    0x21, // PAGEUP
    0x22, // PAGEDOWN
    0x70, // F1
    0x71, // F2
    0x72, // F3
    0x73, // F4
    0x74, // F5
    0x75, // F6
    0x76, // F7
    0x77, // F8
};

void showKeys(Player& player) {
    Config const config = current();

    ll::form::SimpleForm form{
        "Zoom key",
        std::format(
            "§7Currently §f{}§7.\n§7Any other key: §f/zoomidy key <name or 0x code>"
            "§7.\n§7Pick one Minecraft does not already use -- the key is watched, not taken over.",
            keynames::format(config.zoom.keyCode)
        )
    };

    auto append = [&form, &config](int code) {
        form.appendButton(mark(keynames::format(code), code == config.zoom.keyCode), [code](Player&) {
            Config config       = current();
            config.zoom.keyCode = code;
            commit(config, showMain);
        });
    };

    form.appendHeader("Letters");
    for (char const key : kLetterKeys) {
        append(key);
    }

    form.appendDivider();
    form.appendHeader("Other keys");
    for (int const key : kOtherKeys) {
        append(key);
    }

    form.appendDivider();
    form.appendButton("§7Back", [](Player&) { onServerThread(showMain); });

    form.sendTo(player);
}

// ---------------------------------------------------------------------------------------------
// The menu itself
// ---------------------------------------------------------------------------------------------

void showReset(Player& player) {
    ll::form::SimpleForm form{
        "Reset Zoomidy",
        "§7This puts every setting back to the value it shipped with, including the zoom key."
    };

    form.appendButton("§cYes, reset everything", [](Player&) { commit(Config{}, showMain); });
    form.appendButton("§7No, go back", [](Player&) { onServerThread(showMain); });

    form.sendTo(player);
}

void showMain(Player& player) {
    Config const config = current();

    ll::form::SimpleForm form{"Zoomidy", "§7Every change is applied and saved the moment you make it."};

    auto toggle = [&form, &config](std::string_view label, bool (*get)(Config const&), void (*set)(Config&, bool)) {
        form.appendButton(std::format("{}: {}", label, onOff(get(config))), [get, set](Player&) {
            Config config = current();
            set(config, !get(config));
            commit(config, showMain);
        });
    };

    auto choice = [&form, &config](Choice const& target) {
        form.appendButton(
            std::format("{}: §b{}", target.name, labelOf(target, config)),
            [&target](Player&) { onServerThread([&target](Player& player) { showChoice(player, target); }); }
        );
    };

    auto stepper = [&form, &config](Stepper const& target) {
        form.appendButton(
            std::format("{}: §b{}", target.name, valueText(target, config)),
            [&target](Player&) { onServerThread([&target](Player& player) { showStepper(player, target); }); }
        );
    };

    form.appendHeader("Zoom");
    form.appendButton(std::format("Zoom key: §b{}", keynames::format(config.zoom.keyCode)), [](Player&) {
        onServerThread(showKeys);
    });
    choice(kActivationChoice);
    stepper(kMagnification);
    form.appendDivider();

    form.appendHeader("Animation");
    stepper(kTransition);
    choice(kCurveChoice);
    form.appendDivider();

    form.appendHeader("View");
    toggle(
        "Hide hand while zoomed",
        +[](Config const& c) { return c.view.hideHand; },
        +[](Config& c, bool v) { c.view.hideHand = v; }
    );
    form.appendDivider();

    form.appendHeader("Sensitivity");
    choice(kSensitivityChoice);
    stepper(kMultiplier);
    form.appendDivider();

    form.appendHeader("Cinematic camera");
    toggle(
        "Smooth the camera while zoomed",
        +[](Config const& c) { return c.cinematic.enabled; },
        +[](Config& c, bool v) { c.cinematic.enabled = v; }
    );
    stepper(kSmoothing);
    form.appendDivider();

    form.appendHeader("Scroll wheel");
    toggle(
        "Adjust magnification with the wheel",
        +[](Config const& c) { return c.zoom.scrollToAdjust; },
        +[](Config& c, bool v) { c.zoom.scrollToAdjust = v; }
    );
    stepper(kWheelStep);
    toggle(
        "Remember wheel adjustment",
        +[](Config const& c) { return c.zoom.rememberScrolledFactor; },
        +[](Config& c, bool v) { c.zoom.rememberScrolledFactor = v; }
    );
    form.appendDivider();

    form.appendButton("§cReset everything to defaults", [](Player&) { onServerThread(showReset); });

    form.sendTo(player);
}

} // namespace

void buildAndShow(Player& player) { showMain(player); }

} // namespace zoomidy::ui

#endif
