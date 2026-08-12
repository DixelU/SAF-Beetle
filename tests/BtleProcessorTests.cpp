#include "dsp/BtleProcessor.h"

#include <algorithm>
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

void testCleanPathIsExactlyDelayed()
{
    saf::btle::Parameters parameters;
    parameters.quality = 1.0f;
    parameters.mix = 1.0f;
    parameters.drift = 1.0f;

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
} // namespace

int main()
{
    testCleanPathIsExactlyDelayed();
    testRenderingIsIndependentOfHostBlockSize();
    testDamagedPathActuallyChangesAudio();
    testDriftResyncDoesNotRetriggerPerSample();
    std::cout << "All SAF BTLE DSP tests passed.\n";
    return EXIT_SUCCESS;
}
