#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace stringforge::dsp
{
/**
    Fixed-memory stereo looper with up to N additive layers.

    All audio buffers and sample-stamp storage are allocated in prepare().
    request* methods are lock-free commands; call them from GUI or MIDI code.
    Clear is a logical clear (no large memset on the audio thread): stale data is
    unreachable because loopLength and activeLayerCount become zero.

    The first recorded layer establishes loop length. A second Record press
    finishes recording and enters Play (or waits for the next beat if quantized).
    Overdub creates a new undoable layer; repeated overdub passes accumulate in
    that layer with a feedback coefficient. This is layered overdubbing, not a
    set of independently start/stop-addressable tracks.
*/
class StereoLooper final
{
public:
    StereoLooper() = default;
    enum class State : uint8_t
    {
        Empty,
        WaitingToRecord,
        Recording,
        Playing,
        Overdubbing,
        Stopped
    };

    enum class Command : uint32_t
    {
        Record  = 1u << 0,
        Play    = 1u << 1,
        Overdub = 1u << 2,
        Stop    = 1u << 3,
        Clear   = 1u << 4,
        Reverse = 1u << 5,
        Undo    = 1u << 6,
        Redo    = 1u << 7
    };

    void prepare (double sampleRateHz,
                  int maxBlockSize,
                  int maxChannels = 2,
                  double maxLoopSeconds = 30.0,
                  int maxLayers = 4,
                  float crossfadeMilliseconds = 5.0f);

    void reset() noexcept;

    void request (Command command) noexcept
    {
        pendingCommands_.fetch_or ((uint32_t) command, std::memory_order_release);
    }
    void requestRecord() noexcept  { request (Command::Record); }
    void requestPlay() noexcept    { request (Command::Play); }
    void requestOverdub() noexcept { request (Command::Overdub); }
    void requestStop() noexcept    { request (Command::Stop); }
    void requestClear() noexcept   { request (Command::Clear); }
    void requestReverse() noexcept { request (Command::Reverse); }
    void requestUndo() noexcept    { request (Command::Undo); }
    void requestRedo() noexcept    { request (Command::Redo); }

    void setBpm (float bpm) noexcept;
    void setMaxLayerLimit (int layers) noexcept
    {
        maxLayerLimit_.store (juce::jlimit (1, 8, layers), std::memory_order_relaxed);
    }
    void setQuantizeEnabled (bool enabled) noexcept { quantizeEnabled_.store (enabled, std::memory_order_relaxed); }
    /** Beat multiples: 0.25=eighth, 0.5=quarter-note subdivision, 1=beat, 2=bar-ish. */
    void setQuantizeBeatMultiple (float beats) noexcept;

    /** In-place live monitor + loop output. No allocation or locking in this call. */
    void processBlock (juce::AudioBuffer<float>& buffer,
                       float loopLevel = 0.8f,
                       float overdubFeedback = 0.95f,
                       bool monitorInput = true) noexcept;

    // Published status snapshots are safe to read from the GUI thread.
    State getState() const noexcept { return publishedState_.load (std::memory_order_relaxed); }
    int getLoopLengthSamples() const noexcept { return publishedLoopLength_.load (std::memory_order_relaxed); }
    int getActiveLayerCount() const noexcept { return publishedLayerCount_.load (std::memory_order_relaxed); }
    int getMaxLoopSamples() const noexcept { return maxLoopSamples_; }
    int getMaxLayers() const noexcept { return (int) trackBuffers_.size(); }
    int getCrossfadeSamples() const noexcept { return crossfadeSamples_; }
    bool isReverse() const noexcept { return publishedReverse_.load (std::memory_order_relaxed); }

private:
    static constexpr uint32_t bit (Command command) noexcept { return (uint32_t) command; }

    void applyCommands (uint32_t commands) noexcept;
    void startNewRecording() noexcept;
    void requestFinalizeRecording (State destination) noexcept;
    void finalizeRecording (State destination) noexcept;
    void startOverdub() noexcept;
    void finishOverdub (State destination) noexcept;
    void clearLogicalState() noexcept;
    void advancePlayhead() noexcept;
    bool isGridBoundary() const noexcept;
    int samplesPerQuantizeGrid() const noexcept;
    float readTrackWithCrossfade (int layer, int channel, int position) const noexcept;
    float readTrackRawOrZero (int layer, int channel, int position) const noexcept;
    void writeRecordSample (int channel, int position, float input) noexcept;
    void writeOverdubSample (int channel, int position, float input, float feedback) noexcept;
    void commitCurrentOverdubIfNeeded() noexcept;
    void publishStatus() noexcept;

    double sampleRateHz_ = 44100.0;
    int maxChannels_ = 2;
    int maxLoopSamples_ = 0;
    int crossfadeSamples_ = 0;
    bool prepared_ = false;

    // Fixed storage. Each layer is a stereo buffer allocated in prepare().
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> trackBuffers_;
    // One generation stamp per [channel, sample] makes each new overdub layer
    // lazily zero-initialized without clearing an entire loop in one callback.
    std::vector<uint64_t> writeStamps_;
    std::vector<uint64_t> layerStampGeneration_;
    std::vector<float> fadeOutWeights_;
    std::vector<float> fadeInWeights_;
    uint64_t stampGeneration_ = 1;

    State state_ = State::Empty; // audio-thread-owned state
    std::atomic<State> publishedState_ { State::Empty };
    std::atomic<int> publishedLoopLength_ { 0 };
    std::atomic<int> publishedLayerCount_ { 0 };
    std::atomic<bool> publishedReverse_ { false };
    State pendingRecordDestination_ = State::Playing;
    std::atomic<uint32_t> pendingCommands_ { 0 };
    std::atomic<float> bpm_ { 120.0f };
    std::atomic<int> maxLayerLimit_ { 4 };
    std::atomic<float> quantizeBeatMultiple_ { 1.0f };
    std::atomic<bool> quantizeEnabled_ { false };

    int recordSamples_ = 0;
    int loopLengthSamples_ = 0;
    int playhead_ = 0;
    int activeLayerCount_ = 0;
    int committedLayerCount_ = 0;
    int overdubLayerIndex_ = -1;
    bool overdubWroteAny_ = false;
    bool pendingFinalizeRecording_ = false;
    bool reverse_ = false;
    uint64_t absoluteSampleClock_ = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopLevelSmoothed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoLooper)
};
} // namespace stringforge::dsp
