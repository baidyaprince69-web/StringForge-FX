#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class StringForgeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StringForgeAudioProcessorEditor (StringForgeAudioProcessor&);
    ~StringForgeAudioProcessorEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    StringForgeAudioProcessor& processor;
    std::unique_ptr<juce::GenericAudioProcessorEditor> generic;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringForgeAudioProcessorEditor)
};
