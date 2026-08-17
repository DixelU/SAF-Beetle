#pragma once

#include "plugin/PluginProcessor.h"

#include <JuceHeader.h>

#include <initializer_list>
#include <memory>

class SafBeetleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SafBeetleLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosition, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonWidth, int buttonHeight,
                      juce::ComboBox&) override;
    [[nodiscard]] juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    [[nodiscard]] juce::Label* createSliderTextBox(juce::Slider&) override;
};

class SafBeetleAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit SafBeetleAudioProcessorEditor(SafBeetleAudioProcessor&);
    ~SafBeetleAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ParameterKnob final : public juce::Component
    {
    public:
        ParameterKnob(juce::AudioProcessorValueTreeState&, const juce::String& parameterId,
                      const juce::String& name, const juce::String& suffix, int decimalPlaces,
                      double defaultValue, juce::Colour accent, const juce::String& tooltip);

        void resized() override;

    private:
        juce::Label name_;
        juce::Slider slider_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    };

    class PacketSelector final : public juce::Component
    {
    public:
        explicit PacketSelector(juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        juce::Label name_;
        juce::ComboBox selector_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;
    };

    void timerCallback() override;
    void drawPanel(juce::Graphics&, juce::Rectangle<int> bounds,
                   const juce::String& title, const juce::String& subtitle,
                   juce::Colour accent) const;
    void drawSignalPath(juce::Graphics&) const;
    static void layoutControls(juce::Rectangle<int>,
                               std::initializer_list<juce::Component*> controls);

    SafBeetleAudioProcessor& processor_;
    SafBeetleLookAndFeel lookAndFeel_;

    ParameterKnob quality_;
    PacketSelector packet_;
    ParameterKnob burstiness_;
    ParameterKnob burstLength_;
    ParameterKnob burstVariance_;

    ParameterKnob jitter_;
    ParameterKnob temporalSwap_;
    ParameterKnob stutter_;
    ParameterKnob stereoDesync_;
    ParameterKnob clockDrift_;

    ParameterKnob mix_;
    ParameterKnob output_;
    ParameterKnob seed_;

    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> linkPanel_;
    juce::Rectangle<int> corruptionPanel_;
    juce::Rectangle<int> outputPanel_;
    juce::Rectangle<int> signalPathBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SafBeetleAudioProcessorEditor)
};
