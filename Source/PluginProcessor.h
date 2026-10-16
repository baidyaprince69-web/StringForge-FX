#pragma once
#include <JuceHeader.h>
#include "ParameterIDs.h"
#include "DSP/PitchHarmonyEngine.h"
#include "DSP/StereoLooper.h"
#include <array>
#include <atomic>

class StringForgeAudioProcessor final : public juce::AudioProcessor
{
public:
    StringForgeAudioProcessor();
    ~StringForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float value (const char* parameterID) const noexcept;

private:
    void processFloatPath (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void serviceLooperParameterCommands() noexcept;
    double sampleRateHz = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoothed;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> floatConversionBuffer;
    stringforge::dsp::PitchHarmonyEngine pitchHarmony;
    stringforge::dsp::StereoLooper stereoLooper;
    std::array<bool, 6> previousLooperCommandStates { false, false, false, false, false, false };
    int lastMidiNote = 60;
    bool hasMidiNote = false;
    float previousPitchSmoothingMilliseconds = -1.0f;
    std::array<float, 2> wahLowState { 0.0f, 0.0f };
    std::array<juce::dsp::IIR::Filter<float>, 2> wahFilters;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 7>, 2> eqFilters;
    std::vector<std::vector<float>> delayMemory;
    int delayWrite = 0;
    int maxDelaySamples = 1;
// Spectrum analyzer buffer
static constexpr int spectrumBufferSize = 8192;

std::array<std::atomic<float>, spectrumBufferSize> spectrumRing {};
std::atomic<unsigned int> spectrumWritePosition { 0 };

void pushSpectrumSamples(
    const juce::AudioBuffer<float>& buffer
) noexcept;

void copyLatestSpectrumSamples(
    float* destination,
    int numSamples
) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringForgeAudioProcessor)
};
