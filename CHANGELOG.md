# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] - 2026-08-24

### Added

- Support for LeviLamina **26.10.x** / Minecraft Bedrock client **1.26.10.4**, alongside the
  existing 26.20.x line. Each line is its own build, since the hooks resolve against the exact
  game binary. Install it with `lip install github.com/Unny984/Zoomidy#client_26_10`; the plain
  package name still installs the 26.20.x build.

  The 26.10 build is compiled with MSVC rather than clang. LeviLamina derives its event ids from
  a type name the compiler produces, and the event classes sit in an inline namespace that the
  two compilers spell differently, so a mod built with the wrong one loads, reports nothing, and
  then ignores every key press and command. 26.20 is built with clang and 26.10 with MSVC.

  Config files are also read directly rather than through `ll::config::loadConfig`, whose 26.10
  copy wraps the parsed document in an array and then throws on it, leaving the mod on its
  default settings no matter what the file said.

  Everything the mod does is the same on both. The settings screen looks different, because 26.10
  predates the UI API the 26.20 screen is built on: see below. The `/zoomidy` sub-commands and the
  config file are identical on both.

### Changed

- The settings screen saves as you go. There is no Apply button on either version, and nothing to
  scroll to the bottom for — every control takes effect and is written to disk the moment you
  change it.

  On **26.10** that meant replacing the page of controls with a menu of buttons, which is the only
  screen that version can report anything from before it closes. Sliders become a value with
  `+`/`-` buttons, dropdowns become a short list to pick from, and toggles flip in place. The zoom
  key is chosen from a list of the usual candidates; anything else is still `/zoomidy key`.

- `/zoomidy debug` now logs every mouse event rather than only the ones that reach the sensitivity
  filter, and records whether the zoom was engaged. A capture that stays empty now means something
  — that the mod is not being handed your mouse at all — instead of looking the same as a filter
  that decided to do nothing.

### Fixed

- On 26.10 the settings form changed nothing at all. Everything was read back under a name and
  applied on submit, and every one of those reads came back empty, so submitting rewrote the
  config with exactly the values it had been opened with. Turning off "hide hand" left the hand
  hidden, and the sensitivity, cinematic and scroll-wheel settings could not be moved off their
  defaults. The screen no longer submits anything, so there is nothing left to read back.

## [0.3.1] - 2026-08-21

### Fixed

- Zooming in during a fast mouse movement no longer leaves the camera flying for
  a moment afterwards. Banked motion is now scaled by the zoom at the frame it is
  paid out, rather than by whatever the zoom was when the mouse moved.

## [0.3.0] - 2026-08-21

### Added

- The camera now coasts after a flick instead of stopping dead. Motion is banked
  while the cinematic option is on and drained a frame at a time from a per-frame
  hook, which is the only place it can be driven once the mouse stops reporting.
- `/zoomidy debug <true|false>` logs the next 60 mouse events while zoomed, then
  disarms itself.

## [0.2.4] - 2026-08-21

### Fixed

- Circular mouse movement came out square. A per-axis clamp was bending the
  direction of the turn; only the direction-preserving low-pass remains.

## [0.2.1] - 2026-08-21

### Fixed

- Cinematic smoothing had no visible effect. It released a fixed fraction per
  mouse event, and events arrive 1-5 ms apart, so it decayed within milliseconds.
  It is now driven by elapsed time and behaves the same at any polling rate.
- Fractional settings are carried through the settings form as whole percents,
  so sliders no longer read `1.5000000000000002`.

## [0.2.0] - 2026-08-21

### Fixed

- Sensitivity scaling and the cinematic camera had no effect, because both sat
  behind a check for a mouse action id that a pointer-locked camera never uses.
  Any event carrying a delta is now filtered.

## [0.1.0] - 2026-08-21

### Added

- Hold-to-zoom with an animated transition, six easing curves, and a rebindable key.
- Adjustable magnification, with the scroll wheel changing it live while zoomed.
- Hides the first-person hand while zoomed.
- Sensitivity scaling: relative to the zoom, fixed, or off.
- In-game settings form via `/zoomidy`, plus sub-commands that work on remote servers.

[0.4.0]: https://github.com/Unny984/Zoomidy/releases/tag/v0.4.0
