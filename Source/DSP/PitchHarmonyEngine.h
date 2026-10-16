#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace stringforge::dsp
{
/**
    Maps a tracked MIDI note into a diatonic harmony note. This is deliberately
    separate from audio pitch tracking: note tracking has its own latency and
    confidence trade-offs and should feed a stable MIDI-note estimate here.
*/
enum class HarmonyScale : int { Major = 0, NaturalMinor = 1 };
enum class HarmonyVoice : int { ThirdAbove = 0, FifthAbove = 1, OctaveDown = 2 };

struct HarmonyMapper final
{
    static int targetMidiNote (int inputMidiNote,
                               int rootPitchClass,
                               HarmonyScale scale,
                               HarmonyVoice voice) noexcept;

    static int semitoneOffset (int inputMidiNote,
                               int rootPitchClass,
                               HarmonyScale scale,
                               HarmonyVoice voice) noexcept
    {
        return targetMidiNote (inputMidiNote, rootPitchClass, scale, voice) - inputMidiNote;
    }
};

/**
    A compact dual-read-head delay-line pitch shifter.

    * Storage is allocated only in prepare().
    * Shared read-head phase keeps stereo movement coherent.
    * The delay excursion is bounded by windowMilliseconds.
    * The crossfaded delay-head reset is a low-latency compromise, not a
      transparent studio pitch-shifter; it can sound grainy on large shifts.

    Around +/- 12 semitones this is a useful low-latency starting point. At
    extreme ratios (up to +/- 36 semitones), quality degrades substantially.
    The requested sub-10 ms figure is the nominal delay window, not a guarantee
    of measured end-to-end plug-in latency or transparent pitch tracking.
*/
class LowLatencyPitchShifter final
{
public:
    void prepare (double sampleRateHz,
                  int maxBlockSize,
                  int numChannels,
                  float windowMilliseconds = 8.0f,
                  float parameterSmoothingMilliseconds = 8.0f);

    void reset() noexcept;

    /** Changes the pitch glide time without allocating; call on the audio thread only. */
    void setPitchSmoothingMilliseconds (float milliseconds) noexcept;

    /** In-place pitch shifting. semitones is clamped to [-36, +36]. */
    void processBlock (juce::AudioBuffer<float>& buffer,
                       float semitones,
                       float wet = 1.0f) noexcept;

    int getWindowSamples() const noexcept { return windowSamples_; }
    double getNominalWindowMilliseconds() const noexcept
    {
        return sampleRateHz_ > 0.0 ? 1000.0 * (double) windowSamples_ / sampleRateHz_ : 0.0;
    }

private:
    float readInterpolated (int channel, float delaySamples) const noexcept;
    int wrapIndex (int index) const noexcept;

    double sampleRateHz_ = 44100.0;
    int maxChannels_ = 2;
    int windowSamples_ = 352;
    int ringSize_ = 360;
    int writeIndex_ = 0;
    int samplesWritten_ = 0;
    float phase_ = 0.0f;
    bool prepared_ = false;

    juce::AudioBuffer<float> delayLine_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> semitonesSmoothed_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetSmoothed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowLatencyPitchShifter)
};

/**
    Convenience wrapper for a single harmony voice. The caller supplies the
    current tracked monophonic MIDI note (from MIDI or an audio pitch tracker).
    No pitch detector is hidden in this class, which keeps processing cost and
    detector latency explicit.
*/
class PitchHarmonyEngine final
{
public:
    void prepare (double sampleRateHz, int maxBlockSize, int numChannels,
                  float windowMilliseconds = 8.0f);
    void reset() noexcept { shifter_.reset(); }
    void setPitchSmoothingMilliseconds (float milliseconds) noexcept
    { shifter_.setPitchSmoothingMilliseconds (milliseconds); }

    void processPitchShift (juce::AudioBuffer<float>& buffer,
                            float semitones,
                            float wet) noexcept;

    void processHarmony (juce::AudioBuffer<float>& buffer,
                         int trackedInputMidiNote,
                         int rootPitchClass,
                         HarmonyScale scale,
                         HarmonyVoice voice,
                         float wet) noexcept;

    static int harmonySemitones (int trackedInputMidiNote,
                                 int rootPitchClass,
                                 HarmonyScale scale,
                                 HarmonyVoice voice) noexcept
    {
        return HarmonyMapper::semitoneOffset (trackedInputMidiNote, rootPitchClass, scale, voice);
    }

    int getWindowSamples() const noexcept { return shifter_.getWindowSamples(); }

private:
    LowLatencyPitchShifter shifter_;
};
} // namespace stringforge::dsp
