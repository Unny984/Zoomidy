# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.3.1]: https://github.com/Unny984/Zoomidy/releases/tag/v0.3.1
