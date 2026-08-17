#include "plugin/PluginEditor.h"

#include <array>
#include <cmath>

namespace
{
constexpr auto background = 0xff17191c;
constexpr auto backgroundBottom = 0xff111315;
constexpr auto panel = 0xff23272b;
constexpr auto panelBorder = 0xff34393e;
constexpr auto separator = 0xff30353a;
constexpr auto text = 0xffe5e7e8;
constexpr auto mutedText = 0xff8b9399;
constexpr auto orange = 0xffffa21a;
constexpr auto warmOrange = 0xffff7626;
constexpr auto green = 0xff9bc45d;

juce::Font font(float height, int style = juce::Font::plain)
{
    return juce::Font{juce::FontOptions{height, style}};
}

juce::Rectangle<float> centredSquare(juce::Rectangle<float> bounds)
{
    const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    return juce::Rectangle<float>{side, side}.withCentre(bounds.getCentre());
}

void drawControlDividers(juce::Graphics& graphics, juce::Rectangle<int> bounds, int columns)
{
    if (columns < 2)
        return;

    graphics.setColour(juce::Colour{separator});
    for (auto column = 1; column < columns; ++column)
    {
        const auto x = bounds.getX() + (bounds.getWidth() * column) / columns;
        graphics.drawVerticalLine(x, static_cast<float>(bounds.getY() + 7),
                                  static_cast<float>(bounds.getBottom() - 7));
    }
}
}

SafBeetleLookAndFeel::SafBeetleLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour{orange});
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour{0xff4a5055});
    setColour(juce::Slider::textBoxTextColourId, juce::Colour{text});
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour{0xff171a1d});
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour{0xff373c41});
    setColour(juce::ComboBox::backgroundColourId, juce::Colour{0xff171a1d});
    setColour(juce::ComboBox::textColourId, juce::Colour{text});
    setColour(juce::ComboBox::outlineColourId, juce::Colour{0xff3c4247});
    setColour(juce::ComboBox::arrowColourId, juce::Colour{orange});
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour{0xff202428});
    setColour(juce::PopupMenu::textColourId, juce::Colour{text});
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour{0xff3a3f44});
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour{orange});
}

void SafBeetleLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y,
                                            int width, int height, float sliderPosition,
                                            float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider& slider)
{
    auto bounds = centredSquare(juce::Rectangle<float>{static_cast<float>(x), static_cast<float>(y),
                                                        static_cast<float>(width), static_cast<float>(height)})
                      .reduced(7.0f);
    const auto radius = bounds.getWidth() * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

    for (auto index = 0; index < 11; ++index)
    {
        const auto tickAngle = rotaryStartAngle
            + static_cast<float>(index) * (rotaryEndAngle - rotaryStartAngle) / 10.0f;
        const auto outer = juce::Point<float>{0.0f, -(radius + 3.0f)}
                               .rotatedAboutOrigin(tickAngle)
                               .translated(centre.x, centre.y);
        const auto inner = juce::Point<float>{0.0f, -(radius + (index % 5 == 0 ? -0.5f : 0.5f))}
                               .rotatedAboutOrigin(tickAngle)
                               .translated(centre.x, centre.y);
        graphics.setColour(juce::Colour{index == 0 || index == 10 ? 0xff697178 : 0xff444a4f});
        graphics.drawLine(inner.x, inner.y, outer.x, outer.y, 1.0f);
    }

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 1.0f, radius - 1.0f,
                        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    graphics.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    graphics.strokePath(track, juce::PathStrokeType{4.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded});

    if (sliderPosition > 0.001f)
    {
        juce::Path valueTrack;
        valueTrack.addCentredArc(centre.x, centre.y, radius - 1.0f, radius - 1.0f,
                                 0.0f, rotaryStartAngle, angle, true);
        graphics.setColour(accent);
        graphics.strokePath(valueTrack, juce::PathStrokeType{4.0f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded});
    }

    juce::ColourGradient face{juce::Colour{0xff4a5055}, bounds.getX(), bounds.getY(),
                              juce::Colour{0xff262a2e}, bounds.getRight(), bounds.getBottom(), false};
    graphics.setGradientFill(face);
    graphics.fillEllipse(bounds.reduced(4.0f));
    graphics.setColour(juce::Colour{slider.isMouseOverOrDragging() ? 0xff747d84 : 0xff555c62});
    graphics.drawEllipse(bounds.reduced(4.0f), 1.0f);

    const auto pointerStart = juce::Point<float>{0.0f, -3.5f}
                                  .rotatedAboutOrigin(angle)
                                  .translated(centre.x, centre.y);
    const auto pointerEnd = juce::Point<float>{0.0f, -(radius - 10.0f)}
                                .rotatedAboutOrigin(angle)
                                .translated(centre.x, centre.y);
    graphics.setColour(accent.brighter(0.12f));
    graphics.drawLine(pointerStart.x, pointerStart.y, pointerEnd.x, pointerEnd.y, 2.2f);
    graphics.fillEllipse(juce::Rectangle<float>{5.0f, 5.0f}.withCentre(centre));
}

void SafBeetleLookAndFeel::drawComboBox(juce::Graphics& graphics, int width, int height,
                                        bool isButtonDown, int buttonX, int buttonY,
                                        int buttonWidth, int buttonHeight, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>{0.5f, 0.5f, static_cast<float>(width - 1),
                                          static_cast<float>(height - 1)};
    graphics.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour{box.isMouseOver() || box.hasKeyboardFocus(true)
                                        ? 0xff697178
                                        : 0xff3c4247});
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    const auto arrowArea = juce::Rectangle<float>{static_cast<float>(buttonX),
                                                   static_cast<float>(buttonY),
                                                   static_cast<float>(buttonWidth),
                                                   static_cast<float>(buttonHeight)};
    const auto centre = arrowArea.getCentre();
    juce::Path arrow;
    arrow.startNewSubPath(centre.x - 4.0f, centre.y - 2.0f);
    arrow.lineTo(centre.x, centre.y + 2.0f);
    arrow.lineTo(centre.x + 4.0f, centre.y - 2.0f);
    graphics.setColour(box.findColour(juce::ComboBox::arrowColourId)
                           .withMultipliedBrightness(isButtonDown ? 0.75f : 1.0f));
    graphics.strokePath(arrow, juce::PathStrokeType{1.8f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded});
}

juce::Font SafBeetleLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return font(11.5f, juce::Font::bold);
}

void SafBeetleLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(9, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Label* SafBeetleLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(font(11.0f, juce::Font::bold));
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
    label->setColour(juce::Label::backgroundColourId,
                     slider.findColour(juce::Slider::textBoxBackgroundColourId));
    label->setColour(juce::Label::outlineColourId,
                     slider.findColour(juce::Slider::textBoxOutlineColourId));
    return label;
}

SafBeetleAudioProcessorEditor::ParameterKnob::ParameterKnob(
    juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
    const juce::String& name, const juce::String& suffix, int decimalPlaces,
    double defaultValue, juce::Colour accent, const juce::String& tooltip)
{
    name_.setText(name.toUpperCase(), juce::dontSendNotification);
    name_.setFont(font(10.5f, juce::Font::bold));
    name_.setColour(juce::Label::textColourId, juce::Colour{mutedText});
    name_.setJustificationType(juce::Justification::centred);
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);

    slider_.setName(name);
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 19);
    slider_.setTextValueSuffix(suffix);
    slider_.setNumDecimalPlacesToDisplay(decimalPlaces);
    slider_.setDoubleClickReturnValue(true, defaultValue);
    slider_.setMouseDragSensitivity(190);
    slider_.setTooltip(tooltip);
    slider_.setColour(juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible(slider_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, parameterId, slider_);
}

void SafBeetleAudioProcessorEditor::ParameterKnob::resized()
{
    auto bounds = getLocalBounds();
    name_.setBounds(bounds.removeFromTop(18));
    slider_.setBounds(bounds);
}

SafBeetleAudioProcessorEditor::PacketSelector::PacketSelector(
    juce::AudioProcessorValueTreeState& state)
{
    name_.setText("PACKET SIZE", juce::dontSendNotification);
    name_.setFont(font(10.5f, juce::Font::bold));
    name_.setColour(juce::Label::textColourId, juce::Colour{mutedText});
    name_.setJustificationType(juce::Justification::centred);
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);

    selector_.setName("Packet Size");
    selector_.addItem("2.5 ms", 1);
    selector_.addItem("5 ms", 2);
    selector_.addItem("10 ms", 3);
    selector_.addItem("20 ms", 4);
    selector_.setTooltip("Duration of each simulated Bluetooth packet");
    addAndMakeVisible(selector_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, "packet", selector_);
}

void SafBeetleAudioProcessorEditor::PacketSelector::resized()
{
    auto bounds = getLocalBounds();
    name_.setBounds(bounds.removeFromTop(18));
    selector_.setBounds(bounds.withSizeKeepingCentre(104, 27));
}

SafBeetleAudioProcessorEditor::SafBeetleAudioProcessorEditor(SafBeetleAudioProcessor& processor)
    : AudioProcessorEditor(processor),
      processor_(processor),
      quality_(processor.parameters, "quality", "Signal Quality", "%", 1, 65.0,
               juce::Colour{green}, "Master health of the simulated wireless link"),
      packet_(processor.parameters),
      burstiness_(processor.parameters, "burst", "Burstiness", "%", 1, 65.0,
                  juce::Colour{orange}, "How often a new corruption burst begins"),
      burstLength_(processor.parameters, "burstlen", "Burst Length", " pkt", 0, 8.0,
                   juce::Colour{orange}, "Length of each corruption event in packets"),
      burstVariance_(processor.parameters, "burstvar", "Burst Variance", "%", 1, 15.0,
                     juce::Colour{orange}, "Random variation applied to burst length"),
      jitter_(processor.parameters, "jitter", "Jitter", "%", 1, 35.0,
              juce::Colour{warmOrange}, "Reads nearby packets early or late"),
      temporalSwap_(processor.parameters, "swap", "Temporal Swap", "%", 1, 20.0,
                    juce::Colour{warmOrange}, "Exchanges displaced packet ranges"),
      stutter_(processor.parameters, "stutter", "Stutter", "%", 1, 25.0,
               juce::Colour{warmOrange}, "Freezes and repeats a packet"),
      stereoDesync_(processor.parameters, "stereo", "Stereo Desync", "%", 1, 25.0,
                    juce::Colour{warmOrange}, "Temporarily delays the right channel"),
      clockDrift_(processor.parameters, "drift", "Clock Drift", "%", 1, 20.0,
                  juce::Colour{warmOrange}, "Moves the receive clock before a hard resync"),
      mix_(processor.parameters, "mix", "Mix", "%", 1, 100.0,
           juce::Colour{green}, "Blend between latency-aligned clean and damaged audio"),
      output_(processor.parameters, "output", "Output", " dB", 1, 0.0,
              juce::Colour{green}, "Output trim"),
      seed_(processor.parameters, "seed", "Pattern Seed", {}, 0, 812.0,
            juce::Colour{green}, "Seed used to reproduce the same fault pattern")
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);

    for (auto* control : std::array<juce::Component*, 13>{
             &quality_, &packet_, &burstiness_, &burstLength_, &burstVariance_,
             &jitter_, &temporalSwap_, &stutter_, &stereoDesync_, &clockDrift_,
             &mix_, &output_, &seed_})
        addAndMakeVisible(*control);

    setSize(820, 560);
    startTimerHz(12);
}

SafBeetleAudioProcessorEditor::~SafBeetleAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SafBeetleAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ColourGradient base{juce::Colour{background}, 0.0f, 0.0f,
                              juce::Colour{backgroundBottom}, 0.0f,
                              static_cast<float>(getHeight()), false};
    graphics.setGradientFill(base);
    graphics.fillAll();

    graphics.setColour(juce::Colour{orange});
    graphics.fillRect(0, 0, getWidth(), 2);

    const auto logo = headerBounds_.withWidth(43).withHeight(43).withCentre(
        {headerBounds_.getX() + 24, headerBounds_.getCentreY()});
    graphics.setColour(juce::Colour{orange});
    graphics.fillEllipse(logo.toFloat().reduced(3.0f));
    graphics.setColour(juce::Colour{0xff25282b});
    graphics.fillEllipse(logo.toFloat().reduced(11.0f, 7.0f));
    graphics.drawLine(static_cast<float>(logo.getCentreX()), static_cast<float>(logo.getY() + 9),
                      static_cast<float>(logo.getCentreX()), static_cast<float>(logo.getBottom() - 9), 1.5f);
    graphics.drawLine(static_cast<float>(logo.getCentreX() - 8), static_cast<float>(logo.getY() + 9),
                      static_cast<float>(logo.getCentreX() - 13), static_cast<float>(logo.getY() + 4), 1.5f);
    graphics.drawLine(static_cast<float>(logo.getCentreX() + 8), static_cast<float>(logo.getY() + 9),
                      static_cast<float>(logo.getCentreX() + 13), static_cast<float>(logo.getY() + 4), 1.5f);

    auto titleBounds = headerBounds_.withTrimmedLeft(55).withTrimmedRight(260);
    graphics.setColour(juce::Colour{text});
    graphics.setFont(font(22.0f, juce::Font::bold));
    graphics.drawText("SAF BEETLE", titleBounds.removeFromTop(31), juce::Justification::centredLeft);
    graphics.setColour(juce::Colour{mutedText});
    graphics.setFont(font(10.0f, juce::Font::bold));
    graphics.drawText("BLUETOOTH LOSS EMULATOR  /  SIMPLE AF", titleBounds,
                      juce::Justification::centredLeft);

    auto status = headerBounds_.withTrimmedLeft(headerBounds_.getWidth() - 242).reduced(0, 13);
    const auto qualityValue = processor_.parameters.getRawParameterValue("quality")
                                  ->load(std::memory_order_relaxed);
    const auto activeBars = juce::jlimit(0, 5, static_cast<int>(std::ceil(qualityValue / 20.0f)));

    graphics.setColour(juce::Colour{0xff22262a});
    graphics.fillRoundedRectangle(status.toFloat(), 4.0f);
    graphics.setColour(juce::Colour{panelBorder});
    graphics.drawRoundedRectangle(status.toFloat().reduced(0.5f), 4.0f, 1.0f);

    auto bars = status.removeFromLeft(76).reduced(12, 9);
    for (auto index = 0; index < 5; ++index)
    {
        const auto barHeight = 5 + index * 3;
        const auto bar = juce::Rectangle<int>{bars.getX() + index * 9, bars.getBottom() - barHeight,
                                              5, barHeight};
        graphics.setColour(index < activeBars ? juce::Colour{green} : juce::Colour{0xff3d4348});
        graphics.fillRoundedRectangle(bar.toFloat(), 1.0f);
    }

    graphics.setColour(juce::Colour{mutedText});
    graphics.setFont(font(9.0f, juce::Font::bold));
    graphics.drawText("LINK QUALITY", status.removeFromTop(16), juce::Justification::centredLeft);
    graphics.setColour(juce::Colour{text});
    graphics.setFont(font(12.0f, juce::Font::bold));
    graphics.drawText(juce::String{qualityValue, 1} + "%", status,
                      juce::Justification::centredLeft);

    drawPanel(graphics, linkPanel_, "LINK", "packet shape and fault envelope", juce::Colour{orange});
    drawPanel(graphics, corruptionPanel_, "CORRUPTION", "timing, repeats and channel damage",
              juce::Colour{warmOrange});
    drawPanel(graphics, outputPanel_, "OUTPUT", "parallel blend and repeatable pattern",
              juce::Colour{green});

    drawControlDividers(graphics, linkPanel_.reduced(12).withTrimmedTop(29), 5);
    drawControlDividers(graphics, corruptionPanel_.reduced(12).withTrimmedTop(29), 5);
    drawControlDividers(graphics,
                        outputPanel_.reduced(12).withTrimmedTop(29).withWidth(
                            outputPanel_.reduced(12).getWidth() * 3 / 5),
                        3);
    drawSignalPath(graphics);
}

void SafBeetleAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(14);
    headerBounds_ = bounds.removeFromTop(62);
    bounds.removeFromTop(10);
    linkPanel_ = bounds.removeFromTop(146);
    bounds.removeFromTop(10);
    corruptionPanel_ = bounds.removeFromTop(146);
    bounds.removeFromTop(10);
    outputPanel_ = bounds;

    auto linkControls = linkPanel_.reduced(12).withTrimmedTop(29);
    layoutControls(linkControls, {&quality_, &packet_, &burstiness_, &burstLength_, &burstVariance_});

    auto corruptionControls = corruptionPanel_.reduced(12).withTrimmedTop(29);
    layoutControls(corruptionControls,
                   {&jitter_, &temporalSwap_, &stutter_, &stereoDesync_, &clockDrift_});

    auto outputContents = outputPanel_.reduced(12).withTrimmedTop(29);
    auto outputControls = outputContents.removeFromLeft(outputContents.getWidth() * 3 / 5);
    layoutControls(outputControls, {&mix_, &output_, &seed_});
    signalPathBounds_ = outputContents.reduced(13, 8);
}

void SafBeetleAudioProcessorEditor::timerCallback()
{
    repaint(headerBounds_);
}

void SafBeetleAudioProcessorEditor::drawPanel(juce::Graphics& graphics,
                                               juce::Rectangle<int> bounds,
                                               const juce::String& title,
                                               const juce::String& subtitle,
                                               juce::Colour accent) const
{
    graphics.setColour(juce::Colour{panel});
    graphics.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    graphics.setColour(juce::Colour{panelBorder});
    graphics.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);

    graphics.setColour(accent);
    graphics.fillRoundedRectangle(static_cast<float>(bounds.getX() + 12),
                                  static_cast<float>(bounds.getY() + 9), 42.0f, 3.0f, 1.5f);
    graphics.setFont(font(10.0f, juce::Font::bold));
    graphics.drawText(title, bounds.getX() + 62, bounds.getY() + 4, 108, 18,
                      juce::Justification::centredLeft);
    graphics.setColour(juce::Colour{mutedText});
    graphics.setFont(font(9.5f));
    graphics.drawText(subtitle, bounds.getX() + 164, bounds.getY() + 4,
                      bounds.getWidth() - 177, 18, juce::Justification::centredLeft);
    graphics.setColour(juce::Colour{separator});
    graphics.drawHorizontalLine(bounds.getY() + 28, static_cast<float>(bounds.getX() + 12),
                                static_cast<float>(bounds.getRight() - 12));
}

void SafBeetleAudioProcessorEditor::drawSignalPath(juce::Graphics& graphics) const
{
    auto bounds = signalPathBounds_;
    graphics.setColour(juce::Colour{mutedText});
    graphics.setFont(font(9.0f, juce::Font::bold));
    graphics.drawText("SIGNAL PATH", bounds.removeFromTop(15), juce::Justification::centredLeft);

    auto route = bounds.removeFromTop(38);
    constexpr std::array<const char*, 3> labels{"IN", "BTLE", "OUT"};
    const auto blockWidth = (route.getWidth() - 34) / 3;
    for (auto index = 0; index < 3; ++index)
    {
        const auto block = route.removeFromLeft(blockWidth).reduced(0, 5);
        graphics.setColour(juce::Colour{index == 1 ? 0xff353a3f : 0xff292e32});
        graphics.fillRoundedRectangle(block.toFloat(), 3.0f);
        graphics.setColour(index == 1 ? juce::Colour{orange} : juce::Colour{panelBorder});
        graphics.drawRoundedRectangle(block.toFloat().reduced(0.5f), 3.0f, 1.0f);
        graphics.setColour(index == 1 ? juce::Colour{orange} : juce::Colour{text});
        graphics.setFont(font(9.0f, juce::Font::bold));
        graphics.drawText(labels[static_cast<std::size_t>(index)], block,
                          juce::Justification::centred);

        if (index < 2)
        {
            auto arrow = route.removeFromLeft(17);
            graphics.setColour(juce::Colour{0xff616970});
            const auto y = static_cast<float>(arrow.getCentreY());
            graphics.drawLine(static_cast<float>(arrow.getX() + 3), y,
                              static_cast<float>(arrow.getRight() - 4), y, 1.0f);
            graphics.drawLine(static_cast<float>(arrow.getRight() - 7), y - 3.0f,
                              static_cast<float>(arrow.getRight() - 4), y, 1.0f);
            graphics.drawLine(static_cast<float>(arrow.getRight() - 7), y + 3.0f,
                              static_cast<float>(arrow.getRight() - 4), y, 1.0f);
        }
    }

    bounds.removeFromTop(5);
    auto badges = bounds.removeFromTop(22);
    const std::array<juce::String, 2> badgeLabels{"50 ms PDC", "HOST AUTOMATION"};
    const auto badgeWidth = (badges.getWidth() - 6) / 2;
    for (const auto& badgeLabel : badgeLabels)
    {
        auto badge = badges.removeFromLeft(badgeWidth);
        graphics.setColour(juce::Colour{0xff1a1e21});
        graphics.fillRoundedRectangle(badge.toFloat(), 3.0f);
        graphics.setColour(juce::Colour{0xff41474c});
        graphics.drawRoundedRectangle(badge.toFloat().reduced(0.5f), 3.0f, 1.0f);
        graphics.setColour(juce::Colour{mutedText});
        graphics.setFont(font(8.5f, juce::Font::bold));
        graphics.drawText(badgeLabel, badge, juce::Justification::centred);
        badges.removeFromLeft(6);
    }
}

void SafBeetleAudioProcessorEditor::layoutControls(
    juce::Rectangle<int> bounds, std::initializer_list<juce::Component*> controls)
{
    const auto count = static_cast<int>(controls.size());
    auto index = 0;
    for (auto* control : controls)
    {
        const auto left = bounds.getX() + (bounds.getWidth() * index) / count;
        const auto right = bounds.getX() + (bounds.getWidth() * (index + 1)) / count;
        control->setBounds(juce::Rectangle<int>{left, bounds.getY(), right - left, bounds.getHeight()}
                               .reduced(5, 1));
        ++index;
    }
}
