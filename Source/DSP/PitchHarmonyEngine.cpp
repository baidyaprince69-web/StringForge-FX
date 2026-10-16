#include "PitchHarmonyEngine.h"

namespace stringforge::dsp
{
namespace
{
constexpr std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };
constexpr float pi = 3.14159265358979323846f;

int positiveMod12 (int value) noexcept
{
    const int r = value % 12;
    return r < 0 ? r + 12 : r;
}
} // namespace

int HarmonyMapper::targetMidiNote (int inputMidiNote,
                                   int rootPitchClass,
                                   HarmonyScale scale,
                                   HarmonyVoice voice) noexcept
{
    inputMidiNote = juce::jlimit (0, 127, inputMidiNote);
    rootPitchClass = positiveMod12 (rootPitchClass);

    if (voice == HarmonyVoice::OctaveDown)
        return juce::jmax (0, inputMidiNote - 12);

    const auto& notes = (scale == HarmonyScale::NaturalMinor) ? minorScale : majorScale;
    const int relative = inputMidiNote - rootPitchClass;
    const int sourcePitchClass = positiveMod12 (relative);
    const int octave = (relative - sourcePitchClass) / 12;

    // Snap chromatic notes to the nearest scale degree; ties choose the lower
    // degree for deterministic, repeatable behavior on chromatic passing notes.
    int bestDegree = 0;
    int bestDistance = 13;
    for (int degree = 0; degree < 7; ++degree)
    {
        const int rawDistance = std::abs (sourcePitchClass - notes[(size_t) degree]);
        const int circularDistance = juce::jmin (rawDistance, 12 - rawDistance);
        if (circularDistance < bestDistance)
        {
            bestDistance = circularDistance;
            bestDegree = degree;
        }
    }

    const int degreeAdvance = (voice == HarmonyVoice::ThirdAbove) ? 2 : 4;
    const int destinationDegree = bestDegree + degreeAdvance;
    const int octaveAdvance = destinationDegree / 7;
    const int scaleIndex = destinationDegree % 7;
    const int destinationMidi = rootPitchClass
                              + (octave + octaveAdvance) * 12
                              + notes[(size_t) scaleIndex];
    return juce::jlimit (0, 127, destinationMidi);
}

void LowLatencyPitchShifter::prepare (double sampleRateHz,
                                      int maxBlockSize,
                                      int numChannels,
                                      float windowMilliseconds,
                                      float parameterSmoothingMilliseconds)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRateHz_ = juce::jmax (8000.0, sampleRateHz);
    maxChannels_ = juce::jlimit (1, 2, numChannels);
    windowSamples_ = juce::jmax (32, (int) std::lround (sampleRateHz_ * juce::jlimit (2.0f, 30.0f, windowMilliseconds) / 1000.0));
    ringSize_ = windowSamples_ + 8;
    delayLine_.setSize (maxChannels_, ringSize_, false, true, true);

    semitonesSmoothed_.reset (sampleRateHz_, juce::jmax (0.001, (double) parameterSmoothingMilliseconds / 1000.0));
    wetSmoothed_.reset (sampleRateHz_, 0.005);
    semitonesSmoothed_.setCurrentAndTargetValue (0.0f);
    wetSmoothed_.setCurrentAndTargetValue (1.0f);
    prepared_ = true;
    reset();
}

void LowLatencyPitchShifter::reset() noexcept
{
    if (delayLine_.getNumSamples() > 0)
        delayLine_.clear();
    writeIndex_ = 0;
    samplesWritten_ = 0;
    phase_ = 0.0f;
    semitonesSmoothed_.setCurrentAndTargetValue (0.0f);
    wetSmoothed_.setCurrentAndTargetValue (1.0f);
}

void LowLatencyPitchShifter::setPitchSmoothingMilliseconds (float milliseconds) noexcept
{
    // SmoothedValue::reset changes the ramp length without allocating. It preserves
    // the current value and current target; subsequent setTargetValue calls use it.
    const float boundedMs = juce::jlimit (1.0f, 2000.0f, milliseconds);
    semitonesSmoothed_.reset (sampleRateHz_, (double) boundedMs / 1000.0);
}

int LowLatencyPitchShifter::wrapIndex (int index) const noexcept
{
    if (index < 0) index += ringSize_;
    if (index >= ringSize_) index -= ringSize_;
    return index;
}

float LowLatencyPitchShifter::readInterpolated (int channel, float delaySamples) const noexcept
{
    // The ring is larger than the maximum delay, so this needs at most one wrap.
    float position = (float) writeIndex_ - delaySamples;
    if (position < 0.0f) position += (float) ringSize_;
    if (position >= (float) ringSize_) position -= (float) ringSize_;

    const int centre = (int) position;
    const float frac = position - (float) centre;
    const float y0 = delayLine_.getSample (channel, wrapIndex (centre - 1));
    const float y1 = delayLine_.getSample (channel, centre);
    const float y2 = delayLine_.getSample (channel, wrapIndex (centre + 1));
    const float y3 = delayLine_.getSample (channel, wrapIndex (centre + 2));

    // Cubic Hermite interpolation; lower high-frequency loss than linear reads.
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void LowLatencyPitchShifter::processBlock (juce::AudioBuffer<float>& buffer,
                                           float semitones,
                                           float wet) noexcept
{
    if (! prepared_ || ringSize_ <= 8 || buffer.getNumSamples() <= 0)
        return;

    semitonesSmoothed_.setTargetValue (juce::jlimit (-36.0f, 36.0f, semitones));
    wetSmoothed_.setTargetValue (juce::jlimit (0.0f, 1.0f, wet));
    const int channels = juce::jmin (maxChannels_, buffer.getNumChannels());
    const int samples = buffer.getNumSamples();
    if (channels <= 0)
        return;

    const float maxDelay = (float) juce::jmax (4, windowSamples_ - 3);
    for (int n = 0; n < samples; ++n)
    {
        const float shiftNow = semitonesSmoothed_.getNextValue();
        const float wetNow = wetSmoothed_.getNextValue();
        const float ratio = std::pow (2.0f, shiftNow / 12.0f);
        const bool activeShift = std::abs (shiftNow) > 0.015f;
        const bool warmedUp = samplesWritten_ >= windowSamples_ + 3;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float input = buffer.getSample (ch, n);
            delayLine_.setSample (ch, writeIndex_, input);

            float shifted = input;
            if (activeShift && warmedUp)
            {
                float p0 = phase_;
                float p1 = p0 + 0.5f;
                if (p1 >= 1.0f) p1 -= 1.0f;

                const bool shiftingUp = ratio > 1.0f;
                const float delay0 = juce::jlimit (2.0f, maxDelay, (shiftingUp ? (1.0f - p0) : p0) * maxDelay);
                const float delay1 = juce::jlimit (2.0f, maxDelay, (shiftingUp ? (1.0f - p1) : p1) * maxDelay);
                const float weight0 = 0.5f - 0.5f * std::cos (2.0f * pi * p0);
                const float weight1 = 0.5f - 0.5f * std::cos (2.0f * pi * p1);
                shifted = readInterpolated (ch, delay0) * weight0
                        + readInterpolated (ch, delay1) * weight1;
            }

            // The pitch-shifted and dry paths are not time-aligned by design;
            // this is a performance effect, so partial wet settings can comb-filter.
            buffer.setSample (ch, n, input + (shifted - input) * wetNow);
        }

        if (activeShift)
        {
            phase_ += std::abs (1.0f - ratio) / (float) windowSamples_;
            phase_ -= std::floor (phase_);
        }
        if (samplesWritten_ < windowSamples_ + 3)
            ++samplesWritten_;
        ++writeIndex_;
        if (writeIndex_ >= ringSize_) writeIndex_ = 0;
    }
}

void PitchHarmonyEngine::prepare (double sampleRateHz, int maxBlockSize, int numChannels,
                                  float windowMilliseconds)
{
    shifter_.prepare (sampleRateHz, maxBlockSize, numChannels, windowMilliseconds);
}

void PitchHarmonyEngine::processPitchShift (juce::AudioBuffer<float>& buffer,
                                            float semitones,
                                            float wet) noexcept
{
    shifter_.processBlock (buffer, semitones, wet);
}

void PitchHarmonyEngine::processHarmony (juce::AudioBuffer<float>& buffer,
                                         int trackedInputMidiNote,
                                         int rootPitchClass,
                                         HarmonyScale scale,
                                         HarmonyVoice voice,
                                         float wet) noexcept
{
    const auto interval = (float) HarmonyMapper::semitoneOffset (trackedInputMidiNote,
                                                                  rootPitchClass,
                                                                  scale,
                                                                  voice);
    shifter_.processBlock (buffer, interval, wet);
}
} // namespace stringforge::dsp
