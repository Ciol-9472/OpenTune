#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace OpenTune {

class AutoTuneOptionsDialogContent : public juce::Component
{
public:
    using OnAccepted = std::function<void(float retuneSpeedPercent, float noteSplitCents, bool skipPrompt)>;

    AutoTuneOptionsDialogContent(float initialRetuneSpeedPercent, float initialNoteSplitCents, bool initialSkipPrompt);

    void setOnAccepted(OnAccepted fn) { onAccepted_ = std::move(fn); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void updateValueLabels();
    void okPressed();
    void cancelPressed();

    juce::Label titleLabel_;

    juce::Label retuneLabel_;
    juce::Label retuneValueLabel_;
    juce::Slider retuneSlider_;

    juce::Label noteSplitLabel_;
    juce::Label noteSplitValueLabel_;
    juce::Slider noteSplitSlider_;

    juce::ToggleButton skipPromptToggle_;
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;

    OnAccepted onAccepted_;
};

} // namespace OpenTune
