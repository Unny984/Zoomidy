# Zoomidy

Smooth, animated zoom for **Minecraft Bedrock Edition** on Windows, in the spirit of
[Zoomify](https://modrinth.com/mod/zoomify) and
[MCBE-Win10-FOV-Changer](https://github.com/xroix/MCBE-Win10-FOV-Changer).

Built as a **client-side [LeviLamina](https://github.com/LiteLDev/LeviLamina) mod**.

## Features

| | |
|---|---|
| **Hold `F` to zoom** | Or switch to toggle. The key is rebindable to any keyboard key. |
| **Animated transition** | 0.2 s by default, with six easing curves to pick from. |
| **Adjustable magnification** | 4x by default; the scroll wheel changes it live while zoomed. |
| **Hide the hand** | The first-person arm and held item disappear while zoomed. |
| **Sensitivity scaling** | Relative (follows the zoom), fixed, or off. |
| **Cinematic camera** | Optional. The camera lags on the way in and coasts out after a flick. |
| **In-game settings screen** | `/zoomidy` opens a real Minecraft form. |

## Requirements

- Windows 10/11 x64
- LeviLamina **26.20.x** installed on the client, via
  [LeviLauncher](https://lamina.levimc.org/user_guides/install_on_client/)
- Minecraft Bedrock Edition client **1.26.20.4** — the version LeviLamina 26.20.x supports

> The Store copy of Minecraft is usually newer than whatever LeviLamina supports, and the hooks
> here are resolved against the exact game binary. Use LeviLauncher to download and switch to the
> supported client version; it keeps it separate from your Store install.

## Install

Drop the built `zoomidy` folder into your client's `mods/` directory, or install with `lip`:

```bash
lip install github.com/Unny984/Zoomidy
```

## Usage

Hold `F`. That is the whole thing.

Zoomidy watches the key rather than taking it over, so pick one Minecraft does not already use.
If you bind it to a key that already does something, both will happen — but nothing gets locked
away, and you can always rebind from the config file.

### Settings screen

```
/zoomidy
```

> **The settings form only opens in a world this client is hosting** — single-player, or a world
> you opened to LAN. Minecraft's form system belongs to the *server* half of the game, and there
> is no server half to talk to when you are a guest on somebody else's world. The sub-commands
> below work everywhere, and so does editing the config file.

### Sub-commands

These work in every world, including remote servers, because they never touch the form system.

```
/zoomidy status
/zoomidy reload
/zoomidy key <F|Z|SPACE|LSHIFT|F5|0x46|...>
/zoomidy activation <Hold|Toggle>
/zoomidy magnification <1.5-50>
/zoomidy transition <milliseconds> [curve]
/zoomidy hidehand <true|false>
/zoomidy sensitivity <Off|Relative|Fixed> [multiplier]
/zoomidy cinematic <true|false> [strength]
```

`/zoomidy` is registered on the client, so the command never reaches the server you are playing
on and needs no permissions there.

### Config file

`mods/zoomidy/config/config.json`. Edit it and run `/zoomidy reload`, or restart the game.

```json
{
  "version": 1,
  "zoom": {
    "keyCode": 70,
    "activation": "Hold",
    "factor": 4.0,
    "minFactor": 1.5,
    "maxFactor": 50.0,
    "scrollToAdjust": true,
    "scrollStep": 1.2,
    "rememberScrolledFactor": false
  },
  "animation": { "durationSeconds": 0.2, "curve": "EaseOutQuad" },
  "view": { "hideHand": true },
  "sensitivity": { "mode": "Relative", "multiplier": 1.0 },
  "cinematic": { "enabled": false, "strength": 0.6 }
}
```

`keyCode` is a [Windows virtual-key code](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes);
70 (`0x46`) is `F`.

## How it works

| Piece | Mechanism |
|---|---|
| The zoom | Hooks `LevelRendererPlayer::getFov` and divides the answer. |
| Hiding the hand | Skips `ItemInHandRenderer::renderFirstPerson` for the frame. |
| The key | Listens to `ll::event::KeyInputEvent`, gated on the pointer being locked, so it does not fire while you are typing in chat. |
| Sensitivity | Rewrites `dx`/`dy` on `ll::event::MouseInputEvent`, carrying the sub-count remainder forward so slow aiming survives a 0.25x scale. |
| Cinematic camera | Banks the movement, then drains it a frame at a time from a `MouseMapper::tick` hook and feeds it back through `Mouse::feed`. |
| Scroll to adjust | Cancels the wheel event while zoomed, so the hotbar does not move with it. |
| Settings screen | `ll::ui::CustomForm`, built on the server thread. |

### Why the camera coast needs its own hook

A camera that keeps turning after the mouse stops cannot be driven by mouse events, because during
a coast there are none. Filtering the events in place can only ever slow the camera down. So the
movement is banked instead and paid out from `MouseMapper::tick`, which runs once per frame on the
same thread the real events arrive on, immediately before the queue it is handed gets drained.

Guards, since this synthesises input rather than only filtering it: the banked motion is capped,
the per-frame step is capped so a hitch cannot release it all at once, a re-entrancy flag keeps
the synthetic events out of the filter that produced them, and the drain stops entirely once the
pointer is released so it can never push movement into an open menu.

### A note on the zoom curve

The magnification is interpolated **geometrically** (`factor ^ eased`) rather than linearly.
Perceived zoom is logarithmic — 1x → 2x feels like the same amount of movement as 2x → 4x — so a
linear ramp crawls through the barely-zoomed range and then lurches at the end. The endpoints are
still exactly 1x and the configured magnification.

## Building

Needs [xmake](https://xmake.io) and the **clang-cl** toolchain (LeviLamina does not build with
MSVC). Install LLVM, or add *C++ Clang Compiler for Windows* in the Visual Studio Installer.

```bash
xmake f -p windows -a x64 -m release -y
xmake
```

The packed mod ends up in `bin/`.

## License

See [LICENSE](LICENSE).
