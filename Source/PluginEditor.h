
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

class StringForgeAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit StringForgeAudioProcessorEditor (
        StringForgeAudioProcessor&);

    ~StringForgeAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSpectrum();

    StringForgeAudioProcessor& processor;

    std::unique_ptr<juce::GenericAudioProcessorEditor> generic;

    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft { fftOrder };

    std::array<float, fftSize> audioSamples {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2 + 1> magnitudes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        StringForgeAudioProcessorEditor)
};

