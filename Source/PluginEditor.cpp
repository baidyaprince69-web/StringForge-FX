#include "PluginEditor.h"

StringForgeAudioProcessorEditor::StringForgeAudioProcessorEditor (StringForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    generic = std::make_unique<juce::GenericAudioProcessorEditor> (processor);
    addAndMakeVisible (*generic);
    setResizable (true, true);
    setResizeLimits (560, 640, 1100, 1300);
    setSize (650, 900);
}

void StringForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (20, 23, 29));
    g.setColour (juce::Colour::fromRGB (237, 190, 101));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("STRINGFORGE  |  STRING MULTI-FX", 16, 8, getWidth() - 32, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour::fromRGB (155, 165, 180));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("ENGINEERING SCAFFOLD — NOT A FINISHED PEDAL MODEL", 16, 35, getWidth() - 32, 18, juce::Justification::centredLeft);
}

void StringForgeAudioProcessorEditor::resized()
{
    generic->setBounds (8, 60, getWidth() - 16, getHeight() - 68);
}
