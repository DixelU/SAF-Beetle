#include "dsp/BtleProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::vector<float> makeSignal(std::size_t samples)
{
    std::vector<float> signal(samples);
    for (std::size_t index = 0; index < samples; ++index)
    {
        const auto time = static_cast<double>(index) / sampleRate;
        signal[index] = static_cast<float>(0.55 * std::sin(2.0 * 3.141592653589793 * 223.0 * time)
            + 0.20 * std::sin(2.0 * 3.141592653589793 * 997.0 * time));
    }
    return signal;
}

std::vector<float> render(const std::vector<float>& input,
                          const saf::btle::Parameters& parameters,
                          std::size_t blockSize)
{
    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, blockSize, 1);
    processor.setParameters(parameters);
    processor.reset();

    auto output = input;
    for (std::size_t offset = 0; offset < output.size(); offset += blockSize)
    {
        const auto count = std::min(blockSize, output.size() - offset);
        auto* channel = output.data() + offset;
        processor.process(&channel, 1, count);
    }
    return output;
}

float maximumPacketBoundaryStep(const std::vector<float>& audio,
                                std::size_t latency,
                                std::size_t packetSamples)
{
    auto maximum = 0.0f;
    for (auto boundary = latency + packetSamples;
         boundary < audio.size(); boundary += packetSamples)
        maximum = std::max(maximum, std::abs(audio[boundary] - audio[boundary - 1]));
    return maximum;
}

saf::btle::Parameters isolatedParameters()
{
    saf::btle::Parameters parameters;
    parameters.quality = 0.0f;
    parameters.packetSizeMs = 5.0f;
    parameters.burstiness = 1.0f;
    parameters.burstLengthPackets = 6.0f;
    parameters.burstVariance = 0.0f;
    parameters.jitter = 0.0f;
    parameters.temporalSwap = 0.0f;
    parameters.stutter = 0.0f;
    parameters.stereoSkew = 0.0f;
    parameters.drift = 0.0f;
    parameters.mix = 1.0f;
    parameters.seed = 812u;
    return parameters;
}

saf::btle::Statistics collectStatistics(const saf::btle::Parameters& parameters,
                                        double seconds,
                                        std::size_t channels = 2)
{
    constexpr std::size_t blockSize = 257;
    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, blockSize, channels);
    processor.setParameters(parameters);
    processor.reset();

    std::array<std::vector<float>, 2> audio {
        std::vector<float>(blockSize), std::vector<float>(blockSize)};
    const auto totalSamples = static_cast<std::size_t>(sampleRate * seconds);
    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = std::min(blockSize, totalSamples - offset);
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const auto time = static_cast<double>(offset + sample) / sampleRate;
            audio[0][sample] = static_cast<float>(0.6 * std::sin(2.0 * 3.141592653589793
                                                                 * 223.0 * time));
            audio[1][sample] = static_cast<float>(0.6 * std::sin(2.0 * 3.141592653589793
                                                                 * 331.0 * time));
        }

        std::array<float*, 2> data {audio[0].data(), audio[1].data()};
        processor.process(data.data(), channels, count);
    }

    return processor.getStatistics();
}

void requireNoUnselectedPacketEffects(const saf::btle::Statistics& statistics,
                                      std::string_view mode)
{
    require(statistics.lostFrames == 0, "solo mode must not trigger hidden packet loss");
    require(statistics.mutedFrames == 0, "solo mode must not trigger hidden packet muting");
    if (mode != "stutter")
        require(statistics.repeatedFrames == 0, "solo mode must not trigger stutter/repetition");
    if (mode != "jitter")
        require(statistics.jitteredFrames == 0, "solo mode must not trigger jitter");
    if (mode != "swap")
        require(statistics.swappedFrames == 0, "solo mode must not trigger temporal swaps");
    if (mode != "stereo")
        require(statistics.stereoDelayedFrames == 0, "solo mode must not trigger stereo desync");
    if (mode != "drift")
        require(statistics.resyncs == 0, "solo mode must not trigger clock drift");
}

void testCleanPathIsExactlyDelayed()
{
    saf::btle::Parameters parameters;
    parameters.quality = 1.0f;
    parameters.mix = 1.0f;
    parameters.drift = 1.0f;
    parameters.boundarySmoothing = 1.0f;

    const auto input = makeSignal(24000);
    const auto output = render(input, parameters, 127);
    const auto latency = static_cast<std::size_t>(sampleRate * saf::btle::BtleProcessor::latencySeconds);

    for (std::size_t index = 0; index < latency; ++index)
        require(output[index] == 0.0f, "latency pre-roll must be silent");

    for (std::size_t index = latency; index < output.size(); ++index)
        require(std::abs(output[index] - input[index - latency]) < 1.0e-6f,
                "quality 100% must produce a transparent delayed signal");
}

void testRenderingIsIndependentOfHostBlockSize()
{
    saf::btle::Parameters parameters;
    parameters.quality = 0.18f;
    parameters.packetSizeMs = 5.0f;
    parameters.burstiness = 0.82f;
    parameters.jitter = 0.9f;
    parameters.temporalSwap = 0.8f;
    parameters.stutter = 0.75f;
    parameters.stereoSkew = 0.7f;
    parameters.drift = 0.6f;
    parameters.boundarySmoothing = 0.73f;
    parameters.seed = 123456u;

    const auto input = makeSignal(96000);
    const auto first = render(input, parameters, 64);
    const auto second = render(input, parameters, 511);
    require(first == second, "the same seed must render identically at different host block sizes");
}

void testDamagedPathActuallyChangesAudio()
{
    saf::btle::Parameters cleanParameters;
    cleanParameters.quality = 1.0f;
    cleanParameters.seed = 42u;

    auto damagedParameters = cleanParameters;
    damagedParameters.quality = 0.0f;
    damagedParameters.burstiness = 0.95f;
    damagedParameters.jitter = 1.0f;
    damagedParameters.temporalSwap = 1.0f;
    damagedParameters.stutter = 1.0f;
    damagedParameters.drift = 1.0f;

    const auto input = makeSignal(96000);
    const auto clean = render(input, cleanParameters, 256);
    const auto damaged = render(input, damagedParameters, 256);

    std::size_t changed = 0;
    for (std::size_t index = 0; index < damaged.size(); ++index)
    {
        require(std::isfinite(damaged[index]), "damaged samples must remain finite");
        if (std::abs(clean[index] - damaged[index]) > 1.0e-4f)
            ++changed;
    }
    require(changed > input.size() / 10, "maximum damage must materially change the signal");
}

void testDriftResyncDoesNotRetriggerPerSample()
{
    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, 256, 1);

    saf::btle::Parameters parameters;
    parameters.quality = 0.0f;
    parameters.drift = 1.0f;
    processor.setParameters(parameters);
    processor.reset();

    auto audio = makeSignal(static_cast<std::size_t>(sampleRate * 10.0));
    auto* channel = audio.data();
    processor.process(&channel, 1, audio.size());

    const auto& statistics = processor.getStatistics();
    require(statistics.resyncs > 0, "maximum drift must eventually resynchronise");
    require(statistics.resyncs < statistics.frames,
            "one clock correction must not retrigger on each audio sample");
}

void testBoundarySmoothingReducesPacketClicks()
{
    auto parameters = isolatedParameters();
    parameters.stutter = 1.0f;
    parameters.burstLengthPackets = 6.0f;
    parameters.seed = 812u;

    const auto input = makeSignal(static_cast<std::size_t>(sampleRate * 4.0));
    parameters.boundarySmoothing = 0.0f;
    const auto hardBoundaries = render(input, parameters, 257);
    parameters.boundarySmoothing = 1.0f;
    const auto smoothBoundaries = render(input, parameters, 257);

    const auto latency = static_cast<std::size_t>(sampleRate
        * saf::btle::BtleProcessor::latencySeconds);
    const auto packetSamples = static_cast<std::size_t>(std::llround(
        sampleRate * static_cast<double>(parameters.packetSizeMs) * 0.001));
    const auto hardStep = maximumPacketBoundaryStep(hardBoundaries, latency, packetSamples);
    const auto smoothStep = maximumPacketBoundaryStep(smoothBoundaries, latency, packetSamples);

    require(hardStep > 0.25f, "the deterministic stutter render must contain a hard packet click");
    require(smoothStep < hardStep * 0.35f,
            "maximum smoothing must materially reduce packet-boundary discontinuities");
}

void testEachCorruptionControlIsIsolated()
{
    auto parameters = isolatedParameters();
    parameters.jitter = 1.0f;
    auto statistics = collectStatistics(parameters, 3.0);
    require(statistics.jitteredFrames > 0, "solo jitter must produce jitter events");
    requireNoUnselectedPacketEffects(statistics, "jitter");

    parameters = isolatedParameters();
    parameters.temporalSwap = 1.0f;
    statistics = collectStatistics(parameters, 3.0);
    require(statistics.swappedFrames > 0, "solo temporal swap must produce swap events");
    requireNoUnselectedPacketEffects(statistics, "swap");

    parameters = isolatedParameters();
    parameters.stutter = 1.0f;
    statistics = collectStatistics(parameters, 3.0);
    require(statistics.repeatedFrames > 0, "solo stutter must produce repeated frames");
    requireNoUnselectedPacketEffects(statistics, "stutter");

    parameters = isolatedParameters();
    parameters.stereoSkew = 1.0f;
    statistics = collectStatistics(parameters, 3.0);
    require(statistics.stereoDelayedFrames > 0, "solo stereo desync must delay the right channel");
    requireNoUnselectedPacketEffects(statistics, "stereo");

    parameters = isolatedParameters();
    parameters.drift = 1.0f;
    statistics = collectStatistics(parameters, 4.0);
    require(statistics.resyncs > 0, "solo clock drift must eventually resynchronise");
    requireNoUnselectedPacketEffects(statistics, "drift");
}

void testZeroedKnobCancelsLongRunningEvent()
{
    constexpr std::size_t blockSize = 240;
    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, blockSize, 2);

    auto parameters = isolatedParameters();
    parameters.stutter = 1.0f;
    parameters.burstLengthPackets = 1000.0f;
    processor.setParameters(parameters);
    processor.reset();

    std::array<std::vector<float>, 2> audio {
        makeSignal(static_cast<std::size_t>(sampleRate * 2.0)),
        makeSignal(static_cast<std::size_t>(sampleRate * 2.0))};
    std::array<float*, 2> data {audio[0].data(), audio[1].data()};
    processor.process(data.data(), 2, audio[0].size());
    require(processor.getStatistics().repeatedFrames > 0,
            "long stutter must be active before testing cancellation");

    parameters.stutter = 0.0f;
    parameters.stereoSkew = 1.0f;
    processor.setParameters(parameters);
    const auto repeatedBefore = processor.getStatistics().repeatedFrames;
    const auto stereoBefore = processor.getStatistics().stereoDelayedFrames;

    audio[0] = makeSignal(static_cast<std::size_t>(sampleRate * 2.0));
    audio[1] = makeSignal(static_cast<std::size_t>(sampleRate * 2.0));
    data = {audio[0].data(), audio[1].data()};
    processor.process(data.data(), 2, audio[0].size());

    require(processor.getStatistics().repeatedFrames == repeatedBefore,
            "setting Stutter to zero must cancel its active burst");
    require(processor.getStatistics().stereoDelayedFrames > stereoBefore,
            "Stereo Desync must take over after Stutter is disabled");
}

void testStereoDesyncKeepsPacketsAdvancing()
{
    constexpr std::size_t blockSize = 256;
    auto parameters = isolatedParameters();
    parameters.quality = 0.782f;
    parameters.packetSizeMs = 20.0f;
    parameters.burstiness = 0.65f;
    parameters.burstLengthPackets = 71.0f;
    parameters.burstVariance = 0.382f;
    parameters.stereoSkew = 0.374f;

    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, blockSize, 2);
    processor.setParameters(parameters);
    processor.reset();

    std::array<std::vector<float>, 2> audio {
        makeSignal(static_cast<std::size_t>(sampleRate * 10.0)),
        makeSignal(static_cast<std::size_t>(sampleRate * 10.0))};
    for (std::size_t offset = 0; offset < audio[0].size(); offset += blockSize)
    {
        const auto count = std::min(blockSize, audio[0].size() - offset);
        std::array<float*, 2> data {audio[0].data() + offset, audio[1].data() + offset};
        processor.process(data.data(), 2, count);
    }

    require(processor.getStatistics().stereoDelayedFrames > 0,
            "the reported Stereo Desync settings must create delayed frames");
    require(processor.getStatistics().repeatedFrames == 0,
            "Stereo Desync must never select the same packet repeatedly");

    const auto latency = processor.getLatencySamples();
    const auto packetSamples = static_cast<std::size_t>(sampleRate * 0.020);
    std::size_t identicalAdjacentPackets = 0;
    for (auto start = latency * 2 + packetSamples;
         start + packetSamples <= audio[1].size(); start += packetSamples)
    {
        if (std::equal(audio[1].begin() + static_cast<std::ptrdiff_t>(start),
                       audio[1].begin() + static_cast<std::ptrdiff_t>(start + packetSamples),
                       audio[1].begin() + static_cast<std::ptrdiff_t>(start - packetSamples)))
            ++identicalAdjacentPackets;
    }
    require(identicalAdjacentPackets == 0,
            "Stereo Desync output must keep advancing instead of replaying one packet");
}
} // namespace

int main()
{
    testCleanPathIsExactlyDelayed();
    testRenderingIsIndependentOfHostBlockSize();
    testDamagedPathActuallyChangesAudio();
    testDriftResyncDoesNotRetriggerPerSample();
    testBoundarySmoothingReducesPacketClicks();
    testEachCorruptionControlIsIsolated();
    testZeroedKnobCancelsLongRunningEvent();
    testStereoDesyncKeepsPacketsAdvancing();
    std::cout << "All SAF BTLE DSP tests passed.\n";
    return EXIT_SUCCESS;
}
