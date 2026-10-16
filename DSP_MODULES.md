# StringForge DSP modules: pitch/harmony + stereo looper

This document covers the first integrated implementation of `PitchHarmonyEngine` and `StereoLooper`.

## Scope and claims

- `LowLatencyPitchShifter` is a preallocated, dual-read-head modulated-delay pitch shifter. The 8 ms default is the delay excursion/window setting, not a guarantee of total DAW round-trip latency. Its tone is a low-latency performance compromise; large shifts can produce modulation/grain artifacts and it is not a transparent studio shifter.
- `HarmonyMapper` maps a supplied MIDI note estimate to a diatonic third/fifth or an octave below in Major/Natural Minor. For monophonic harmony, the note estimate can come from MIDI input or an audio tracker. The current plug-in adapter consumes MIDI note-on events; a robust audio pitch detector is still needed for audio-only violin input.
- `StereoLooper` preallocates layer buffers, crossfade weights and sample-generation stamps in `prepare()`. No storage allocation, mutex, file I/O or full-buffer clear occurs in its `processBlock()`.
- Overdubs are undoable layers, not independently addressable tracks with independent transport heads. Each overdub layer contains the samples captured at loop positions; repeated passes use the overdub feedback setting.
- BPM quantization uses a sample clock local to this plug-in instance. For host-transport-locked timing, replace that clock with `AudioPlayHead` transport position/PPQ and handle transport discontinuities.

## Pitch/harmony API

```cpp
stringforge::dsp::PitchHarmonyEngine engine;
engine.prepare (sampleRate, maxBlockSize, channels, 8.0f);

// Static pitch shifting (±36 semitones; quality falls off at extreme ratios)
engine.processPitchShift (audioBuffer, semitones, wetMix);

// Intelligent single-voice harmony; trackedInputMidiNote must be supplied by MIDI
// or a monophonic audio-pitch detector with confidence/hysteresis.
engine.processHarmony (audioBuffer, trackedInputMidiNote, rootPitchClass,
                       stringforge::dsp::HarmonyScale::NaturalMinor,
                       stringforge::dsp::HarmonyVoice::ThirdAbove,
                       wetMix);
```

Pitch classes use `C=0, C#=1, ..., B=11`; input and output notes use MIDI note numbers 0–127. Chromatic notes are assigned to their nearest in-key degree (ties choose the lower degree) before selecting a scale third/fifth. The octave-down voice is chromatic. A production version should let the user choose the chromatic-note policy (nearest degree, preserve accidentals, or voice-leading lookup).

The shifter exposes a smoothing-time setter; the processor uses the `Super Bend Rise` parameter (10–2000 ms) for Super Bend and an 8 ms glide for other modes. This smooths changes to the target interval but does not reproduce the original pedal's dedicated bend envelope exactly.

The shifter has a warm-up period equal to its window length. During warm-up it passes dry audio; wet/dry interpolation then blends into the shifted result. The dry and shifted signals are not time-aligned, so partial wet blend can comb-filter. Test this with the actual violin pickup and decide whether to add a dry-path delay or use a higher-latency quality mode.

## Looper command/state behavior

| Command | Behavior |
|---|---|
| Record in Empty/Stopped | Begin a new take; enters `WaitingToRecord` if beat quantization is enabled |
| Record while Recording | Finish the take and enter Play, quantized to the next grid if enabled |
| Play while Stopped | Restart at the loop start (or end in reverse mode) |
| Overdub while Playing | Create a new undoable layer |
| Overdub while Overdubbing | Commit the current layer and return to Play |
| Stop while Playing | Stop loop playback; live input can still pass through |
| Stop while Recording | Finish the take into Stopped |
| Stop while Overdubbing | Commit the current layer and stop |
| Clear | Immediately make the current loop unreachable and return to Empty; physical memory is not scrubbed on the audio thread |
| Undo / Redo | Change active layer count without copying a whole loop buffer |
| Reverse | Toggle playback direction |

Commands are atomic bit flags. If multiple commands arrive between callbacks, they can coalesce; Clear takes priority. This is appropriate for footswitch/UI commands, but a production MIDI router should serialize actions and define precedence for simultaneous messages. Action parameters in the starter adapter are **rising-edge triggered**: send a false-to-true pulse or toggle each command parameter low before triggering it again.

The forward loop seam blends the final crossfade window into the head window and skips already blended head samples on subsequent cycles. Reverse playback uses the mirrored seam. A first take can be as short as 128 samples in this starter; production UI should enforce a musically useful minimum duration.

## Memory budget

Memory for raw audio is approximately:

`maxLoopSeconds × sampleRate × 2 channels × sizeof(float) × maxLayers`

For 30 s, 48 kHz, stereo and 4 layers, audio storage is about 46 MB and generation stamps add about 23 MB (roughly 69 MB total, plus small overhead). At 96 kHz, that example is about 138 MB total. A 256 MiB budget is used for audio plus stamps; the requested duration may be capped to fit. Choose and display the duration/layer budget intentionally; allocate it only during `prepareToPlay`, and handle allocation failure outside the audio callback.

## Real-time constraints and remaining audit

The two new modules allocate only in `prepare()`. However, the pre-existing FX/EQ scaffold currently creates JUCE IIR coefficient objects in its callback when wah/EQ parameters are read. Audit/replace those coefficient updates before treating the *entire* plug-in signal chain as strict no-allocation real-time code. A later production pass should also test reverb parameter changes, denormals, sample-rate reconfiguration, destruction/clear while audio is running, and plug-in unload behavior.

## Build and run tests (CMake)

JUCE must exist at `./JUCE` because the repository's root `CMakeLists.txt` uses `add_subdirectory(JUCE)`. A JUCE checkout is not vendored in this source bundle.

### Windows (Visual Studio 2022, x64)

Run from a Visual Studio Developer PowerShell or terminal with CMake available:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSTRINGFORGE_BUILD_DSP_TESTS=ON
cmake --build build --config Release --target StringForge_VST3 StringForgeDspTests
ctest --test-dir build -C Release --output-on-failure
```

Likely plug-in artifact path:

```text
build\StringForge_artefacts\Release\VST3\StringForge.vst3
```

The exact artifact directory can vary by JUCE version and generator; inspect the CMake build output if it differs. To scan in a DAW that only scans standard locations, copy the entire `StringForge.vst3` bundle folder into:

```text
C:\Program Files\Common Files\VST3\
```

Then rescan/restart the host.

### macOS

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTRINGFORGE_BUILD_DSP_TESTS=ON
cmake --build build --config Release --target StringForge_VST3 StringForge_AU StringForgeDspTests
ctest --test-dir build -C Release --output-on-failure
```

AU is available only on macOS. If using a multi-configuration generator, keep `--config Release`; for a single-configuration generator, set `CMAKE_BUILD_TYPE=Release` during configuration.

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTRINGFORGE_BUILD_DSP_TESTS=ON
cmake --build build --config Release --target StringForge_VST3 StringForgeDspTests
ctest --test-dir build -C Release --output-on-failure
```

Some distributions/package managers may build JUCE with different optional system dependencies; satisfy those JUCE build requirements for your distribution first.

## DAW acceptance checklist

1. Insert StringForge on an audio track and monitor an instrument input at 44.1/48 kHz with 64/128/256-sample blocks.
2. For pitch shifting, test unity, ±7, ±12 and extreme shifts; record transient smearing, pitch accuracy, stereo image, level jumps and CPU. Do not advertise ±36-semitone quality without dedicated listening/measurement.
3. To test MIDI-assisted harmony, route monophonic MIDI note-on messages to the effect while the target audio passes through it. The current adapter uses the latest note-on in a block and is not polyphonic harmony tracking.
4. For looper testing, create a false-to-true edge on Record, record a phrase, trigger Record again to finalize, then test Play, Overdub, Undo, Redo, Reverse, Stop and Clear. The generic parameter UI uses booleans, so return a command parameter to false before triggering it again.
5. Test tempo quantization with different BPM values, changing host block sizes, stop/start, and transport resets. This starter uses an internal sample clock rather than host PPQ.
6. Repeat tests after save/reload and in at least two hosts. Stress rapid commands and long recordings; check for NaN/Inf, clicks at both loop seams, unbounded CPU and excessive memory.

The tests included here are executable JUCE `UnitTest` tests. They should be run on the target toolchain; this source bundle has not been compiled against a local JUCE checkout in its preparation environment.
