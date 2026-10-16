# StringForge — JUCE multi-effects starter scaffold

This repository now includes the first integrated DSP modules for **low-latency pitch/harmony** and a **preallocated stereo looper**, along with JUCE unit tests and CMake test-target wiring. It remains a development scaffold, not a completed or hardware-calibrated seven-pedal emulator.

## What is implemented

- APVTS host-automatable parameter layout with stable IDs and basic state persistence.
- Existing wah-inspired filter, seven-band peaking EQ, basic feedback delay and JUCE reverb baseline.
- `Source/DSP/PitchHarmonyEngine.*`: a preallocated dual-read-head delay-line shifter, major/natural-minor third/fifth mapper, octave-down mapping, and adjustable pitch-glide smoothing (used by Super Bend).
- `Source/DSP/StereoLooper.*`: a preallocated stereo loop recorder/player, quantized record start/finish using an internal BPM sample clock, reverse playback, crossfade seam handling, up to eight configured audio layers (the starter plug-in allocates four), and logical undo/redo.
- `Tests/DspModuleTests.cpp`: JUCE unit tests for harmony mapping, shifted-tone output, looper states/layers, crossfade seam, and repeated-buffer stability.
- `DSP_MODULES.md`: API contract, limitations, memory estimates, exact CMake commands, and DAW acceptance tests.

## Important limitations

- The pitch shifter is a low-latency performance prototype, not a transparent professional pitch shifter. The window value is not a guarantee of total DAW latency; extreme shifts may be grainy. A high-quality option may require a longer-window WSOLA/phase-vocoder or other specialized algorithm.
- Key-aware harmony needs a pitch estimate. The plug-in adapter can use MIDI note-on input; audio-only electric violin requires a separate monophonic pitch tracker, whose latency/confidence should be shown to the user.
- Looper overdubs are undoable layers, not independent multitrack loops with individual playheads.
- BPM sync follows a local sample clock in this starter. Locking to a host's musical position requires `AudioPlayHead` PPQ/transport integration.
- The existing wah/EQ coefficient code still needs a no-allocation audit before the entire processor can be classified as strict real-time safe.
- Circuit/optical sensor models and the spring/Flerb algorithms are approximations/placeholders. Do not imply circuit-accurate modeling without reference measurements.
- The JUCE checkout is not included. Place JUCE under `./JUCE` before CMake configuration.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTRINGFORGE_BUILD_DSP_TESTS=ON
cmake --build build --config Release --target StringForge_VST3 StringForgeDspTests
ctest --test-dir build -C Release --output-on-failure
```

On Windows, Visual Studio 2022 users should use a Developer PowerShell and configure with `-G "Visual Studio 17 2022" -A x64`. AU is built only on macOS. See `DSP_MODULES.md` for full platform and DAW steps.
