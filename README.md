# GHOST

GHOST is an experimental dynamic audio effect plugin for FL Studio and other VST3 hosts.

## Concept

GHOST does not behave like a traditional static EQ, compressor, or stereo widener. Its first prototype uses the incoming signal's envelope and transient activity to create subtle, movement-based processing.

The long-term concept is:

> Make sound react.

## Current prototype

Version 0.1.0 includes:

- VST3 plugin target
- JUCE 9.0.2 via CMake FetchContent
- real-time envelope detection
- transient-sensitive stereo widening
- dynamic attack/tail response
- wet/dry mix
- parameter state saving

## Build

Requirements:

- CMake 3.22+
- C++20 compiler
- Visual Studio 2022/2026 or a compatible toolchain

Configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The generated VST3 will be copied after the build when supported by the current JUCE/CMake configuration.

## Roadmap

- real ghost-response visualization
- smoother parameter modulation
- stronger transient/body/tail separation
- frequency-dependent ghosting
- character controls
- preset system
- CPU-safe analyzer
- automated plugin QA
