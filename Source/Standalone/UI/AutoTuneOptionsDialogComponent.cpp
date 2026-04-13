#include "AutoTuneOptionsDialogComponent.h"
#include "UIColors.h"
#include "../../Utils/PitchControlConfig.h"
#include "../../Utils/LocalizationManager.h"

namespace OpenTune {

AutoTuneOptionsDialogContent::AutoTuneOptionsDialogContent(float initialRetuneSpeedPercent,
                                                           float initialNoteSplitCents,
                                                           bool initialSkipPrompt)
{
    titleLabel_.setText(LOC(kAutoOptionsTitle), juce::dontSendNotification);
    titleLabel_.setColour(juce::Label::textColourId, UIColors::textPrimary);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setFont(UIColors::getHeaderFont(16.0f));
    addAndMakeVisible(titleLabel_);

    auto setupNameLabel = [](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, UIColors::textPrimary);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setFont(UIColors::getUIFont(13.0f));
    };

    auto setupValueLabel = [](juce::Label& label) {
        label.setColour(juce::Label::textColourId, UIColors::accent);
        label.setJustificationType(juce::Justification::centredRight);
        label.setFont(UIColors::getUIFont(13.0f));
    };

    auto setupSlider = [](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour(juce::Slider::backgroundColourId, UIColors::backgroundMedium);
        slider.setColour(juce::Slider::trackColourId, UIColors::accent);
        slider.setColour(juce::Slider::thumbColourId, UIColors::textPrimary);
    };

    setupNameLabel(retuneLabel_, LOC(kRetuneSpeed));
    setupValueLabel(retuneValueLabel_);
    addAndMakeVisible(retuneLabel_);
    addAndMakeVisible(retuneValueLabel_);

    setupSlider(retuneSlider_);
    retuneSlider_.setRange(0.0, 100.0, 0.1);
    retuneSlider_.setValue(juce::jlimit(0.0, 100.0, static_cast<double>(initialRetuneSpeedPercent)), juce::dontSendNotification);
    retuneSlider_.onValueChange = [this]() { updateValueLabels(); };
    addAndMakeVisible(retuneSlider_);

    setupNameLabel(noteSplitLabel_, LOC(kNoteSplit));
    setupValueLabel(noteSplitValueLabel_);
    addAndMakeVisible(noteSplitLabel_);
    addAndMakeVisible(noteSplitValueLabel_);

    setupSlider(noteSplitSlider_);
    noteSplitSlider_.setRange(
        PitchControlConfig::kMinNoteSplitCents,
        PitchControlConfig::kMaxNoteSplitCents,
        0.1);
    noteSplitSlider_.setValue(
        juce::jlimit(
            static_cast<double>(PitchControlConfig::kMinNoteSplitCents),
            static_cast<double>(PitchControlConfig::kMaxNoteSplitCents),
            static_cast<double>(initialNoteSplitCents)),
        juce::dontSendNotification);
    noteSplitSlider_.onValueChange = [this]() { updateValueLabels(); };
    addAndMakeVisible(noteSplitSlider_);

    skipPromptToggle_.setButtonText(LOC(kAutoUseAsDefaultAndSkip));
    skipPromptToggle_.setToggleState(initialSkipPrompt, juce::dontSendNotification);
    skipPromptToggle_.setColour(juce::ToggleButton::textColourId, UIColors::textPrimary);
    skipPromptToggle_.setColour(juce::ToggleButton::tickColourId, UIColors::accent);
    addAndMakeVisible(skipPromptToggle_);

    okButton_.setButtonText(LOC(kOk));
    okButton_.setColour(juce::TextButton::buttonColourId, UIColors::accent);
    okButton_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
    okButton_.onClick = [this]() { okPressed(); };
    addAndMakeVisible(okButton_);

    cancelButton_.setButtonText(LOC(kCancel));
    cancelButton_.setColour(juce::TextButton::buttonColourId, UIColors::buttonNormal);
    cancelButton_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
    cancelButton_.onClick = [this]() { cancelPressed(); };
    addAndMakeVisible(cancelButton_);

    updateValueLabels();
}

void AutoTuneOptionsDialogContent::paint(juce::Graphics& g)
{
    g.fillAll(UIColors::backgroundDark);
}

void AutoTuneOptionsDialogContent::resized()
{
    auto bounds = getLocalBounds().reduced(14);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    auto layoutParamRow = [](juce::Rectangle<int> row,
                             juce::Label& nameLabel,
                             juce::Label& valueLabel,
                             juce::Slider& slider) {
        auto topLine = row.removeFromTop(20);
        nameLabel.setBounds(topLine.removeFromLeft(juce::jmax(120, topLine.getWidth() / 2)));
        valueLabel.setBounds(topLine);
        row.removeFromTop(4);
        slider.setBounds(row.removeFromTop(24));
    };

    auto row1 = bounds.removeFromTop(52);
    bounds.removeFromTop(10);
    auto row2 = bounds.removeFromTop(52);
    bounds.removeFromTop(12);

    layoutParamRow(row1, retuneLabel_, retuneValueLabel_, retuneSlider_);
    layoutParamRow(row2, noteSplitLabel_, noteSplitValueLabel_, noteSplitSlider_);

    skipPromptToggle_.setBounds(bounds.removeFromTop(40));

    bounds.removeFromTop(8);

    auto buttonRow = bounds.removeFromBottom(28);
    cancelButton_.setBounds(buttonRow.removeFromRight(90));
    buttonRow.removeFromRight(8);
    okButton_.setBounds(buttonRow.removeFromRight(90));
}

void AutoTuneOptionsDialogContent::updateValueLabels()
{
    const double retuneSpeed = retuneSlider_.getValue();
    const double noteSplit = noteSplitSlider_.getValue();
    retuneValueLabel_.setText(juce::String(retuneSpeed, 1) + " %", juce::dontSendNotification);
    noteSplitValueLabel_.setText(juce::String(noteSplit, 1) + " cents", juce::dontSendNotification);
}

void AutoTuneOptionsDialogContent::okPressed()
{
    const float retuneSpeedPercent = static_cast<float>(retuneSlider_.getValue());
    const float noteSplitCents = static_cast<float>(noteSplitSlider_.getValue());
    const bool skipPrompt = skipPromptToggle_.getToggleState();

    OnAccepted onAccepted = onAccepted_;

    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(1);

    if (onAccepted != nullptr)
    {
        juce::MessageManager::callAsync([onAccepted, retuneSpeedPercent, noteSplitCents, skipPrompt]() mutable {
            onAccepted(retuneSpeedPercent, noteSplitCents, skipPrompt);
        });
    }
}

void AutoTuneOptionsDialogContent::cancelPressed()
{
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(0);
}

} // namespace OpenTune
