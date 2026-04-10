#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "PluginProcessor.h"

namespace OpenTune {

class StemExportDialogContent : public juce::Component
{
public:
    using OnAccepted = std::function<void(juce::String prefix, juce::Array<int> trackIds)>;

    StemExportDialogContent(OpenTuneAudioProcessor& processor, juce::String defaultPrefix);
    void setOnAccepted(OnAccepted fn) { onAccepted_ = std::move(fn); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void okPressed();
    void cancelPressed();
    static juce::String sanitizePrefixForFileNames(juce::String s);

    OpenTuneAudioProcessor& processor_;
    juce::Label prefixLabel_;
    juce::TextEditor prefixEditor_;
    juce::Label tracksLabel_;
    std::array<juce::ToggleButton, OpenTuneAudioProcessor::MAX_TRACKS> trackToggles_;
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;

    OnAccepted onAccepted_;
};

} // namespace OpenTune
