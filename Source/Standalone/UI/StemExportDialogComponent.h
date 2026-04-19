#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

namespace OpenTune {

class StemExportDialogContent : public juce::Component
{
public:
    using OnAccepted = std::function<void(juce::String prefix, juce::Array<int> trackIds)>;

    StemExportDialogContent(OpenTuneAudioProcessor& processor, juce::String defaultPrefix);
    void setOnAccepted(OnAccepted fn) { onAccepted_ = std::move(fn); }

    /** 与分轨导出、单轨导出默认文件名共用的安全片段（去非法字符等） */
    static juce::String sanitizeFileNameSegment(juce::String s);

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
    std::vector<std::unique_ptr<juce::ToggleButton>> trackToggles_;
    std::vector<int> trackIds_;
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;

    OnAccepted onAccepted_;
};

} // namespace OpenTune
