#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include <array>
#include <cmath>

SafBeetleAudioProcessor::SafBeetleAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SAF_BEETLE_PARAMETERS", createParameterLayout())
{
}

void SafBeetleAudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    btle_.prepare(sampleRate,
                  static_cast<std::size_t>(std::max(1, maximumExpectedSamplesPerBlock)),
                  static_cast<std::size_t>(std::max(1, getTotalNumOutputChannels())));
    btle_.setParameters(readParameters());
    btle_.reset();
    setLatencySamples(static_cast<int>(btle_.getLatencySamples()));
}

void SafBeetleAudioProcessor::releaseResources()
{
}

void SafBeetleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();

    for (auto channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    btle_.setParameters(readParameters());
    btle_.process(buffer.getArrayOfWritePointers(),
                  static_cast<std::size_t>(outputChannels),
                  static_cast<std::size_t>(buffer.getNumSamples()));
}

bool SafBeetleAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

juce::AudioProcessorEditor* SafBeetleAudioProcessor::createEditor()
{
    return new SafBeetleAudioProcessorEditor(*this);
}

bool SafBeetleAudioProcessor::hasEditor() const { return true; }
const juce::String SafBeetleAudioProcessor::getName() const { return JucePlugin_Name; }
bool SafBeetleAudioProcessor::acceptsMidi() const { return false; }
bool SafBeetleAudioProcessor::producesMidi() const { return false; }
bool SafBeetleAudioProcessor::isMidiEffect() const { return false; }
double SafBeetleAudioProcessor::getTailLengthSeconds() const { return saf::btle::BtleProcessor::latencySeconds; }
int SafBeetleAudioProcessor::getNumPrograms() { return 1; }
int SafBeetleAudioProcessor::getCurrentProgram() { return 0; }
void SafBeetleAudioProcessor::setCurrentProgram(int) {}
const juce::String SafBeetleAudioProcessor::getProgramName(int) { return {}; }
void SafBeetleAudioProcessor::changeProgramName(int, const juce::String&) {}

void SafBeetleAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    if (const auto state = parameters.copyState().createXml())
        copyXmlToBinary(*state, destinationData);
}

void SafBeetleAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto state = getXmlFromBinary(data, sizeInBytes))
        if (state->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*state));
}

juce::AudioProcessorValueTreeState::ParameterLayout SafBeetleAudioProcessor::createParameterLayout()
{
    using FloatParameter = juce::AudioParameterFloat;
    using ChoiceParameter = juce::AudioParameterChoice;
    using IntParameter = juce::AudioParameterInt;
    using ParameterID = juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<FloatParameter>(ParameterID{"quality", 1}, "Signal Quality",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 65.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<ChoiceParameter>(ParameterID{"packet", 1}, "Packet Size",
        juce::StringArray{"2.5 ms", "5 ms", "10 ms", "20 ms"}, 1));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"smooth", 1}, "Boundary Smooth",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"burst", 1}, "Burstiness",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 65.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"burstlen", 1}, "Burst Length",
        juce::NormalisableRange<float>{1.0f, 1000.0f, 1.0f, 0.25f}, 8.0f,
        juce::AudioParameterFloatAttributes{}.withLabel(" packets")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"burstvar", 1}, "Burst Variance",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 15.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"jitter", 1}, "Jitter",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 35.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"swap", 1}, "Temporal Swap",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 20.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"stutter", 1}, "Stutter",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 25.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"stereo", 1}, "Stereo Desync",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 25.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"drift", 1}, "Clock Drift",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 20.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 100.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));
    layout.add(std::make_unique<FloatParameter>(ParameterID{"output", 1}, "Output",
        juce::NormalisableRange<float>{-24.0f, 6.0f, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel(" dB")));
    layout.add(std::make_unique<IntParameter>(ParameterID{"seed", 1}, "Pattern Seed", 1, 9999, 812));
    return layout;
}

saf::btle::Parameters SafBeetleAudioProcessor::readParameters() const noexcept
{
    const auto value = [this](const char* id)
    {
        return parameters.getRawParameterValue(id)->load(std::memory_order_relaxed);
    };

    static constexpr std::array<float, 4> packetSizes {2.5f, 5.0f, 10.0f, 20.0f};
    const auto packetIndex = juce::jlimit(0, 3, static_cast<int>(std::lround(value("packet"))));

    saf::btle::Parameters result;
    result.quality = value("quality") * 0.01f;
    result.packetSizeMs = packetSizes[static_cast<std::size_t>(packetIndex)];
    result.burstiness = value("burst") * 0.01f;
    result.burstLengthPackets = value("burstlen");
    result.burstVariance = value("burstvar") * 0.01f;
    result.jitter = value("jitter") * 0.01f;
    result.temporalSwap = value("swap") * 0.01f;
    result.stutter = value("stutter") * 0.01f;
    result.stereoSkew = value("stereo") * 0.01f;
    result.drift = value("drift") * 0.01f;
    result.boundarySmoothing = value("smooth") * 0.01f;
    result.mix = value("mix") * 0.01f;
    result.outputGain = juce::Decibels::decibelsToGain(value("output"));
    result.seed = static_cast<std::uint32_t>(std::lround(value("seed")));
    return result;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SafBeetleAudioProcessor();
}
