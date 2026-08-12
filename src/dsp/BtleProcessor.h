#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace saf::btle
{

struct Parameters
{
    float quality = 0.65f;
    float packetSizeMs = 5.0f;
    float burstiness = 0.65f;
    float jitter = 0.35f;
    float temporalSwap = 0.20f;
    float stutter = 0.25f;
    float stereoSkew = 0.25f;
    float drift = 0.20f;
    float mix = 1.0f;
    float outputGain = 1.0f;
    std::uint32_t seed = 0x53414642u;
};

struct Statistics
{
    std::uint64_t frames = 0;
    std::uint64_t lostFrames = 0;
    std::uint64_t repeatedFrames = 0;
    std::uint64_t jitteredFrames = 0;
    std::uint64_t swappedFrames = 0;
    std::uint64_t mutedFrames = 0;
    std::uint64_t resyncs = 0;
};

class BtleProcessor
{
public:
    static constexpr double latencySeconds = 0.050;

    void prepare(double sampleRate, std::size_t maximumBlockSize, std::size_t channels);
    void reset();
    void setParameters(const Parameters& parameters) noexcept;

    // Processing is in-place and performs no allocation or locking.
    void process(float* const* channelData, std::size_t channels, std::size_t samples) noexcept;

    [[nodiscard]] std::size_t getLatencySamples() const noexcept;
    [[nodiscard]] const Statistics& getStatistics() const noexcept;

private:
    enum class FrameAction
    {
        normal,
        lossRepeat,
        lossMute,
        jitter,
        swapForward,
        swapBackward,
        stutter
    };

    void beginFrame(std::int64_t nominalStart) noexcept;
    [[nodiscard]] float readHistory(std::size_t channel, double sample) const noexcept;
    [[nodiscard]] std::uint32_t nextRandom() noexcept;
    [[nodiscard]] float randomUnit() noexcept;
    [[nodiscard]] int randomInt(int minimum, int maximum) noexcept;
    [[nodiscard]] std::size_t packetSamplesFromParameters() const noexcept;
    void updateSmoothedControls() noexcept;

    Parameters parameters_;
    Statistics statistics_;
    std::vector<std::vector<float>> history_;
    std::vector<double> frameReadBase_;

    double sampleRate_ = 48000.0;
    std::size_t historySize_ = 0;
    std::size_t latencySamples_ = 2400;
    std::size_t preparedChannels_ = 0;
    std::uint64_t absoluteSample_ = 0;

    std::size_t framePosition_ = 0;
    std::size_t currentPacketSamples_ = 240;
    std::int64_t currentNominalStart_ = 0;
    std::int64_t lastGoodSourceStart_ = 0;
    std::int64_t frozenSourceStart_ = 0;
    int stutterFramesRemaining_ = 0;
    bool pendingSwapBack_ = false;
    bool badChannelState_ = false;
    bool frameMuted_ = false;
    FrameAction frameAction_ = FrameAction::normal;

    double driftOffset_ = 0.0;
    double driftRate_ = 0.0;
    double stereoLagSamples_ = 0.0;
    float smoothedMix_ = 1.0f;
    float smoothedGain_ = 1.0f;
    float smoothingAmount_ = 0.0f;
    std::uint32_t randomState_ = 0x53414642u;
};

} // namespace saf::btle
