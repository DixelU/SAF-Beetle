#include "dsp/BtleProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr std::uint32_t sampleRate = 48000;
constexpr std::uint16_t channels = 2;
constexpr double pi = 3.14159265358979323846;

template <typename Integer>
void writeLittleEndian(std::ofstream& stream, Integer value)
{
    for (std::size_t byte = 0; byte < sizeof(Integer); ++byte)
        stream.put(static_cast<char>((value >> (byte * 8u)) & 0xffu));
}

void writeWave(const std::string& path,
               const std::vector<float>& left,
               const std::vector<float>& right)
{
    const auto frames = std::min(left.size(), right.size());
    const auto dataBytes = static_cast<std::uint32_t>(frames * channels * sizeof(std::int16_t));
    std::ofstream stream(path, std::ios::binary);
    stream.write("RIFF", 4);
    writeLittleEndian(stream, 36u + dataBytes);
    stream.write("WAVEfmt ", 8);
    writeLittleEndian(stream, 16u);
    writeLittleEndian(stream, static_cast<std::uint16_t>(1));
    writeLittleEndian(stream, channels);
    writeLittleEndian(stream, sampleRate);
    writeLittleEndian(stream, sampleRate * channels * static_cast<std::uint32_t>(sizeof(std::int16_t)));
    writeLittleEndian(stream, static_cast<std::uint16_t>(channels * sizeof(std::int16_t)));
    writeLittleEndian(stream, static_cast<std::uint16_t>(16));
    stream.write("data", 4);
    writeLittleEndian(stream, dataBytes);

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        for (const auto sample : {left[frame], right[frame]})
        {
            const auto clipped = std::clamp(sample, -1.0f, 1.0f);
            const auto integer = static_cast<std::int16_t>(std::lrint(clipped * 32767.0f));
            writeLittleEndian(stream, static_cast<std::uint16_t>(integer));
        }
    }
}

float synthSample(std::size_t index, bool right)
{
    constexpr std::array<double, 8> notes {110.0, 138.59, 164.81, 220.0,
                                           98.0, 123.47, 164.81, 196.0};
    const auto time = static_cast<double>(index) / sampleRate;
    const auto step = static_cast<std::size_t>(time / 0.5) % notes.size();
    const auto noteTime = std::fmod(time, 0.5);
    const auto envelope = std::exp(-noteTime * 2.8);
    const auto frequency = notes[step] * (right ? 1.002 : 1.0);
    const auto bass = 0.34 * envelope * std::sin(2.0 * pi * frequency * time);
    const auto overtone = 0.16 * envelope * std::sin(2.0 * pi * frequency * 2.01 * time);

    const auto beatTime = std::fmod(time, 0.5);
    const auto kickEnvelope = std::exp(-beatTime * 24.0);
    const auto kickFrequency = 48.0 + 70.0 * std::exp(-beatTime * 35.0);
    const auto kick = 0.42 * kickEnvelope * std::sin(2.0 * pi * kickFrequency * time);

    const auto tick = std::fmod(time + (right ? 0.0007 : 0.0), 0.125);
    const auto hat = tick < 0.008
        ? 0.08 * (1.0 - tick / 0.008) * std::sin(2.0 * pi * 7300.0 * time) : 0.0;
    return static_cast<float>(bass + overtone + kick + hat);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string outputPath = argc > 1 ? argv[1] : "saf_beetle_demo.wav";
    constexpr std::size_t durationSamples = sampleRate * 10u;
    constexpr std::size_t blockSize = 256;

    std::vector<float> left(durationSamples);
    std::vector<float> right(durationSamples);
    for (std::size_t index = 0; index < durationSamples; ++index)
    {
        left[index] = synthSample(index, false);
        right[index] = synthSample(index, true);
    }

    saf::btle::BtleProcessor processor;
    processor.prepare(sampleRate, blockSize, channels);

    saf::btle::Parameters parameters;
    parameters.seed = 20260812u;
    parameters.packetSizeMs = 5.0f;
    parameters.burstiness = 0.82f;
    parameters.jitter = 0.85f;
    parameters.temporalSwap = 0.70f;
    parameters.stutter = 0.72f;
    parameters.stereoSkew = 0.65f;
    parameters.drift = 0.55f;
    parameters.mix = 0.92f;

    for (std::size_t offset = 0; offset < durationSamples; offset += blockSize)
    {
        const auto seconds = static_cast<double>(offset) / sampleRate;
        parameters.quality = seconds < 2.5 ? 0.98f
            : seconds < 5.0 ? 0.60f
            : seconds < 7.5 ? 0.30f : 0.08f;
        processor.setParameters(parameters);

        const auto count = std::min(blockSize, durationSamples - offset);
        std::array<float*, channels> data {left.data() + offset, right.data() + offset};
        processor.process(data.data(), channels, count);
    }

    writeWave(outputPath, left, right);
    const auto& stats = processor.getStatistics();
    std::cout << "Rendered " << outputPath << '\n'
              << "frames=" << stats.frames
              << " repeated=" << stats.repeatedFrames
              << " jittered=" << stats.jitteredFrames
              << " swapped=" << stats.swappedFrames
              << " stereo-delayed=" << stats.stereoDelayedFrames
              << " resyncs=" << stats.resyncs << '\n';
    return 0;
}
