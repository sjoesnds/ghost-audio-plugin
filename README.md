# GHOST

GHOST is an experimental dynamic audio effect for FL Studio and other VST3 hosts.

> Make sound react.

## Current prototype: 0.3.0

GHOST now tracks three continuously changing properties of the incoming audio and carries those detections through a smoothed temporal Ghost state:

- **Transient** — fast attack activity.
- **Body** — sustained program energy.
- **Tail** — decay/release behavior.
- **Ghost state** — a short-lived memory of recent audio activity, smoothing the transition between events.

Those states now drive a perceptual contrast stage: GHOST identifies the dominant spectral region of the current event, reinforces it, and temporarily reduces nearby masking energy instead of relying on a static EQ move.

### Controls

- **GHOST** — overall intensity.
- **ATTACK** — transient emphasis.
- **BODY** — sustained movement.
- **TAIL** — decay influence.
- **WIDTH** — transient expansion and tail contraction.
- **AIR** — dynamic high-frequency movement.
- **SMOOTH** — response shaping.
- **MIX** — dry/wet amount.

The central display follows the live detector and shows transient, body, tail and overall ghost motion.

## Build

Requirements: CMake 3.22+, C++20, and a supported compiler.

```powershell
cmake -B build
cmake --build build --config Release --parallel
```

JUCE is fetched automatically by CMake.

## Direction

GHOST should stay a distinctive audio effect, not become a bundle of conventional processors. Planned work includes better frequency-dependent behavior, character modes, polished visual feedback, presets, and automated audio/plugin QA.
