#pragma once

#include "dsp/BtleProcessor.h"

#include <JuceHeader.h>

class SafBeetleAudioProcessor final : public juce::AudioProcessor
{
public:
    SafBeetleAudioProcessor();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    [[nodiscard]] bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override;
    [[nodiscard]] const juce::String getName() const override;
    [[nodiscard]] bool acceptsMidi() const override;
    [[nodiscard]] bool producesMidi() const override;
    [[nodiscard]] bool isMidiEffect() const override;
    [[nodiscard]] double getTailLengthSeconds() const override;
    [[nodiscard]] int getNumPrograms() override;
    [[nodiscard]] int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    [[nodiscard]] const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& name) override;
    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    [[nodiscard]] saf::btle::Parameters readParameters() const noexcept;

    saf::btle::BtleProcessor btle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SafBeetleAudioProcessor)
};
