# StringForge — production architecture specification

## 1. Product boundary and claims

StringForge is a single serial multi-effects processor for electric violin, electric cello and acoustic-electric strings. The seven sections are *inspired-by functional modules*, not a claim of bit-for-bit or circuit-identical replication. Authentic modeling needs physical reference pedals, test fixtures, repeatable DI/re-amp captures, component/circuit information where available, level-matched null/listening tests, and permission/legal review for product names and trade dress. Use neutral names in shipping builds unless trademark clearance has been obtained.

**Do not promise absolute zero latency.** EQ, volume, conventional delay, and standard reverbs can run without look-ahead. A stable pitch shifter/harmonizer often adds algorithmic latency; FFT windows or buffering add delay by design. Quantized loop start can wait for the next grid boundary. Publish measured latency for every mode and expose it through JUCE's latency reporting when the active route changes. If the host cannot accept mode-dependent latency changes reliably, choose a fixed maximum latency for every mode and delay the dry path to match. Provide a true low-latency performance mode that disables latency-heavy features rather than concealing latency.

## 2. Processing graph

```text
Input bus (mono/stereo)
  -> input trim + DC/denormal guard + input meter
  -> FX17-inspired expression volume / resonant wah
  -> GE-7-style seven-band EQ + EQ output trim
  -> tuner analysis tap (pre/post selector; no audio-path analysis dependency)
       -> optional tuner mute gate (smoothed)
  -> PS-6-inspired pitch / intelligent harmony / detune / bend engine
  -> Flashback-inspired delay / modulation / dynamic feedback
  -> JamMan-inspired loop playback and record/overdub mixer
  -> Holy-Grail-inspired reverb / spring / hall / Flerb
  -> global wet/dry (with documented placement)
  -> optional output safety stage / true-peak meter
  -> output trim -> output bus
```

The serial order above is the default user-facing order. Internally, the tuner is a *parallel analysis tap* so analysis does not force the audible path through a pitch detector. A true serial tuner mute gate is controllable independently. In looper modes, decide and document whether the looper records pre-reverb, post-delay, or selectable taps; the recommended default is pre-reverb/post-delay to prevent the loop from recursively recording ambience unless the user chooses otherwise.

### Gain staging / headroom

- Use `double` sample values for the internal routing graph in the production version. A float-host adapter copies float samples to a preallocated double scratch buffer, processes in double and converts back at the output. This reduces internal numerical clipping but **does not prevent output clipping** and does not improve every algorithm automatically.
- Avoid hidden automatic gain jumps. Meter each module input/output and the internal maximum. Offer a separate, optional output safety stage (oversampled limiter/clipper) with a clearly visible gain-reduction indicator.
- Use per-stage soft bypass crossfades, typically 5–20 ms, so switches do not click. The tuner mute uses its own short ramp. Preserve reverb/delay tails on bypass where expected; a “kill tails” action should be distinct.
- Smooth continuous parameters using linear smoothing for gains and perceptually appropriate/log smoothing for frequency/time. For filter coefficient changes, smooth the controlling frequency/Q or interpolate between stable coefficient sets; don't slam unstable coefficients at arbitrary sample boundaries.
- Use denormal protection, finite-value checks in debug builds, bounded output guardrails, and no unbounded loops.

## 3. Module architecture

Each module implements a common contract and owns only its own DSP state:

```cpp
struct ProcessContext {
    double sampleRate;
    int maxBlockSize;
    int channels;
    // Host tempo, transport, playhead state, and a non-owning parameter snapshot.
};

class IEffectStage {
public:
    virtual ~IEffectStage() = default;
    virtual void prepare (const ProcessContext&) = 0; // allocations only here
    virtual void reset() noexcept = 0;
    virtual void process (juce::AudioBuffer<double>&, const BlockContext&) noexcept = 0;
    virtual int latencySamples() const noexcept = 0;
    virtual void setBypassed (bool) noexcept = 0;
};
```

In shipping code, avoid virtual-call overhead only if profiling proves it matters. An explicit fixed chain with compiled stage types is also acceptable. Parameters should be copied into a small POD `ParameterSnapshot` once per block using atomic loads; stages should not each call `getRawParameterValue()` inside the sample loop.

### 3.1 FX17-inspired optical wah / volume

- **Mode:** Wah or Volume.
- **Position:** normalized 0–1 expression value; accept host automation and MIDI CC/expression pedal input. Use a perceptual sweep, e.g. log-frequency from roughly 250 Hz to 3 kHz, then tune by reference measurement. Range/contour controls resonance/range; avoid implying these provisional ranges match the hardware without calibration.
- **Optical response:** if the reference unit is available, measure sensor travel versus resistance/control voltage, sweep hysteresis, heel/toe endpoints, and settling response. Fit a monotonic lookup curve plus a one/two-pole smoothing model. Do not label a generic exponential curve “accurate optical sensor emulation.”
- **Volume mode:** use a configurable taper curve (linear/audio/log) and gain smoothing to avoid zipper noise. Decide explicitly whether heel reaches silence or a finite floor.

### 3.2 Seven-band EQ

- Seven peaking filters centered at 100, 200, 400, 800, 1600, 3200 and 6400 Hz, nominal slider range −15…+15 dB, plus ±15 dB output level.
- Match the hardware's overlapping bandwidth/Q by measurement rather than assuming every band uses identical Q. Use RBJ/bilinear-transform biquads or a double-precision equivalent, validate stability at 44.1/48/88.2/96/192 kHz, and ramp coefficient changes.
- Optional “hardware color” should be a calibrated low-level nonlinear/noise model with a bypassable noise floor, not arbitrary hiss or saturation applied by default.

### 3.3 Tuner

- Analyze a preallocated mono-fold-down tap. Use a robust fundamental estimator (YIN/MPM/autocorrelation for monophonic; multi-pitch method for polyphonic mode), confidence gate and hysteresis. Polyphonic tuning is materially harder and must not be treated as a checkbox over a monophonic estimator.
- Calibrate A4 from 435–445 Hz. Convert detected frequency to cents and note using the selected calibration; use temporal smoothing for the LED matrix while retaining fast attack.
- UI display updates from a lock-free SPSC analysis queue at 20–60 Hz. Never draw from or lock the audio thread. Tuner mute/pass-through must ramp to avoid clicks.
- Test low C on cello, violin upper register, vibrato, harmonics, double stops, distortion-heavy input, noisy stage signals and low input levels.

### 3.4 Pitch / intelligent harmony

- Modes: intelligent major/minor harmony, chromatic pitch shift ±36 semitones, detune, and Super Bend rise/fall sweep.
- The key + scale interval must map each detected source note to a scale-dependent interval. Chordal/polyphonic harmony requires note tracking or a high-quality pitch-shift architecture; it is not the same problem as monophonic note detection.
- Candidate implementations: transient-preserving time-domain methods for lower latency or phase-vocoder methods for wider range/polyphony. Compare artifacts on bowed-string transients, sustained notes, slides, vibrato and double stops. Add anti-alias filtering before/after rate-change stages and avoid cumulative high-frequency loss.
- Maintain `reportedLatencySamples`; compensate the dry path so the Balance knob does not comb-filter merely because the wet path is delayed. Document that zero algorithmic latency is not realistic for all settings.

### 3.5 Delay / modulation / dynamic engine

- Types: clean digital, short analog-style slap, tape-style bandwidth/saturation/flutter, dynamic ducking delay, and modulated delay. Share a preallocated delay core but use separate feedback-loop coloration blocks.
- Time: 1–2000 ms (or host-tempo subdivisions). Feedback clamped below unity unless an explicitly designed freeze mode is used. Add DC blocking/high-pass inside feedback loops where necessary.
- Subdivision choices: quarter, eighth, dotted eighth and triplet. Use host PPQ/tempo if valid; fall back to stored BPM and clearly show which source is active.
- MASH/expression should map to a documented set of macro destinations (e.g. feedback, modulation depth, filter cutoff). Don't let one macro silently change unrelated parameters without a UI mapping display.
- Use anti-alias filtering or oversampling for nonlinear tape/analog feedback coloration and modulation paths that produce significant high-frequency sidebands.

### 3.6 Looper

- Bounded preallocated stereo loop storage sized in `prepareToPlay` from a user-configured maximum duration and memory budget. Example: 10 minutes stereo float is about 230 MB at 48 kHz; double storage roughly doubles that. Provide a storage budget and graceful refusal before record starts if memory is not available.
- State machine: Empty → Recording → Playing → Overdubbing → Playing; Stopped is a transport state. Edge-trigger Stop/Clear/Undo/Redo and guard illegal state transitions. No buffer growth, file I/O, mutex locks or clearing millions of samples in one unbounded real-time operation inside a single callback.
- A practical single-loop overdub model can support multiple *layers* in memory; independent multi-track loops need separate playback heads, synchronization state and gain/pan/mute per track. Label the feature “layers” unless true independently addressable tracks are implemented.
- Undo/redo options: preallocated snapshots (memory intensive), delta buffers, or bounded incremental capture with a worker thread. Make guarantees explicit for maximum duration/layers.
- Reverse can run a read head backwards over an already recorded region; transition at loop boundaries with safe fades. Quantize loop start by transport grid logic; quantization inherently may delay the action until the next boundary.
- Define whether clear is immediate, confirmation-gated, or MIDI-triggerable. A held MIDI switch must not repeatedly clear the loop.

### 3.7 Reverb / ambience

- Hall baseline can use a proven algorithm. Spring requires a dedicated resonant tank model or validated convolution/algorithmic hybrid. Flerb is a reverb path combined with modulated delay/flanging; tune rate, depth and feedback carefully to avoid runaway feedback.
- Keep tail time, damping, pre-delay and modulation controls available internally even if the first UI exposes only Algorithm and Amount. They support presets and future tuning.
- On bypass, allow tails to ring out by default. Include an explicit tail-kill command for performance.

## 4. Parameter tree / host automation

The `ParameterIDs.h` file defines stable IDs. The starter source adds them to `AudioProcessorValueTreeState` (APVTS); don't rename IDs once sessions are distributed. If a parameter is deprecated, preserve an old-ID migration path during state restore.

| Group | Parameters | Range / behavior |
|---|---|---|
| Global | Input, Output, Wet/Dry, Global Bypass | Input −24…+24 dB; output −24…+12 dB; blend 0…100%; bypass crossfade |
| FX17 | Bypass, Mode, Contour, Position | Wah/Volume; 0…1 contour & expression |
| GE-7 | Bypass, 7 bands, Level | Each band ±15 dB, level ±15 dB |
| Tuner | Bypass, Mode, Calibration, Mute | Chromatic/Polyphonic; A4 435…445 Hz |
| PS-6 | Bypass, Mode, Key, Scale, Interval, Balance, Rise, Bend | interval ±36 semitones; balance 0…1; rise 10 ms…2 s |
| Delay | Bypass, Type, Time, Feedback, Level, Subdivision, MASH | 1…2000 ms; feedback 0…0.95; level 0…1 |
| Looper | Bypass, Level, Record, Play, Overdub, Stop, Clear, Reverse, Tempo Sync, Quantize, BPM, Layers | BPM 30…300; layers 1…4 in this schema; command actions edge-triggered |
| Reverb | Bypass, Algorithm, Amount | Spring/Hall/Flerb; amount 0…1 |

Production implementation should add the following if not exposed already: parameter smoothing mode, tuner analysis tap, input/output meters, pre/post gain, loop maximum duration, loop overdub feedback, loop clear confirmation, undo/redo, stop behavior (stop immediately / end of bar), independent track controls if true multitrack is implemented, MIDI CC mapping channel, and a global “low-latency mode.”

## 5. MIDI CC design

All continuous controls are host-automatable. For foot controllers, provide a separate in-plugin MIDI Learn page:

- Learn target → move physical control/send CC → store CC number + channel + min/max + invert + pickup mode.
- Support momentary and toggle switches, rising-edge actions for Record/Stop/Clear/Undo/Redo, expression CC 11 and conventional CC 1 where useful, with reassignable defaults.
- Detect mapping conflicts and offer “replace,” “keep both,” or “cancel.” Do not silently map one CC to a destructive clear action.
- Save mapping state in the plug-in state, while leaving normal parameter automation IDs stable. Expose a mapping table with clear/remove controls.
- Example default mapping (fully reassignable): CC11 Wah/Volume Position; CC1 Wah Contour or user-selected macro; CC20–27 EQ bands/level; CC64 Sustain-like user-assigned bypass only if the user chooses; CC80 Record; CC81 Play; CC82 Overdub; CC83 Stop; CC84 Undo; CC85 Reverse; CC86 Clear (hold-to-confirm or explicit arming recommended); CC91 Reverb Amount; CC92 Delay Level. Avoid destructive defaults without clear on-screen arming.
- If the host exposes only MIDI-to-parameter mapping, document that behavior and test across major DAWs; embedded MIDI learn is needed for consistent per-instance routing.

## 6. GUI / UX spec

### Layout

- Resizable dark brushed-metal rack face, but keep controls legible rather than photorealism at the expense of workflow. Each of seven modules is a vertical strip in signal-chain order. On a 1440 px wide UI, allow a 7-column overview; on smaller windows switch to a horizontal module selector and one active module panel.
- Top bar: product name, preset dropdown, A/B slot, previous/next preset, save, undo/redo parameter edits, input/output meters, global input/output gain, global bypass, latency indicator and CPU indicator.
- Each module header: module number, short module name, status LED and soft-latch bypass. Header click focuses module; it must not unexpectedly toggle bypass.
- FX17 panel: Wah/Volume segmented switch, large Sweep/Position control with expression indicator, Contour knob, assign-to-MIDI icon.
- EQ panel: 7 vertical faders with printed frequencies and ±15 dB scale, reset-flat button, Level knob, optional response graph.
- Tuner panel: large note/octave display, cents needle/LED matrix, calibration, Chromatic/Poly mode, Mute/Thru switch. Show “LOW CONFIDENCE” rather than jumping to wrong notes.
- Pitch panel: mode selector, key/scale, interval, balance, rise time, bend control. Show live pitch/latency status; avoid displaying falsely precise detected notes if confidence is weak.
- Delay panel: delay type, time or synced subdivision, feedback ring, FX level, MASH macro, tempo source display.
- Looper panel: conspicuous timeline/waveform, REC/PLAY/OVERDUB/STOP states, undo/redo, reverse, tempo/quantize, layer indicators. CLEAR must be visually distinct and guarded. Display memory/duration budget.
- Reverb panel: Spring/Hall/Flerb selector and Amount knob; optional advanced view for damping, pre-delay, modulation and tail.
- Bottom strip: MIDI Learn, settings (oversampling / low-latency mode / headroom), bypass-all, help, software version.

### Visual language / accessibility

- Near-black graphite background, warm amber for active signal, teal/ice for analysis, red only for clipping/armed destructive actions. Avoid red/green-only status distinction; include icons/text labels and accessible contrast.
- Soft-latch controls switch visual state immediately but audio transitions are ramped. Provide focus outlines, keyboard access, screen-reader names, scaling from 100–200%, and reduced-motion preference.
- Preset switch should warn only about loop memory/state if meaningful; don't block parameter changes with unnecessary dialogs.

## 7. Factory preset intentions

Create these after all modules are validated; these are design targets, not calibrated settings yet:

1. **Padrón Live Setup** — clean high-pass/low-mid cleanup, conservative delay, moderate hall, expression on volume/wah; maintain transient definition and low feedback.
2. **Ethereal Ambient Soundscape** — long hall/Flerb, tempo delay, higher wet amount, gentle detune/harmony; preserve a dry transient path.
3. **Acoustic Warmth Enhancer** — subtle broad EQ, controlled high-frequency reduction, low saturation, short hall; avoid masking bow articulation.
4. **High-Energy Fusion** — resonant wah, focused mid presence, harmony/pitch effect, tempo delay and restrained ambience; keep output headroom.

## 8. Latency, CPU and memory budgets

- Measure round-trip plugin latency at 44.1, 48, 88.2 and 96 kHz, block sizes 32/64/128/256/512, mono/stereo, every pitch mode, and looper quantize mode. Record latency in samples and milliseconds.
- Add 0/1/2/4x oversampling choices only to the nonlinear modules that need it. Upsampling does not equal zero latency; report the effective latency.
- Target stable real-time operation on a modest CPU at a 64/128-sample DAW block, with performance quality tiers for polyphonic pitch processing. Do not run offline-quality FFT/IR construction or preset I/O on the audio thread.
- Reserve all ring buffers/analysis scratch buffers in `prepareToPlay`. If sample rate or max block size changes, rebuild off the audio callback where host lifecycle permits.
- Stress-test two-hour sessions, automation at audio rate, rapid bypass, rapid program change, host stop/start, sample-rate switching, MIDI floods, invalid/out-of-range parameter state and memory pressure.

## 9. Validation checklist

- Plug-inval / host scanning: VST3 on Windows/macOS; AU on macOS; state save/restore and automation recall in at least three hosts.
- Audio: no NaN/Inf, no unexplained clicks on continuous parameters, bypass, transport start/stop, and sample-rate change. Run null tests and level-matched A/B against the hardware if available.
- Tuner: confidence and cents error by note/register, vibrato, slides, harmonics, double stops and distortion.
- Pitch: cents accuracy, transient smear, stereo coherence, aliasing, latency and CPU across modes.
- Looper: all state transitions, exact boundary behavior, reverse, undo/redo limits, loop end-click testing, MIDI switch bounce, clear protection and long-duration stability.
- Reverb/delay: stability at high feedback, silence decay, tail continuity on bypass, denormal tests and stereo image.
- GUI: HiDPI, resizing, keyboard and screen-reader basics, high contrast, parameter context menu, automation lane naming and MIDI Learn conflicts.

## 10. Licensing / product-name caution

The pedal names and logos belong to their respective owners. Treat them as internal feature-reference language only until legal review clears shipping UI and marketing. Do not copy artwork, enclosure design, proprietary firmware behavior or branding. Use original UI art and clear “inspired by” descriptions where legally appropriate.
