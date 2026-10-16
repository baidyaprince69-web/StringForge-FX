
#include "PluginEditor.h"

StringForgeAudioProcessorEditor::StringForgeAudioProcessorEditor (
    StringForgeAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    generic = std::make_unique<juce::GenericAudioProcessorEditor> (
        processor);

    addAndMakeVisible (*generic);

    setResizable (true, true);
    setResizeLimits (560, 640, 1100, 1300);
    setSize (760, 900);

    startTimerHz (30);
}

void StringForgeAudioProcessorEditor::timerCallback()
{
    updateSpectrum();
    repaint();
}

void StringForgeAudioProcessorEditor::updateSpectrum()
{
    // Copy recent samples from the processor's audio ring buffer.
    processor.copyLatestSpectrumSamples (
        audioSamples.data(), fftSize);

    // Prepare FFT input and apply a Hann window to reduce spectral leakage.
    fftData.fill (0.0f);

    for (int i = 0; i < fftSize; ++i)
    {
        const float window =
            0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi
                * static_cast<float> (i)
                / static_cast<float> (fftSize - 1));

        fftData[static_cast<size_t> (i)] =
            audioSamples[static_cast<size_t> (i)] * window;
    }

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float normalisation =
        2.0f / static_cast<float> (fftSize);

    for (int i = 0; i <= fftSize / 2; ++i)
    {
        const float amplitude =
            fftData[static_cast<size_t> (i)] * normalisation;

        // Convert amplitude to a display level in decibels.
        const float decibels = juce::Decibels::gainToDecibels (
            amplitude, -90.0f);

        const float level = juce::jlimit (
            0.0f, 1.0f, (decibels + 90.0f) / 90.0f);

        // Smooth movement between successive frames.
        auto& current = magnitudes[static_cast<size_t> (i)];
        current = current * 0.70f + level * 0.30f;
    }
}

void StringForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto background = juce::Colour::fromRGB (20, 23, 29);
    const auto gold = juce::Colour::fromRGB (237, 190, 101);
    const auto muted = juce::Colour::fromRGB (145, 155, 170);
    const auto grid = juce::Colour::fromRGB (49, 56, 67);

    g.fillAll (background);

    // Plugin heading.
    g.setColour (gold);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (
        "STRINGFORGE  |  STRING MULTI-FX",
        16, 8, getWidth() - 32, 28,
        juce::Justification::centredLeft);

    g.setColour (muted);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (
        "LIVE SPECTRUM ANALYZER",
        16, 35, getWidth() - 32, 18,
        juce::Justification::centredLeft);

    // Analyzer panel.
    const juce::Rectangle<float> panel (
        16.0f, 60.0f,
        static_cast<float> (getWidth() - 32),
        190.0f);

    g.setColour (juce::Colour::fromRGB (13, 16, 21));
    g.fillRoundedRectangle (panel, 8.0f);

    g.setColour (grid);
    g.drawRoundedRectangle (panel, 8.0f, 1.0f);

    auto graph = panel.reduced (42.0f, 14.0f);
    graph.removeFromBottom (22.0f);

    // Horizontal level grid.
    for (int db = 0; db >= -80; db -= 20)
    {
        const float proportion =
            static_cast<float> (0 - db) / 90.0f;

        const float y =
            graph.getY() + proportion * graph.getHeight();

        g.setColour (grid);
        g.drawHorizontalLine (
            juce::roundToInt (y),
            graph.getX(),
            graph.getRight());

        g.setColour (muted);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (
            juce::String (db) + " dB",
            18,
            juce::roundToInt (y - 6.0f),
            38,
            12,
            juce::Justification::centredLeft);
    }

    // Logarithmic frequency grid: 20 Hz to 20 kHz.
    const std::array<float, 10> frequencies {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };

    const double sampleRate = processor.sampleRateHz;
    const float nyquist = static_cast<float> (sampleRate * 0.5);

    auto frequencyToX = [&] (float frequency)
    {
        const float fraction =
            std::log10 (frequency / 20.0f)
            / std::log10 (20000.0f / 20.0f);

        return graph.getX() + fraction * graph.getWidth();
    };

    for (const float frequency : frequencies)
    {
        const float x = frequencyToX (frequency);

        g.setColour (grid);
        g.drawVerticalLine (
            juce::roundToInt (x),
            graph.getY(),
            graph.getBottom());

        g.setColour (muted);
        g.setFont (juce::FontOptions (9.0f));

        juce::String label;

        if (frequency >= 1000.0f)
            label = juce::String (frequency / 1000.0f, 0) + "k";
        else
            label = juce::String (static_cast<int> (frequency));

        g.drawText (
            label,
            juce::roundToInt (x - 16.0f),
            juce::roundToInt (graph.getBottom() + 5.0f),
            34,
            12,
            juce::Justification::centred);
    }

    // Draw the live spectrum curve.
    juce::Path spectrumPath;
    bool firstPoint = true;

    for (int bin = 1; bin <= fftSize / 2; ++bin)
    {
        const float frequency =
            static_cast<float> (bin) * static_cast<float> (sampleRate)
            / static_cast<float> (fftSize);

        if (frequency < 20.0f || frequency > 20000.0f
            || frequency > nyquist)
            continue;

        const float x = frequencyToX (frequency);

        const float level =
            magnitudes[static_cast<size_t> (bin)];

        const float y =
            graph.getBottom() - level * graph.getHeight();

        if (firstPoint)
        {
            spectrumPath.startNewSubPath (x, y);
            firstPoint = false;
        }
        else
        {
            spectrumPath.lineTo (x, y);
        }
    }

    g.setColour (juce::Colour::fromRGB (75, 220, 175));
    g.strokePath (
        spectrumPath,
        juce::PathStrokeType (
            2.0f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

    g.setColour (muted);
    g.setFont (juce::FontOptions (9.0f));

    if (sampleRate < 40000.0)
    {
        g.drawText (
            "High frequencies limited by current sample rate",
            16, 252, getWidth() - 32, 14,
            juce::Justification::centredLeft);
    }
}

void StringForgeAudioProcessorEditor::resized()
{
    // Keep the existing plugin controls beneath the analyzer.
    generic->setBounds (
        8, 274, getWidth() - 16, getHeight() - 282);
}
