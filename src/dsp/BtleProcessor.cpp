#include "dsp/BtleProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace saf::btle
{
namespace
{
constexpr float minimumPacketMs = 2.5f;
constexpr float maximumPacketMs = 20.0f;

float clampUnit(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}
} // namespace

void BtleProcessor::prepare(double sampleRate, std::size_t maximumBlockSize, std::size_t channels)
{
    sampleRate_ = std::clamp(sampleRate, 8000.0, 384000.0);
    preparedChannels_ = std::clamp<std::size_t>(channels, 1, 2);
    latencySamples_ = static_cast<std::size_t>(std::llround(sampleRate_ * latencySeconds));

    // Two seconds of history leaves ample room for long frozen fragments while
    // the fixed 50 ms look-ahead covers forward jitter and packet swaps.
    historySize_ = latencySamples_
        + static_cast<std::size_t>(std::ceil(sampleRate_ * 2.0))
        + maximumBlockSize + 8;
    history_.assign(preparedChannels_, std::vector<float>(historySize_, 0.0f));
    frameReadBase_.assign(preparedChannels_, 0.0);

    const auto smoothingSamples = std::max(1.0, sampleRate_ * 0.020);
    smoothingAmount_ = static_cast<float>(1.0 - std::exp(-1.0 / smoothingSamples));
    reset();
}

void BtleProcessor::reset()
{
    for (auto& channel : history_)
        std::fill(channel.begin(), channel.end(), 0.0f);

    statistics_ = {};
    absoluteSample_ = 0;
    framePosition_ = 0;
    currentPacketSamples_ = packetSamplesFromParameters();
    currentNominalStart_ = 0;
    lastGoodSourceStart_ = 0;
    frozenSourceStart_ = 0;
    stutterFramesRemaining_ = 0;
    pendingSwapBack_ = false;
    badChannelState_ = false;
    frameMuted_ = false;
    frameAction_ = FrameAction::normal;
    driftOffset_ = 0.0;
    stereoLagSamples_ = 0.0;
    smoothedMix_ = clampUnit(parameters_.mix);
    smoothedGain_ = std::clamp(parameters_.outputGain, 0.0f, 4.0f);
    randomState_ = parameters_.seed == 0 ? 0x53414642u : parameters_.seed;
    driftRate_ = (randomUnit() < 0.5f ? -1.0 : 1.0)
        * static_cast<double>(clampUnit(parameters_.drift)) * 0.0015;
}

void BtleProcessor::setParameters(const Parameters& parameters) noexcept
{
    const auto oldSeed = parameters_.seed;
    parameters_ = parameters;
    parameters_.quality = clampUnit(parameters_.quality);
    parameters_.packetSizeMs = std::clamp(parameters_.packetSizeMs, minimumPacketMs, maximumPacketMs);
    parameters_.burstiness = clampUnit(parameters_.burstiness);
    parameters_.jitter = clampUnit(parameters_.jitter);
    parameters_.temporalSwap = clampUnit(parameters_.temporalSwap);
    parameters_.stutter = clampUnit(parameters_.stutter);
    parameters_.stereoSkew = clampUnit(parameters_.stereoSkew);
    parameters_.drift = clampUnit(parameters_.drift);
    parameters_.mix = clampUnit(parameters_.mix);
    parameters_.outputGain = std::clamp(parameters_.outputGain, 0.0f, 4.0f);

    if (parameters_.seed != oldSeed)
        randomState_ = parameters_.seed == 0 ? 0x53414642u : parameters_.seed;
}

void BtleProcessor::process(float* const* channelData,
                            std::size_t channels,
                            std::size_t samples) noexcept
{
    if (historySize_ == 0 || channelData == nullptr || channels == 0)
        return;

    channels = std::min(channels, preparedChannels_);

    for (std::size_t sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        const auto writeIndex = static_cast<std::size_t>(absoluteSample_ % historySize_);
        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            const auto input = std::isfinite(channelData[channel][sampleIndex])
                ? channelData[channel][sampleIndex] : 0.0f;
            history_[channel][writeIndex] = input;
        }

        const auto nominalSample = static_cast<std::int64_t>(absoluteSample_)
            - static_cast<std::int64_t>(latencySamples_);

        if (nominalSample < 0)
        {
            for (std::size_t channel = 0; channel < channels; ++channel)
                channelData[channel][sampleIndex] = 0.0f;
        }
        else
        {
            if (framePosition_ == 0)
                beginFrame(nominalSample);

            updateSmoothedControls();
            const auto drySample = static_cast<double>(nominalSample);

            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                const auto dry = readHistory(channel, drySample);
                auto wet = 0.0f;

                if (!frameMuted_)
                {
                    auto source = frameReadBase_[channel]
                        + static_cast<double>(framePosition_)
                        + driftOffset_;
                    if (channel == 1)
                        source -= stereoLagSamples_;
                    wet = readHistory(channel, source);
                }

                const auto mixed = dry + (wet - dry) * smoothedMix_;
                channelData[channel][sampleIndex] = mixed * smoothedGain_;
            }

            const auto damage = 1.0 - static_cast<double>(parameters_.quality);
            driftOffset_ += driftRate_ * damage;
            const auto driftLimit = static_cast<double>(currentPacketSamples_) * 0.5;
            if (parameters_.drift > 0.0f && std::abs(driftOffset_) >= driftLimit)
            {
                // A receiver correction is a single discontinuity. Resetting to
                // the opposite boundary would retrigger on every following sample.
                driftOffset_ = 0.0;
                driftRate_ = -driftRate_;
                ++statistics_.resyncs;
            }

            ++framePosition_;
            if (framePosition_ >= currentPacketSamples_)
                framePosition_ = 0;
        }

        ++absoluteSample_;
    }
}

std::size_t BtleProcessor::getLatencySamples() const noexcept
{
    return latencySamples_;
}

const Statistics& BtleProcessor::getStatistics() const noexcept
{
    return statistics_;
}

void BtleProcessor::beginFrame(std::int64_t nominalStart) noexcept
{
    currentPacketSamples_ = packetSamplesFromParameters();
    currentNominalStart_ = nominalStart;
    frameMuted_ = false;
    stereoLagSamples_ = 0.0;
    frameAction_ = FrameAction::normal;
    ++statistics_.frames;

    const auto damage = 1.0f - parameters_.quality;
    const auto damageSquared = damage * damage;
    const auto enterBadChance = damageSquared * 0.08f;
    const auto remainBadChance = 0.08f + parameters_.burstiness * 0.90f;

    if (badChannelState_)
        badChannelState_ = randomUnit() < remainBadChance;
    else
        badChannelState_ = randomUnit() < enterBadChance;

    auto sourceStart = nominalStart;

    if (stutterFramesRemaining_ > 0)
    {
        sourceStart = frozenSourceStart_;
        --stutterFramesRemaining_;
        frameAction_ = FrameAction::stutter;
        ++statistics_.repeatedFrames;
    }
    else if (badChannelState_)
    {
        ++statistics_.lostFrames;
        if (randomUnit() < 0.12f + damage * 0.28f)
        {
            frameMuted_ = true;
            frameAction_ = FrameAction::lossMute;
            ++statistics_.mutedFrames;
        }
        else
        {
            sourceStart = lastGoodSourceStart_;
            frameAction_ = FrameAction::lossRepeat;
            ++statistics_.repeatedFrames;
        }
    }
    else if (pendingSwapBack_)
    {
        sourceStart = nominalStart - static_cast<std::int64_t>(currentPacketSamples_);
        pendingSwapBack_ = false;
        frameAction_ = FrameAction::swapBackward;
        ++statistics_.swappedFrames;
    }
    else
    {
        const auto eventRoll = randomUnit();
        const auto stutterChance = damageSquared * parameters_.stutter * 0.030f;
        const auto swapChance = damageSquared * parameters_.temporalSwap * 0.025f;
        const auto jitterChance = damageSquared * parameters_.jitter * 0.080f;

        if (eventRoll < stutterChance)
        {
            frozenSourceStart_ = lastGoodSourceStart_;
            sourceStart = frozenSourceStart_;
            stutterFramesRemaining_ = randomInt(1, 7);
            frameAction_ = FrameAction::stutter;
            ++statistics_.repeatedFrames;
        }
        else if (eventRoll < stutterChance + swapChance)
        {
            sourceStart = nominalStart + static_cast<std::int64_t>(currentPacketSamples_);
            pendingSwapBack_ = true;
            frameAction_ = FrameAction::swapForward;
            ++statistics_.swappedFrames;
        }
        else if (eventRoll < stutterChance + swapChance + jitterChance)
        {
            const auto maximumForwardFrames = std::max(1, static_cast<int>(latencySamples_
                / currentPacketSamples_) - 1);
            const auto maximumJitterFrames = std::min(2, maximumForwardFrames);
            auto frameOffset = randomInt(-2, maximumJitterFrames);
            if (frameOffset == 0)
                frameOffset = -1;
            sourceStart += static_cast<std::int64_t>(frameOffset)
                * static_cast<std::int64_t>(currentPacketSamples_);
            frameAction_ = FrameAction::jitter;
            ++statistics_.jitteredFrames;
        }
        else
        {
            lastGoodSourceStart_ = nominalStart;
        }
    }

    if (preparedChannels_ > 1
        && randomUnit() < damageSquared * parameters_.stereoSkew * 0.20f)
    {
        stereoLagSamples_ = randomUnit() * static_cast<double>(currentPacketSamples_);
    }

    const auto maximumFutureStart = nominalStart
        + static_cast<std::int64_t>(latencySamples_)
        - static_cast<std::int64_t>(currentPacketSamples_) - 2;
    sourceStart = std::min(sourceStart, maximumFutureStart);

    for (auto& base : frameReadBase_)
        base = static_cast<double>(sourceStart);

    const auto driftMagnitude = static_cast<double>(parameters_.drift) * 0.0015;
    if (std::abs(driftRate_) < std::numeric_limits<double>::epsilon())
        driftRate_ = (randomUnit() < 0.5f ? -1.0 : 1.0) * driftMagnitude;
    else
        driftRate_ = std::copysign(driftMagnitude, driftRate_);
}

float BtleProcessor::readHistory(std::size_t channel, double sample) const noexcept
{
    if (channel >= history_.size() || sample < 0.0)
        return 0.0f;

    const auto first = static_cast<std::int64_t>(std::floor(sample));
    const auto second = first + 1;
    const auto newest = static_cast<std::int64_t>(absoluteSample_);
    const auto oldest = newest >= static_cast<std::int64_t>(historySize_)
        ? newest - static_cast<std::int64_t>(historySize_) + 1 : 0;

    if (first < oldest || second > newest)
        return 0.0f;

    const auto fraction = static_cast<float>(sample - static_cast<double>(first));
    const auto firstValue = history_[channel][static_cast<std::size_t>(first) % historySize_];
    const auto secondValue = history_[channel][static_cast<std::size_t>(second) % historySize_];
    return firstValue + (secondValue - firstValue) * fraction;
}

std::uint32_t BtleProcessor::nextRandom() noexcept
{
    auto value = randomState_;
    if (value == 0)
        value = 0x53414642u;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    randomState_ = value;
    return value;
}

float BtleProcessor::randomUnit() noexcept
{
    return static_cast<float>(nextRandom() >> 8u) * (1.0f / 16777216.0f);
}

int BtleProcessor::randomInt(int minimum, int maximum) noexcept
{
    if (maximum <= minimum)
        return minimum;
    const auto range = static_cast<std::uint32_t>(maximum - minimum + 1);
    return minimum + static_cast<int>(nextRandom() % range);
}

std::size_t BtleProcessor::packetSamplesFromParameters() const noexcept
{
    const auto samples = sampleRate_ * static_cast<double>(parameters_.packetSizeMs) * 0.001;
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(samples)));
}

void BtleProcessor::updateSmoothedControls() noexcept
{
    smoothedMix_ += (parameters_.mix - smoothedMix_) * smoothingAmount_;
    smoothedGain_ += (parameters_.outputGain - smoothedGain_) * smoothingAmount_;
}

} // namespace saf::btle
