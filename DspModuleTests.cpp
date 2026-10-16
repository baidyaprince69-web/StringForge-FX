#include <JuceHeader.h>
#include "../Source/DSP/PitchHarmonyEngine.h"
#include "../Source/DSP/StereoLooper.h"
#include <cmath>

namespace
{
class PitchHarmonyUnitTests final : public juce::UnitTest
{
public:
    PitchHarmonyUnitTests() : UnitTest ("StringForge pitch/harmony DSP", "StringForge DSP") {}

    void runTest() override
    {
        beginTest ("Diatonic interval mapping");
        expect (stringforge::dsp::HarmonyMapper::targetMidiNote (60, 0, stringforge::dsp::HarmonyScale::Major,
                    stringforge::dsp::HarmonyVoice::ThirdAbove) == 64, "C major: C to E (third)");
        expect (stringforge::dsp::HarmonyMapper::targetMidiNote (60, 0, stringforge::dsp::HarmonyScale::Major,
                    stringforge::dsp::HarmonyVoice::FifthAbove) == 67, "C major: C to G (fifth)");
        expect (stringforge::dsp::HarmonyMapper::targetMidiNote (69, 9, stringforge::dsp::HarmonyScale::NaturalMinor,
                    stringforge::dsp::HarmonyVoice::ThirdAbove) == 72, "A minor: A to C (minor third)");
        expect (stringforge::dsp::HarmonyMapper::targetMidiNote (69, 9, stringforge::dsp::HarmonyScale::NaturalMinor,
                    stringforge::dsp::HarmonyVoice::FifthAbove) == 76, "A minor: A to E (fifth)");
        expect (stringforge::dsp::HarmonyMapper::targetMidiNote (69, 9, stringforge::dsp::HarmonyScale::NaturalMinor,
                    stringforge::dsp::HarmonyVoice::OctaveDown) == 57, "Octave-down is chromatic");

        beginTest ("Pitch shifter produces finite, shifted output");
        constexpr double sr = 48000.0;
        constexpr int count = 24000;
        juce::AudioBuffer<float> tone (1, count);
        for (int i = 0; i < count; ++i)
            tone.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / sr));

        stringforge::dsp::LowLatencyPitchShifter shifter;
        shifter.prepare (sr, 512, 1, 8.0f, 3.0f);
        shifter.processBlock (tone, 12.0f, 1.0f);

        bool finite = true;
        for (int i = 0; i < count; ++i)
            finite = finite && std::isfinite (tone.getSample (0, i));
        expect (finite, "Pitch-shift output should not contain NaN/Inf");

        // Estimate pitch from positive-going zero crossings after the startup window.
        const int first = 5000;
        const int last = 23000;
        int crossings = 0;
        for (int i = first + 1; i < last; ++i)
            if (tone.getSample (0, i - 1) <= 0.0f && tone.getSample (0, i) > 0.0f)
                ++crossings;
        const double estimatedHz = (double) crossings * sr / (double) (last - first);
        expect (estimatedHz > 700.0 && estimatedHz < 1060.0,
                juce::String ("An octave-up test tone should be in the neighborhood of 880 Hz; actual=") + juce::String (estimatedHz, 1));
    }
};

class StereoLooperUnitTests final : public juce::UnitTest
{
public:
    StereoLooperUnitTests() : UnitTest ("StringForge stereo looper DSP", "StringForge DSP") {}

    void runTest() override
    {
        beginTest ("State machine and layered overdub/undo/redo");
        stringforge::dsp::StereoLooper looper;
        looper.prepare (48000.0, 512, 2, 0.2, 3, 5.0f);
        looper.setQuantizeEnabled (false);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Empty, "Starts empty");

        constexpr int recordedSamples = 2400;
        juce::AudioBuffer<float> capture (2, recordedSamples);
        for (int i = 0; i < recordedSamples; ++i)
        {
            const float l = i < recordedSamples / 2 ? 0.8f : -0.8f;
            capture.setSample (0, i, l);
            capture.setSample (1, i, -0.5f * l);
        }
        looper.requestRecord();
        looper.processBlock (capture, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Recording, "Record command enters Recording");

        looper.requestRecord();
        juce::AudioBuffer<float> oneSample (2, 1);
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Playing, "Second Record finalizes into Play");
        expect (looper.getLoopLengthSamples() == recordedSamples, "Recorded loop length stays sample accurate");
        expect (looper.getActiveLayerCount() == 1, "Initial take is layer one");

        juce::AudioBuffer<float> overdub (2, 512);
        overdub.clear();
        for (int i = 0; i < overdub.getNumSamples(); ++i)
        {
            overdub.setSample (0, i, 0.1f);
            overdub.setSample (1, i, -0.1f);
        }
        looper.requestOverdub();
        looper.processBlock (overdub, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Overdubbing, "Overdub command enters Overdubbing");
        looper.requestOverdub();
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Playing, "Second Overdub commits layer");
        expect (looper.getActiveLayerCount() == 2, "Overdub becomes a separately undoable layer");

        looper.requestUndo();
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getActiveLayerCount() == 1, "Undo removes latest layer logically");
        looper.requestRedo();
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getActiveLayerCount() == 2, "Redo restores latest layer logically");
        looper.requestStop();
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Stopped, "Stop freezes playback");
        looper.requestClear();
        oneSample.clear();
        looper.processBlock (oneSample, 0.8f, 0.95f, true);
        expect (looper.getState() == stringforge::dsp::StereoLooper::State::Empty, "Clear resets logical state");
        expect (looper.getLoopLengthSamples() == 0 && looper.getActiveLayerCount() == 0, "Clear makes old buffer data unreachable");

        beginTest ("Loop splice crossfade suppresses the wrap discontinuity");
        stringforge::dsp::StereoLooper fadeLooper;
        fadeLooper.prepare (48000.0, 512, 2, 0.2, 2, 5.0f);
        fadeLooper.setQuantizeEnabled (false);
        juce::AudioBuffer<float> stepLoop (2, recordedSamples);
        for (int i = 0; i < recordedSamples; ++i)
        {
            const float v = i < recordedSamples / 2 ? 0.8f : -0.8f;
            stepLoop.setSample (0, i, v);
            stepLoop.setSample (1, i, v * 0.25f);
        }
        fadeLooper.requestRecord();
        fadeLooper.processBlock (stepLoop, 1.0f, 0.95f, true);
        fadeLooper.requestRecord();
        oneSample.clear();
        fadeLooper.processBlock (oneSample, 1.0f, 0.95f, true); // consumes playhead sample 0

        juce::AudioBuffer<float> playback (2, recordedSamples);
        playback.clear();
        fadeLooper.processBlock (playback, 1.0f, 0.95f, false);
        const int spliceA = recordedSamples - 2;
        const int spliceB = recordedSamples - 1;
        const float seamJump = std::abs (playback.getSample (0, spliceB) - playback.getSample (0, spliceA));
        expect (fadeLooper.getCrossfadeSamples() >= 2, "Crossfade has a nonzero window");
        expect (seamJump < 0.15f, juce::String ("Loop wrap should be crossfaded; adjacent seam jump=") + juce::String (seamJump, 4));

        beginTest ("Repeated blocks preserve bounded storage and finite audio");
        fadeLooper.requestClear();
        juce::AudioBuffer<float> noise (2, 256);
        bool allFinite = true;
        for (int block = 0; block < 1000; ++block)
        {
            for (int i = 0; i < noise.getNumSamples(); ++i)
            {
                const float v = std::sin (0.01f * (float) (block * noise.getNumSamples() + i));
                noise.setSample (0, i, v * 0.2f);
                noise.setSample (1, i, -v * 0.15f);
            }
            fadeLooper.processBlock (noise, 0.8f, 0.95f, true);
            for (int i = 0; i < noise.getNumSamples(); ++i)
                allFinite = allFinite && std::isfinite (noise.getSample (0, i))
                                      && std::isfinite (noise.getSample (1, i));
        }
        expect (allFinite, "Looper must keep output finite over repeated blocks");
        expect (fadeLooper.getLoopLengthSamples() <= fadeLooper.getMaxLoopSamples(), "Loop length remains bounded");
    }
};

PitchHarmonyUnitTests pitchHarmonyTests;
StereoLooperUnitTests stereoLooperTests;
} // namespace

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (true);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    juce::Logger::writeToLog ("StringForge DSP test failures: " + juce::String (failures));
    return failures == 0 ? 0 : 1;
}
