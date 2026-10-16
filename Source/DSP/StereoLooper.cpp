#include "StereoLooper.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace stringforge::dsp
{
namespace
{
constexpr int minimumLoopSamples = 128;
}

void StereoLooper::prepare (double sampleRateHz,
                            int maxBlockSize,
                            int maxChannels,
                            double maxLoopSeconds,
                            int maxLayers,
                            float crossfadeMilliseconds)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRateHz_ = juce::jmax (8000.0, sampleRateHz);
    maxChannels_ = juce::jlimit (1, 2, maxChannels);
    const auto boundedSeconds = juce::jlimit (1.0, 600.0, maxLoopSeconds);
    const int requestedLayers = juce::jlimit (1, 8, maxLayers);
    constexpr double maxAudioStorageBytes = 256.0 * 1024.0 * 1024.0;
    const double audioBytesPerLoopSample = (double) requestedLayers * 2.0 * (double) sizeof (float);
    const double stampBytesPerLoopSample = requestedLayers > 1 ? 2.0 * (double) sizeof (uint64_t) : 0.0;
    const double bytesPerLoopSample = audioBytesPerLoopSample + stampBytesPerLoopSample;
    const double budgetSamples = maxAudioStorageBytes / bytesPerLoopSample;
    const double requestedSamples = boundedSeconds * sampleRateHz_;
    maxLoopSamples_ = juce::jmax (minimumLoopSamples, (int) std::min ({ requestedSamples,
        budgetSamples, (double) std::numeric_limits<int>::max() / 4.0 }));
    crossfadeSamples_ = juce::jlimit (2, juce::jmax (2, maxLoopSamples_ / 8),
        (int) std::lround (sampleRateHz_ * juce::jlimit (0.5f, 20.0f, crossfadeMilliseconds) / 1000.0));

    try
    {
        trackBuffers_.clear();
        trackBuffers_.reserve ((size_t) requestedLayers);
        for (int i = 0; i < requestedLayers; ++i)
        {
            auto track = std::make_unique<juce::AudioBuffer<float>>();
            track->setSize (2, maxLoopSamples_, false, true, true);
            track->clear();
            trackBuffers_.push_back (std::move (track));
        }

        const auto stampCount = requestedLayers > 1 ? (size_t) maxLoopSamples_ * 2u : 0u;
        writeStamps_.assign (stampCount, 0u);
        layerStampGeneration_.assign (trackBuffers_.size(), 0u);
        fadeOutWeights_.resize ((size_t) crossfadeSamples_);
        fadeInWeights_.resize ((size_t) crossfadeSamples_);
    }
    catch (const std::bad_alloc&)
    {
        trackBuffers_.clear();
        writeStamps_.clear();
        layerStampGeneration_.clear();
        fadeOutWeights_.clear();
        fadeInWeights_.clear();
        prepared_ = false;
        maxLoopSamples_ = 0;
        crossfadeSamples_ = 0;
        clearLogicalState();
        publishStatus();
        return;
    }
    for (int i = 0; i < crossfadeSamples_; ++i)
    {
        const float t = (float) i / (float) (crossfadeSamples_ - 1);
        fadeOutWeights_[(size_t) i] = (float) std::cos (0.5 * 3.14159265358979323846 * t);
        fadeInWeights_[(size_t) i] = (float) std::sin (0.5 * 3.14159265358979323846 * t);
    }
    maxLayerLimit_.store ((int) trackBuffers_.size(), std::memory_order_relaxed);
    loopLevelSmoothed_.reset (sampleRateHz_, 0.005);
    loopLevelSmoothed_.setCurrentAndTargetValue (0.8f);
    prepared_ = true;
    reset();
}

void StereoLooper::reset() noexcept
{
    state_ = State::Empty;
    pendingRecordDestination_ = State::Playing;
    pendingCommands_.store (0u, std::memory_order_relaxed);
    recordSamples_ = 0;
    loopLengthSamples_ = 0;
    playhead_ = 0;
    activeLayerCount_ = 0;
    committedLayerCount_ = 0;
    overdubLayerIndex_ = -1;
    overdubWroteAny_ = false;
    pendingFinalizeRecording_ = false;
    reverse_ = false;
    absoluteSampleClock_ = 0;
    // Keep the generation counter monotonic across reset(); sample stamps are not
    // scrubbed on the audio thread, so reusing a small generation could expose stale data.
    loopLevelSmoothed_.setCurrentAndTargetValue (0.8f);
    publishStatus();
}

void StereoLooper::publishStatus() noexcept
{
    publishedState_.store (state_, std::memory_order_relaxed);
    publishedLoopLength_.store (loopLengthSamples_, std::memory_order_relaxed);
    publishedLayerCount_.store (activeLayerCount_, std::memory_order_relaxed);
    publishedReverse_.store (reverse_, std::memory_order_relaxed);
}

void StereoLooper::setBpm (float bpm) noexcept
{
    bpm_.store (juce::jlimit (30.0f, 300.0f, bpm), std::memory_order_relaxed);
}

void StereoLooper::setQuantizeBeatMultiple (float beats) noexcept
{
    quantizeBeatMultiple_.store (juce::jlimit (0.25f, 4.0f, beats), std::memory_order_relaxed);
}

int StereoLooper::samplesPerQuantizeGrid() const noexcept
{
    const double bpm = (double) bpm_.load (std::memory_order_relaxed);
    const double beats = (double) quantizeBeatMultiple_.load (std::memory_order_relaxed);
    return juce::jmax (1, (int) std::lround (sampleRateHz_ * 60.0 / bpm * beats));
}

bool StereoLooper::isGridBoundary() const noexcept
{
    if (! quantizeEnabled_.load (std::memory_order_relaxed))
        return true;
    const auto grid = (uint64_t) samplesPerQuantizeGrid();
    return grid > 0 && (absoluteSampleClock_ % grid) == 0;
}

void StereoLooper::clearLogicalState() noexcept
{
    // Intentionally do not memset loop buffers: doing so could consume a large,
    // unbounded amount of time on the audio thread. No stale sample is reachable
    // while the logical loop length and layer count are zero.
    state_ = State::Empty;
    recordSamples_ = 0;
    loopLengthSamples_ = 0;
    playhead_ = 0;
    activeLayerCount_ = 0;
    committedLayerCount_ = 0;
    overdubLayerIndex_ = -1;
    overdubWroteAny_ = false;
    pendingFinalizeRecording_ = false;
}

void StereoLooper::startNewRecording() noexcept
{
    recordSamples_ = 0;
    loopLengthSamples_ = 0;
    activeLayerCount_ = 0;
    committedLayerCount_ = 0;
    overdubLayerIndex_ = -1;
    overdubWroteAny_ = false;
    pendingFinalizeRecording_ = false;
    playhead_ = 0;
    pendingRecordDestination_ = State::Playing;

    if (quantizeEnabled_.load (std::memory_order_relaxed))
        state_ = State::WaitingToRecord;
    else
        state_ = State::Recording;
}

void StereoLooper::requestFinalizeRecording (State destination) noexcept
{
    if (state_ != State::Recording)
        return;

    pendingRecordDestination_ = destination;
    if (quantizeEnabled_.load (std::memory_order_relaxed))
        pendingFinalizeRecording_ = true;
    else
        finalizeRecording (destination);
}

void StereoLooper::finalizeRecording (State destination) noexcept
{
    pendingFinalizeRecording_ = false;
    if (recordSamples_ < minimumLoopSamples_)
    {
        clearLogicalState();
        return;
    }

    loopLengthSamples_ = juce::jmin (recordSamples_, maxLoopSamples_);
    activeLayerCount_ = 1;
    committedLayerCount_ = 1;
    overdubLayerIndex_ = -1;
    overdubWroteAny_ = false;
    playhead_ = reverse_ ? loopLengthSamples_ - 1 : 0;
    state_ = destination;
}

void StereoLooper::startOverdub() noexcept
{
    const int usableLayers = juce::jmin ((int) trackBuffers_.size(), maxLayerLimit_.load (std::memory_order_relaxed));
    if (loopLengthSamples_ < minimumLoopSamples_ || activeLayerCount_ >= usableLayers)
        return;

    // Starting a new overdub after Undo discards the redo branch logically.
    committedLayerCount_ = activeLayerCount_;
    overdubLayerIndex_ = activeLayerCount_;
    ++stampGeneration_; // uint64 wrap is practically unreachable in a live session.
    if (stampGeneration_ == 0)
        stampGeneration_ = 1;
    layerStampGeneration_[(size_t) overdubLayerIndex_] = stampGeneration_;
    overdubWroteAny_ = false;
    state_ = State::Overdubbing;
}

void StereoLooper::commitCurrentOverdubIfNeeded() noexcept
{
    if (state_ == State::Overdubbing && overdubWroteAny_ && overdubLayerIndex_ == activeLayerCount_)
    {
        ++activeLayerCount_;
        committedLayerCount_ = activeLayerCount_;
    }
    overdubLayerIndex_ = -1;
    overdubWroteAny_ = false;
}

void StereoLooper::finishOverdub (State destination) noexcept
{
    commitCurrentOverdubIfNeeded();
    state_ = loopLengthSamples_ > 0 ? destination : State::Empty;
}

void StereoLooper::applyCommands (uint32_t commands) noexcept
{
    if ((commands & bit (Command::Clear)) != 0u)
    {
        clearLogicalState();
        // Clear has priority over all other commands coalesced in this block.
        return;
    }

    if ((commands & bit (Command::Reverse)) != 0u && loopLengthSamples_ > 0)
        reverse_ = ! reverse_;

    if ((commands & bit (Command::Undo)) != 0u && state_ != State::Recording
        && state_ != State::WaitingToRecord && state_ != State::Overdubbing && activeLayerCount_ > 1)
    {
        --activeLayerCount_;
        state_ = State::Playing;
    }
    if ((commands & bit (Command::Redo)) != 0u && state_ != State::Recording
        && state_ != State::WaitingToRecord && state_ != State::Overdubbing
        && activeLayerCount_ < committedLayerCount_)
    {
        ++activeLayerCount_;
        state_ = State::Playing;
    }

    if ((commands & bit (Command::Record)) != 0u)
    {
        if (state_ == State::Empty || state_ == State::Stopped)
            startNewRecording();
        else if (state_ == State::WaitingToRecord)
            state_ = State::Empty; // second press cancels an armed quantized take
        else if (state_ == State::Recording)
            requestFinalizeRecording (State::Playing);
    }

    if ((commands & bit (Command::Play)) != 0u)
    {
        if (state_ == State::Recording)
            requestFinalizeRecording (State::Playing);
        else if (state_ == State::Stopped && loopLengthSamples_ >= minimumLoopSamples_)
        {
            state_ = State::Playing;
            playhead_ = reverse_ ? loopLengthSamples_ - 1 : 0;
        }
    }

    if ((commands & bit (Command::Overdub)) != 0u)
    {
        if (state_ == State::Playing)
            startOverdub();
        else if (state_ == State::Overdubbing)
            finishOverdub (State::Playing);
        else if (state_ == State::Stopped && loopLengthSamples_ >= minimumLoopSamples_)
        {
            state_ = State::Playing;
            playhead_ = reverse_ ? loopLengthSamples_ - 1 : 0;
            startOverdub();
        }
    }

    if ((commands & bit (Command::Stop)) != 0u)
    {
        if (state_ == State::Recording)
            requestFinalizeRecording (State::Stopped);
        else if (state_ == State::Overdubbing)
            finishOverdub (State::Stopped);
        else if (state_ == State::Playing)
            state_ = State::Stopped;
        else if (state_ == State::WaitingToRecord)
            state_ = State::Empty;
    }
}

void StereoLooper::writeRecordSample (int channel, int position, float input) noexcept
{
    if (trackBuffers_.empty() || channel < 0 || channel >= 2 || position < 0 || position >= maxLoopSamples_)
        return;
    trackBuffers_[0]->setSample (channel, position, input);
}

void StereoLooper::writeOverdubSample (int channel, int position, float input, float feedback) noexcept
{
    if (overdubLayerIndex_ < 0 || overdubLayerIndex_ >= (int) trackBuffers_.size()
        || channel < 0 || channel >= 2 || position < 0 || position >= loopLengthSamples_)
        return;

    const size_t stampIndex = (size_t) channel * (size_t) maxLoopSamples_ + (size_t) position;
    auto& stamp = writeStamps_[stampIndex];
    auto& target = *trackBuffers_[(size_t) overdubLayerIndex_];
    if (stamp != stampGeneration_)
    {
        target.setSample (channel, position, input);
        stamp = stampGeneration_;
    }
    else
    {
        const float oldSample = target.getSample (channel, position);
        target.setSample (channel, position, oldSample * juce::jlimit (0.0f, 1.0f, feedback) + input);
    }
    overdubWroteAny_ = true;
}

float StereoLooper::readTrackRawOrZero (int layer, int channel, int position) const noexcept
{
    if (layer < 0 || layer >= (int) trackBuffers_.size() || channel < 0 || channel >= 2
        || position < 0 || position >= loopLengthSamples_)
        return 0.0f;

    if (layer > 0)
    {
        const auto expectedGeneration = layerStampGeneration_[(size_t) layer];
        const size_t stampIndex = (size_t) channel * (size_t) maxLoopSamples_ + (size_t) position;
        if (expectedGeneration == 0u || writeStamps_[stampIndex] != expectedGeneration)
            return 0.0f;
    }
    return trackBuffers_[(size_t) layer]->getSample (channel, position);
}

float StereoLooper::readTrackWithCrossfade (int layer, int channel, int position) const noexcept
{
    if (loopLengthSamples_ <= 0)
        return 0.0f;

    const int fade = juce::jmin (crossfadeSamples_, loopLengthSamples_ / 4);
    const float current = readTrackRawOrZero (layer, channel, position);
    if (fade < 2)
        return current;

    if (! reverse_ && position >= loopLengthSamples_ - fade)
    {
        const int fadeOffset = position - (loopLengthSamples_ - fade);
        const int weightIndex = juce::jlimit (0, crossfadeSamples_ - 1,
            (int) std::lround ((double) fadeOffset * (crossfadeSamples_ - 1) / (double) (fade - 1)));
        const float a = readTrackRawOrZero (layer, channel, position);
        const float b = readTrackRawOrZero (layer, channel, fadeOffset);
        return a * fadeOutWeights_[(size_t) weightIndex] + b * fadeInWeights_[(size_t) weightIndex];
    }

    if (reverse_ && position < fade)
    {
        const int fadeOffset = fade - 1 - position;
        const int weightIndex = juce::jlimit (0, crossfadeSamples_ - 1,
            (int) std::lround ((double) fadeOffset * (crossfadeSamples_ - 1) / (double) (fade - 1)));
        const float a = readTrackRawOrZero (layer, channel, position);
        const float b = readTrackRawOrZero (layer, channel, loopLengthSamples_ - fade + position);
        return a * fadeOutWeights_[(size_t) weightIndex] + b * fadeInWeights_[(size_t) weightIndex];
    }

    return current;
}

void StereoLooper::advancePlayhead() noexcept
{
    if (loopLengthSamples_ <= 0)
        return;

    const int fade = juce::jmin (crossfadeSamples_, loopLengthSamples_ / 4);
    if (! reverse_)
    {
        ++playhead_;
        if (playhead_ >= loopLengthSamples_)
            playhead_ = (fade >= 2) ? fade : 0;
    }
    else
    {
        --playhead_;
        if (playhead_ < 0)
            playhead_ = (fade >= 2) ? loopLengthSamples_ - fade - 1 : loopLengthSamples_ - 1;
    }
}

void StereoLooper::processBlock (juce::AudioBuffer<float>& buffer,
                                 float loopLevel,
                                 float overdubFeedback,
                                 bool monitorInput) noexcept
{
    if (! prepared_ || buffer.getNumSamples() <= 0)
        return;

    applyCommands (pendingCommands_.exchange (0u, std::memory_order_acquire));
    loopLevelSmoothed_.setTargetValue (juce::jlimit (0.0f, 2.0f, loopLevel));
    const float feedback = juce::jlimit (0.0f, 1.0f, overdubFeedback);
    const int channels = juce::jmin (maxChannels_, buffer.getNumChannels());
    if (channels <= 0)
    {
        publishStatus();
        return;
    }

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        if (state_ == State::WaitingToRecord && isGridBoundary())
            state_ = State::Recording;

        if (state_ == State::Recording && pendingFinalizeRecording_ && isGridBoundary())
            finalizeRecording (pendingRecordDestination_);

        const bool isRecording = state_ == State::Recording;
        const bool isPlaying = state_ == State::Playing || state_ == State::Overdubbing;
        const float level = loopLevelSmoothed_.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float input = buffer.getSample (ch, n);
            if (isRecording)
            {
                writeRecordSample (ch, recordSamples_, input);
                // The dry live input is monitored while recording.
                buffer.setSample (ch, n, input);
                continue;
            }

            if (isPlaying && state_ == State::Overdubbing)
                writeOverdubSample (ch, playhead_, input, feedback);

            float loopSample = 0.0f;
            if (isPlaying && loopLengthSamples_ > 0)
            {
                const int layersToMix = activeLayerCount_ + ((state_ == State::Overdubbing && overdubLayerIndex_ == activeLayerCount_) ? 1 : 0);
                for (int layer = 0; layer < layersToMix; ++layer)
                    loopSample += readTrackWithCrossfade (layer, ch, playhead_);
            }

            const float live = monitorInput ? input : 0.0f;
            buffer.setSample (ch, n, live + loopSample * level);
        }

        if (isRecording)
        {
            ++recordSamples_;
            if (recordSamples_ >= maxLoopSamples_)
                finalizeRecording (State::Playing);
        }
        else if (isPlaying)
        {
            advancePlayhead();
        }

        ++absoluteSampleClock_;
    }
    publishStatus();
}
} // namespace stringforge::dsp
