#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using APVTS = juce::AudioProcessorValueTreeState;
    auto makeBool (const char* id, const char* name, bool def = false)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    }
    auto makeFloat (const char* id, const char* name, float lo, float hi, float def,
                    juce::AudioParameterFloatAttributes attr = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi), def, attr);
    }
    auto makeChoice (const char* id, const char* name, juce::StringArray choices, int def = 0)
    {
        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, 1 }, name, choices, def);
    }
    juce::StringArray noteNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }
}

StringForgeAudioProcessor::StringForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STRINGFORGE_STATE", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout StringForgeAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout p;
    // Global gain and blend. The blend is a global post-chain dry/wet control, not an auto-headroom control.
    p.add (makeFloat (ids::inputGain,  "Master Input",  -24.0f, 24.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::outputGain, "Master Output", -24.0f, 12.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::globalWet,  "Master Wet/Dry", 0.0f, 1.0f, 1.0f));
    p.add (makeBool  (ids::globalBypass, "Global Bypass"));

    p.add (makeBool (ids::wahBypass, "FX17 Bypass"));
    p.add (makeChoice (ids::wahMode, "FX17 Mode", { "Wah", "Volume" }, 0));
    p.add (makeFloat (ids::wahContour, "FX17 Contour", 0.0f, 1.0f, 0.5f));
    p.add (makeFloat (ids::wahPosition, "FX17 Position", 0.0f, 1.0f, 0.5f));

    p.add (makeBool (ids::eqBypass, "GE-7 Bypass"));
    p.add (makeFloat (ids::eq100,  "EQ 100 Hz",  -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq200,  "EQ 200 Hz",  -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq400,  "EQ 400 Hz",  -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq800,  "EQ 800 Hz",  -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq1600, "EQ 1.6 kHz", -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq3200, "EQ 3.2 kHz", -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eq6400, "EQ 6.4 kHz", -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.add (makeFloat (ids::eqLevel, "EQ Level", -15.0f, 15.0f, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));

    p.add (makeBool (ids::tunerBypass, "Tuner Bypass"));
    p.add (makeChoice (ids::tunerMode, "Tuner Mode", { "Chromatic", "Polyphonic" }, 0));
    p.add (makeFloat (ids::tunerCalibration, "Tuner Calibration", 435.0f, 445.0f, 440.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    p.add (makeBool (ids::tunerMute, "Tuner Mute"));

    p.add (makeBool (ids::pitchBypass, "PS-6 Bypass"));
    p.add (makeChoice (ids::pitchMode, "Pitch Mode", { "Intelligent Harmony", "Pitch Shift", "Detune", "Super Bend" }, 0));
    p.add (makeChoice (ids::pitchKey, "Harmony Key", noteNames(), 0));
    p.add (makeChoice (ids::pitchScale, "Harmony Scale", { "Major", "Minor", "Chromatic" }, 0));
    p.add (makeFloat (ids::pitchInterval, "Pitch Interval", -36.0f, 36.0f, 7.0f, juce::AudioParameterFloatAttributes().withLabel ("semitones")));
    p.add (makeChoice (ids::pitchHarmonyVoice, "Harmony Voice", { "Third Above", "Fifth Above", "Octave Down" }, 0));
    p.add (makeFloat (ids::pitchBalance, "Pitch Balance", 0.0f, 1.0f, 0.5f));
    p.add (makeFloat (ids::pitchRise, "Super Bend Rise", 0.01f, 2.0f, 0.5f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    p.add (makeFloat (ids::pitchBend, "Super Bend Amount", 0.0f, 1.0f, 0.5f));

    p.add (makeBool (ids::delayBypass, "Flashback Bypass"));
    p.add (makeChoice (ids::delayType, "Delay Type", { "2290 Digital", "Analog Slap", "Tape", "Dynamic", "Modulation" }, 0));
    p.add (makeFloat (ids::delayTime, "Delay Time", 1.0f, 2000.0f, 375.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    p.add (makeFloat (ids::delayFeedback, "Delay Feedback", 0.0f, 0.95f, 0.25f));
    p.add (makeFloat (ids::delayLevel, "Delay Level", 0.0f, 1.0f, 0.25f));
    p.add (makeChoice (ids::delaySubdivision, "Delay Subdivision", { "Quarter", "Eighth", "Dotted Eighth", "Triplet" }, 0));
    p.add (makeFloat (ids::delayMash, "Delay MASH", 0.0f, 1.0f, 0.0f));

    p.add (makeBool (ids::looperBypass, "JamMan Bypass"));
    p.add (makeFloat (ids::loopLevel, "Loop Level", 0.0f, 1.0f, 0.8f));
    p.add (makeBool (ids::loopRecord, "Loop Record"));
    p.add (makeBool (ids::loopPlay, "Loop Play"));
    p.add (makeBool (ids::loopOverdub, "Loop Overdub"));
    p.add (makeBool (ids::loopStop, "Loop Stop"));
    p.add (makeBool (ids::loopClear, "Loop Clear"));
    p.add (makeBool (ids::loopReverse, "Loop Reverse"));
    p.add (makeBool (ids::loopTempoSync, "Loop Tempo Sync"));
    p.add (makeFloat (ids::loopQuantize, "Loop Quantize", 0.0f, 1.0f, 0.0f));
    p.add (makeFloat (ids::loopBpm, "Loop BPM", 30.0f, 300.0f, 120.0f, juce::AudioParameterFloatAttributes().withLabel ("BPM")));
    p.add (makeFloat (ids::loopTracks, "Loop Layers", 1.0f, 4.0f, 4.0f));

    p.add (makeBool (ids::reverbBypass, "Holy Grail Bypass"));
    p.add (makeChoice (ids::reverbType, "Reverb Algorithm", { "Spring", "Hall", "Flerb" }, 1));
    p.add (makeFloat (ids::reverbAmount, "Reverb Amount", 0.0f, 1.0f, 0.25f));
    return p;
}

float StringForgeAudioProcessor::value (const char* parameterID) const noexcept
{
    if (auto* v = parameters.getRawParameterValue (parameterID))
        return v->load (std::memory_order_relaxed);
    return 0.0f;
}


void StringForgeAudioProcessor::pushSpectrumSamples(
    const juce::AudioBuffer<float>& buffer
) noexcept
{
    const int channels = juce::jmin(2, buffer.getNumChannels());
    const int samples = buffer.getNumSamples();

    unsigned int position =
        spectrumWritePosition.load(std::memory_order_relaxed);

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.getSample(ch, i);

        if (channels > 0)
            mono /= static_cast<float>(channels);

        spectrumRing[position % spectrumBufferSize].store(
            mono, std::memory_order_relaxed);

        ++position;
    }

    spectrumWritePosition.store(
        position, std::memory_order_release);
}

void StringForgeAudioProcessor::copyLatestSpectrumSamples(
    float* destination,
    int numSamples
) const noexcept
{
    if (destination == nullptr || numSamples <= 0)
        return;

    const unsigned int end =
        spectrumWritePosition.load(std::memory_order_acquire);

    const unsigned int count = static_cast<unsigned int>(
        juce::jmin(numSamples, spectrumBufferSize));

    const unsigned int start = end - count;

    for (unsigned int i = 0; i < count; ++i)
    {
        destination[i] = spectrumRing[
            (start + i) % spectrumBufferSize
        ].load(std::memory_order_relaxed);
    }

    for (int i = static_cast<int>(count); i < numSamples; ++i)
        destination[i] = 0.0f;
}

void StringForgeAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRateHz = juce::jmax (1.0, sr);
    inputGainSmoothed.reset (sampleRateHz, 0.02);
    outputGainSmoothed.reset (sampleRateHz, 0.02);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (ids::inputGain)));
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (ids::outputGain)));
    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    floatConversionBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    pitchHarmony.prepare (sampleRateHz, samplesPerBlock, 2, 8.0f);
    previousPitchSmoothingMilliseconds = -1.0f;
    stereoLooper.prepare (sampleRateHz, samplesPerBlock, 2, 30.0, 4, 5.0f);
    stereoLooper.setBpm (value (ids::loopBpm));
    stereoLooper.setQuantizeEnabled (value (ids::loopTempoSync) >= 0.5f && value (ids::loopQuantize) >= 0.5f);
    stereoLooper.setMaxLayerLimit ((int) std::lround (value (ids::loopTracks)));
    previousLooperCommandStates = { value (ids::loopRecord) >= 0.5f,
                                    value (ids::loopPlay) >= 0.5f,
                                    value (ids::loopOverdub) >= 0.5f,
                                    value (ids::loopStop) >= 0.5f,
                                    value (ids::loopClear) >= 0.5f,
                                    value (ids::loopReverse) >= 0.5f };

    for (auto& ch : wahFilters)
    {
        ch.reset();
        ch.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRateHz, 900.0, 1.2);
    }
    const std::array<float, 7> freqs { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
    for (int ch = 0; ch < 2; ++ch)
        for (int b = 0; b < 7; ++b)
        {
            eqFilters[(size_t) ch][(size_t) b].reset();
            eqFilters[(size_t) ch][(size_t) b].coefficients =
                juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRateHz, freqs[(size_t) b], 1.0,
                                                                    juce::Decibels::decibelsToGain (0.0f));
        }

    maxDelaySamples = juce::jmax (2, (int) std::ceil (sampleRateHz * 2.1));
    delayMemory.assign (2, std::vector<float> ((size_t) maxDelaySamples, 0.0f));
    delayWrite = 0;
    reverb.reset();
    reverbParams = {};
    reverbParams.roomSize = 0.65f;
    reverbParams.damping = 0.35f;
    reverbParams.wetLevel = 0.25f;
    reverbParams.dryLevel = 1.0f;
    reverbParams.width = 1.0f;
    reverb.setParameters (reverbParams);
}

void StringForgeAudioProcessor::releaseResources()
{
    reverb.reset();
    pitchHarmony.reset();
    stereoLooper.reset();
    delayMemory.clear();
}

bool StringForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void StringForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Some hosts occasionally exceed the prepareToPlay block hint. Process such
    // blocks as non-owning views over preallocated chunks; never resize a scratch
    // buffer inside the real-time callback.
    const int capacity = juce::jmax (1, dryBuffer.getNumSamples());
    if (buffer.getNumSamples() <= capacity)
    {
        processFloatPath (buffer, midi);
        return;
    }

    const int channels = juce::jmin (2, buffer.getNumChannels());
    for (int offset = 0; offset < buffer.getNumSamples(); offset += capacity)
    {
        const int count = juce::jmin (capacity, buffer.getNumSamples() - offset);
        std::array<float*, 2> pointers { nullptr, nullptr };
        for (int ch = 0; ch < channels; ++ch)
            pointers[(size_t) ch] = buffer.getWritePointer (ch, offset);
        juce::AudioBuffer<float> view (pointers.data(), channels, count);
        processFloatPath (view, midi);
    }
}

void StringForgeAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // Compatibility adapter only. It uses preallocated scratch storage and chunks large host blocks.
    // This is NOT a double-native DSP path; supportsDoublePrecisionProcessing() therefore returns false.
    const int channels = juce::jmin (2, buffer.getNumChannels());
    const int capacity = juce::jmax (1, floatConversionBuffer.getNumSamples());
    for (int offset = 0; offset < buffer.getNumSamples(); offset += capacity)
    {
        const int count = juce::jmin (capacity, buffer.getNumSamples() - offset);
        std::array<float*, 2> pointers { floatConversionBuffer.getWritePointer (0), floatConversionBuffer.getWritePointer (1) };
        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < count; ++n)
                pointers[(size_t) ch][n] = (float) buffer.getSample (ch, offset + n);
        juce::AudioBuffer<float> view (pointers.data(), channels, count);
        processFloatPath (view, midi);
        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < count; ++n)
                buffer.setSample (ch, offset + n, (double) pointers[(size_t) ch][n]);
    }
}

void StringForgeAudioProcessor::processFloatPath (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // MIDI note-on events can drive intelligent harmony in MIDI-controlled setups.
    // Audio-only violin input still needs a dedicated low-latency pitch tracker.
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
        {
            lastMidiNote = metadata.getMessage().getNoteNumber();
            hasMidiNote = true;
        }
    serviceLooperParameterCommands();
    stereoLooper.setBpm (value (ids::loopBpm));
    stereoLooper.setQuantizeEnabled (value (ids::loopTempoSync) >= 0.5f && value (ids::loopQuantize) >= 0.5f);
    stereoLooper.setMaxLayerLimit ((int) std::lround (value (ids::loopTracks)));

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0) return;

    pushSpectrumSamples (buffer);

    if (numSamples > dryBuffer.getNumSamples())
    {
        // Defensive fail-safe; normal processBlock() chunks over-sized callbacks.
        buffer.clear();
        return;
    }
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (value (ids::inputGain)));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (value (ids::outputGain)));
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = inputGainSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, n, buffer.getSample (ch, n) * g);
    }

    // 1) FX17-inspired stage: smooth, resonant band-pass sweep; NOT a circuit-exact FX17 model.
    if (value (ids::wahBypass) < 0.5f)
    {
        if ((int) value (ids::wahMode) == 1)
        {
            const float gain = value (ids::wahPosition);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.applyGain (ch, 0, numSamples, gain);
        }
        else
        {
            const float pos = value (ids::wahPosition);
            const float contour = value (ids::wahContour);
            const double f = 250.0 * std::pow (12.0, (double) pos);
            const double q = 0.7 + 5.0 * (double) contour;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                wahFilters[(size_t) ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRateHz, f, q);
                auto* x = buffer.getWritePointer (ch);
                for (int n = 0; n < numSamples; ++n) x[n] = wahFilters[(size_t) ch].processSample (x[n]);
            }
        }
    }

    // 2) Seven-band peaking EQ. This is an approximate graphic EQ, not a component-level analog model.
    if (value (ids::eqBypass) < 0.5f)
    {
        const std::array<float, 7> freqs { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
        const std::array<const char*, 7> idsEQ { ids::eq100, ids::eq200, ids::eq400, ids::eq800, ids::eq1600, ids::eq3200, ids::eq6400 };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            for (int b = 0; b < 7; ++b)
            {
                const float db = value (idsEQ[(size_t) b]);
                eqFilters[(size_t) ch][(size_t) b].coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRateHz, freqs[(size_t) b], 1.0, juce::Decibels::decibelsToGain (db));
                auto& filter = eqFilters[(size_t) ch][(size_t) b];
                for (int n = 0; n < numSamples; ++n) x[n] = filter.processSample (x[n]);
            }
            buffer.applyGain (ch, 0, numSamples, juce::Decibels::decibelsToGain (value (ids::eqLevel)));
        }
    }

    // 3) Tuner side-chain is intentionally not implemented yet. Tuner mute is implemented.
    // A production tuner should analyse a copied pre/post-EQ signal and must not run FFT work on the UI thread.
    if (value (ids::tunerBypass) < 0.5f && value (ids::tunerMute) >= 0.5f)
        buffer.clear();

    // 4) Low-latency PS-6-inspired pitch/harmony engine.
    // The delay-line shifter is a performance-quality first implementation, not a transparent studio shifter.
    if (value (ids::pitchBypass) < 0.5f)
    {
        const int mode = (int) std::lround (value (ids::pitchMode));
        const float balance = juce::jlimit (0.0f, 1.0f, value (ids::pitchBalance));
        const float interval = value (ids::pitchInterval);
        const float smoothingMilliseconds = mode == 3
            ? juce::jlimit (10.0f, 2000.0f, value (ids::pitchRise) * 1000.0f)
            : 8.0f;
        if (previousPitchSmoothingMilliseconds < 0.0f
            || std::abs (smoothingMilliseconds - previousPitchSmoothingMilliseconds) > 0.05f)
        {
            pitchHarmony.setPitchSmoothingMilliseconds (smoothingMilliseconds);
            previousPitchSmoothingMilliseconds = smoothingMilliseconds;
        }
        if (mode == 0) // Intelligent harmony; requires note-on MIDI or a future audio pitch tracker.
        {
            const int scaleChoice = (int) std::lround (value (ids::pitchScale));
            if (scaleChoice == 2) // Chromatic mode follows the selected semitone interval.
                pitchHarmony.processPitchShift (buffer, interval, balance);
            else if (hasMidiNote)
            {
                const auto scale = scaleChoice == 1 ? stringforge::dsp::HarmonyScale::NaturalMinor
                                                    : stringforge::dsp::HarmonyScale::Major;
                const auto voiceIndex = juce::jlimit (0, 2, (int) std::lround (value (ids::pitchHarmonyVoice)));
                const auto voice = static_cast<stringforge::dsp::HarmonyVoice> (voiceIndex);
                pitchHarmony.processHarmony (buffer, lastMidiNote, (int) std::lround (value (ids::pitchKey)),
                                             scale, voice, balance);
            }
            // Audio-only harmony is intentionally not guessed from a fixed MIDI note.
            // Connect a monophonic audio pitch tracker here for non-MIDI instruments.
        }
        else if (mode == 1)
            pitchHarmony.processPitchShift (buffer, interval, balance);
        else if (mode == 2)
            pitchHarmony.processPitchShift (buffer, (value (ids::pitchBend) - 0.5f) * 1.0f, balance);
        else // Super Bend: target interval is reached through the shifter's smoothed pitch parameter.
            pitchHarmony.processPitchShift (buffer, interval * value (ids::pitchBend), balance);
    }

    // 5) Flashback-inspired feedback delay. Basic digital delay core; delay-type coloration and MASH are TODOs.
    if (value (ids::delayBypass) < 0.5f && ! delayMemory.empty())
    {
        const int delaySamples = juce::jlimit (1, maxDelaySamples - 1,
            (int) std::round ((double) value (ids::delayTime) * sampleRateHz / 1000.0));
        const float feedback = juce::jlimit (0.0f, 0.95f, value (ids::delayFeedback));
        const float level = juce::jlimit (0.0f, 1.0f, value (ids::delayLevel));
        for (int n = 0; n < numSamples; ++n)
        {
            const int readIndex = (delayWrite - delaySamples + maxDelaySamples) % maxDelaySamples;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float input = buffer.getSample (ch, n);
                const float delayed = delayMemory[(size_t) ch][(size_t) readIndex];
                delayMemory[(size_t) ch][(size_t) delayWrite] = input + delayed * feedback;
                buffer.setSample (ch, n, input + delayed * level);
            }
            delayWrite = (delayWrite + 1) % maxDelaySamples;
        }
    }

    // 6) Preallocated stereo looper. Command parameters are edge-triggered below.
    // All track storage was allocated in prepareToPlay; processBlock performs no vector growth.
    if (value (ids::looperBypass) < 0.5f)
        stereoLooper.processBlock (buffer, value (ids::loopLevel), 0.95f, true);

    // 7) JUCE reverb foundation. Spring and Flerb-specific algorithms are TODOs; Hall is the baseline approximation.
    if (value (ids::reverbBypass) < 0.5f)
    {
        const float amount = juce::jlimit (0.0f, 1.0f, value (ids::reverbAmount));
        reverbParams.wetLevel = amount;
        reverbParams.dryLevel = 1.0f;
        reverbParams.roomSize = 0.25f + amount * 0.7f;
        reverbParams.damping = 0.2f + (1.0f - amount) * 0.6f;
        reverbParams.width = 1.0f;
        reverb.setParameters (reverbParams);
        if (numChannels >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        else
            reverb.processMono (buffer.getWritePointer (0), numSamples);
    }

    // Global wet/dry and output gain. Blend stays sample-aligned; no look-ahead limiter is used here.
    const float wet = juce::jlimit (0.0f, 1.0f, value (ids::globalWet));
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
            x[n] = dry[n] * (1.0f - wet) + x[n] * wet;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        const float g = outputGainSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float x = buffer.getSample (ch, n) * g;
            if (value (ids::globalBypass) >= 0.5f)
                x = dryBuffer.getSample (ch, n);
            buffer.setSample (ch, n, x);
        }
    }
}

void StringForgeAudioProcessor::serviceLooperParameterCommands() noexcept
{
    const std::array<const char*, 6> parameterIDs
    {
        ids::loopRecord, ids::loopPlay, ids::loopOverdub,
        ids::loopStop, ids::loopClear, ids::loopReverse
    };
    const std::array<stringforge::dsp::StereoLooper::Command, 6> commands
    {
        stringforge::dsp::StereoLooper::Command::Record,
        stringforge::dsp::StereoLooper::Command::Play,
        stringforge::dsp::StereoLooper::Command::Overdub,
        stringforge::dsp::StereoLooper::Command::Stop,
        stringforge::dsp::StereoLooper::Command::Clear,
        stringforge::dsp::StereoLooper::Command::Reverse
    };

    for (size_t i = 0; i < parameterIDs.size(); ++i)
    {
        const bool now = value (parameterIDs[i]) >= 0.5f;
        if (now && ! previousLooperCommandStates[i])
            stereoLooper.request (commands[i]);
        previousLooperCommandStates[i] = now;
    }
}

juce::AudioProcessorEditor* StringForgeAudioProcessor::createEditor()
{
    return new StringForgeAudioProcessorEditor (*this);
}

double StringForgeAudioProcessor::getTailLengthSeconds() const
{
    return juce::jmax (2.1, (double) value (ids::delayTime) / 1000.0 * 20.0);
}

void StringForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void StringForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StringForgeAudioProcessor();
}

#pragma once
#include <JuceHeader.h>
#include "ParameterIDs.h"
#include "DSP/PitchHarmonyEngine.h"
#include "DSP/StereoLooper.h"
#include <array>

