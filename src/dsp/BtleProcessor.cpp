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

    // The longest supported stutter/loss repeats one 20 ms packet for 1000
    // frames. The fixed 50 ms look-ahead still bounds forward reads.
    const auto maximumCorruptionSamples = static_cast<std::size_t>(
        std::ceil(sampleRate_ * static_cast<double>(maximumPacketMs) * 0.001)) * 1000;
    historySize_ = latencySamples_
        + std::max(static_cast<std::size_t>(std::ceil(sampleRate_ * 2.0)), maximumCorruptionSamples)
        + maximumBlockSize + 8;
    history_.assign(preparedChannels_, std::vector<float>(historySize_, 0.0f));
    frameReadBase_.assign(preparedChannels_, 0.0);
    previousFrameContinuation_.assign(preparedChannels_, 0.0);

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
    boundaryFadeSamples_ = 0;
    currentNominalStart_ = 0;
    lastGoodSourceStart_ = 0;
    frozenSourceStart_ = 0;
    stutterFramesRemaining_ = 0;
    dropoutFramesRemaining_ = 0;
    jitterFramesRemaining_ = 0;
    swapFramesRemaining_ = 0;
    swapDurationFrames_ = 0;
    swapOffsetFrames_ = 0;
    stereoLagFramesRemaining_ = 0;
    jitterOffsetFrames_ = 0;
    pendingSwapBack_ = false;
    hasPreviousFrame_ = false;
    frameMuted_ = false;
    previousFrameMuted_ = false;
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
    parameters_.burstLengthPackets = std::clamp(parameters_.burstLengthPackets, 1.0f, 1000.0f);
    parameters_.burstVariance = std::clamp(parameters_.burstVariance, 0.0f, 1.0f);
    parameters_.jitter = clampUnit(parameters_.jitter);
    parameters_.temporalSwap = clampUnit(parameters_.temporalSwap);
    parameters_.stutter = clampUnit(parameters_.stutter);
    parameters_.dropout = clampUnit(parameters_.dropout);
    parameters_.stereoSkew = clampUnit(parameters_.stereoSkew);
    parameters_.drift = clampUnit(parameters_.drift);
    parameters_.boundarySmoothing = clampUnit(parameters_.boundarySmoothing);
    parameters_.mix = clampUnit(parameters_.mix);
    parameters_.outputGain = std::clamp(parameters_.outputGain, 0.0f, 4.0f);

    const auto fullyHealthy = parameters_.quality >= 1.0f;
    if (fullyHealthy || parameters_.stutter <= 0.0f)
        stutterFramesRemaining_ = 0;
    if (fullyHealthy || parameters_.dropout <= 0.0f)
        dropoutFramesRemaining_ = 0;
    if (fullyHealthy || parameters_.jitter <= 0.0f)
    {
        jitterFramesRemaining_ = 0;
        jitterOffsetFrames_ = 0;
    }
    if (fullyHealthy || parameters_.temporalSwap <= 0.0f)
    {
        swapFramesRemaining_ = 0;
        swapDurationFrames_ = 0;
        swapOffsetFrames_ = 0;
        pendingSwapBack_ = false;
    }
    if (fullyHealthy || parameters_.stereoSkew <= 0.0f)
    {
        stereoLagFramesRemaining_ = 0;
        stereoLagSamples_ = 0.0;
    }
    if (fullyHealthy || parameters_.drift <= 0.0f)
    {
        driftOffset_ = 0.0;
        driftRate_ = 0.0;
    }

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

                if (boundaryFadeSamples_ > 1 && framePosition_ < boundaryFadeSamples_)
                {
                    auto previousWet = 0.0f;
                    if (!previousFrameMuted_)
                    {
                        const auto previousSource = previousFrameContinuation_[channel]
                            + static_cast<double>(framePosition_)
                            + driftOffset_;
                        previousWet = readHistory(channel, previousSource);
                    }

                    const auto progress = static_cast<double>(framePosition_)
                        / static_cast<double>(boundaryFadeSamples_ - 1);
                    const auto blend = static_cast<float>(0.5 - 0.5
                        * std::cos(3.14159265358979323846 * progress));
                    wet = previousWet + (wet - previousWet) * blend;
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
    const auto previousPacketSamples = currentPacketSamples_;
    const auto previousStereoLagSamples = stereoLagSamples_;
    previousFrameMuted_ = frameMuted_;
    if (hasPreviousFrame_)
    {
        for (std::size_t channel = 0; channel < previousFrameContinuation_.size(); ++channel)
        {
            previousFrameContinuation_[channel] = frameReadBase_[channel]
                + static_cast<double>(previousPacketSamples)
                - (channel == 1 ? previousStereoLagSamples : 0.0);
        }
    }

    currentPacketSamples_ = packetSamplesFromParameters();
    currentNominalStart_ = nominalStart;
    frameMuted_ = false;
    frameAction_ = FrameAction::normal;
    ++statistics_.frames;

    const auto damage = 1.0f - parameters_.quality;
    const auto damageSquared = damage * damage;
    const auto burstDensity = 0.25f + parameters_.burstiness * 1.15f;

    if (stereoLagFramesRemaining_ > 0)
        --stereoLagFramesRemaining_;
    else
        stereoLagSamples_ = 0.0;

    auto sourceStart = nominalStart;

    if (stutterFramesRemaining_ > 0)
    {
        sourceStart = frozenSourceStart_;
        --stutterFramesRemaining_;
        frameAction_ = FrameAction::stutter;
        ++statistics_.repeatedFrames;
    }
    else if (dropoutFramesRemaining_ > 0)
    {
        --dropoutFramesRemaining_;
        frameMuted_ = true;
        frameAction_ = FrameAction::dropout;
        ++statistics_.lostFrames;
        ++statistics_.mutedFrames;
    }
    else if (swapFramesRemaining_ > 0)
    {
        const auto returning = pendingSwapBack_;
        const auto swapOffset = static_cast<std::int64_t>(swapOffsetFrames_)
            * static_cast<std::int64_t>(currentPacketSamples_);
        sourceStart = returning ? nominalStart - swapOffset : nominalStart + swapOffset;
        --swapFramesRemaining_;
        if (swapFramesRemaining_ == 0)
        {
            if (returning)
                pendingSwapBack_ = false;
            else
            {
                pendingSwapBack_ = true;
                swapFramesRemaining_ = swapDurationFrames_;
            }
        }
        frameAction_ = returning ? FrameAction::swapBackward : FrameAction::swapForward;
        ++statistics_.swappedFrames;
    }
    else if (jitterFramesRemaining_ > 0)
    {
        sourceStart += static_cast<std::int64_t>(jitterOffsetFrames_)
            * static_cast<std::int64_t>(currentPacketSamples_);
        --jitterFramesRemaining_;
        frameAction_ = FrameAction::jitter;
        ++statistics_.jitteredFrames;
    }
    else if (stereoLagSamples_ > 0.0)
    {
        // Stereo desync delays the right channel while both channels continue
        // to advance. Do not start a packet-selection effect during this event.
        lastGoodSourceStart_ = nominalStart;
    }
    else
    {
        const auto eventRoll = randomUnit();
        const auto stutterChance = damageSquared * burstDensity * parameters_.stutter * 0.030f;
        const auto dropoutChance = damageSquared * burstDensity * parameters_.dropout * 0.060f;
        const auto swapChance = damageSquared * burstDensity * parameters_.temporalSwap * 0.025f;
        const auto jitterChance = damageSquared * burstDensity * parameters_.jitter * 0.080f;
        const auto stereoChance = preparedChannels_ > 1
            ? damageSquared * burstDensity * parameters_.stereoSkew * 0.200f
            : 0.0f;

        if (eventRoll < stutterChance)
        {
            frozenSourceStart_ = lastGoodSourceStart_;
            sourceStart = frozenSourceStart_;
            stutterFramesRemaining_ = corruptionLengthFrames() - 1;
            frameAction_ = FrameAction::stutter;
            ++statistics_.repeatedFrames;
        }
        else if (eventRoll < stutterChance + dropoutChance)
        {
            dropoutFramesRemaining_ = corruptionLengthFrames() - 1;
            frameMuted_ = true;
            frameAction_ = FrameAction::dropout;
            ++statistics_.lostFrames;
            ++statistics_.mutedFrames;
        }
        else if (eventRoll < stutterChance + dropoutChance + swapChance)
        {
            swapDurationFrames_ = corruptionLengthFrames();
            const auto maximumForwardFrames = std::max(1, static_cast<int>(latencySamples_
                / currentPacketSamples_) - 1);
            swapOffsetFrames_ = std::min(swapDurationFrames_,
                static_cast<std::size_t>(maximumForwardFrames));
            sourceStart = nominalStart + static_cast<std::int64_t>(swapOffsetFrames_)
                * static_cast<std::int64_t>(currentPacketSamples_);
            pendingSwapBack_ = false;
            swapFramesRemaining_ = swapDurationFrames_ - 1;
            if (swapFramesRemaining_ == 0)
            {
                pendingSwapBack_ = true;
                swapFramesRemaining_ = swapDurationFrames_;
            }
            frameAction_ = FrameAction::swapForward;
            ++statistics_.swappedFrames;
        }
        else if (eventRoll < stutterChance + dropoutChance + swapChance + jitterChance)
        {
            const auto maximumForwardFrames = std::max(1, static_cast<int>(latencySamples_
                / currentPacketSamples_) - 1);
            const auto maximumJitterFrames = std::min(2, maximumForwardFrames);
            auto frameOffset = randomInt(-2, maximumJitterFrames);
            if (frameOffset == 0)
                frameOffset = -1;
            sourceStart += static_cast<std::int64_t>(frameOffset)
                * static_cast<std::int64_t>(currentPacketSamples_);
            jitterOffsetFrames_ = frameOffset;
            jitterFramesRemaining_ = corruptionLengthFrames() - 1;
            frameAction_ = FrameAction::jitter;
            ++statistics_.jitteredFrames;
        }
        else if (eventRoll < stutterChance + dropoutChance + swapChance + jitterChance + stereoChance)
        {
            stereoLagSamples_ = std::max(1.0, randomUnit()
                * static_cast<double>(currentPacketSamples_));
            stereoLagFramesRemaining_ = corruptionLengthFrames() - 1;
            lastGoodSourceStart_ = nominalStart;
        }
        else
        {
            lastGoodSourceStart_ = nominalStart;
        }
    }

    if (preparedChannels_ > 1 && stereoLagSamples_ > 0.0)
        ++statistics_.stereoDelayedFrames;

    const auto maximumFutureStart = nominalStart
        + static_cast<std::int64_t>(latencySamples_)
        - static_cast<std::int64_t>(currentPacketSamples_) - 2;
    sourceStart = std::min(sourceStart, maximumFutureStart);

    for (auto& base : frameReadBase_)
        base = static_cast<double>(sourceStart);

    boundaryFadeSamples_ = 0;
    const auto maximumFadeSamples = currentPacketSamples_ / 2;
    if (hasPreviousFrame_ && parameters_.boundarySmoothing > 0.0f
        && maximumFadeSamples >= 2)
    {
        const auto requestedFadeSamples = static_cast<std::size_t>(std::llround(
            static_cast<double>(maximumFadeSamples) * parameters_.boundarySmoothing));
        boundaryFadeSamples_ = std::clamp<std::size_t>(requestedFadeSamples, 2,
                                                       maximumFadeSamples);
    }
    hasPreviousFrame_ = true;

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

std::size_t BtleProcessor::corruptionLengthFrames() noexcept
{
    const auto logSpread = std::log(2.0f) * parameters_.burstVariance;
    const auto multiplier = std::exp((randomUnit() * 2.0f - 1.0f) * logSpread);
    const auto frames = static_cast<std::size_t>(std::llround(
        static_cast<double>(parameters_.burstLengthPackets) * multiplier));
    return std::clamp<std::size_t>(frames, 1, 1000);
}

void BtleProcessor::updateSmoothedControls() noexcept
{
    smoothedMix_ += (parameters_.mix - smoothedMix_) * smoothingAmount_;
    smoothedGain_ += (parameters_.outputGain - smoothedGain_) * smoothingAmount_;
}

} // namespace saf::btle