# GHOST

GHOST is an experimental dynamic audio effect for FL Studio and other VST3 hosts.

> Make sound react.

## Release: 1.0.0

GHOST 1.0 is the release build of the transient-aware spatial afterimage engine. It detects transient, body and tail behaviour, finds the dominant spectral region of an event, shapes local contrast, and creates a short asymmetric two-tap reflection. The effect is designed to react to the performance instead of behaving like a static EQ or conventional stereo widener.

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

The central display follows the live detector and shows transient, body, tail and overall ghost motion. The interface shows the exact release version in the main title, and the GitHub Actions artifact is versioned as GHOST-VST3-v1.0.0.

## Build

Requirements: CMake 3.22+, C++20, and a supported compiler.

```powershell
cmake -B build
cmake --build build --config Release --parallel
```

JUCE is fetched automatically by CMake.

## Release notes

- Stable VST3 release for Windows via the included GitHub Actions build.
- GHOST at 0% is a clean dry path; MIX provides predictable dry/wet blending.
- Spatial halo uses two short reflections with cross-fed stereo placement.
- DSP allocations for the delay lines are performed during preparation, not during audio processing.
- The plugin remains intentionally focused: no preset browser, no MIDI dependency, and no added conventional compressor/EQ module.
