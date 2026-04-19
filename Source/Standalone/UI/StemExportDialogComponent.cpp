#include "StemExportDialogComponent.h"
#include "UIColors.h"
#include "../../Utils/LocalizationManager.h"

namespace OpenTune {

namespace {

juce::String stripOuterWhitespace(juce::String s)
{
    return s.trimStart().trimEnd();
}

} // namespace

juce::String StemExportDialogContent::sanitizeFileNameSegment(juce::String s)
{
    s = stripOuterWhitespace(s);
    const juce::String badChars("\\/:*?\"<>|");
    for (int i = 0; i < badChars.length(); ++i)
        s = s.replaceCharacter(badChars[i], juce::juce_wchar('_'));

    while (s.startsWithChar('.'))
        s = s.substring(1);
    while (s.endsWithChar('.') || s.endsWithChar(' '))
        s = s.dropLastCharacters(1);

    return stripOuterWhitespace(s);
}

juce::String StemExportDialogContent::sanitizePrefixForFileNames(juce::String s)
{
    s = sanitizeFileNameSegment(s);
    if (s.isEmpty())
        return {};
    return s;
}

StemExportDialogContent::StemExportDialogContent(OpenTuneAudioProcessor& processor, juce::String defaultPrefix)
    : processor_(processor)
{
    prefixLabel_.setText(LOC(kExportStemsPrefixLabel), juce::dontSendNotification);
    prefixLabel_.setColour(juce::Label::textColourId, UIColors::textPrimary);
    prefixLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(prefixLabel_);

    prefixEditor_.setText(defaultPrefix);
    prefixEditor_.setColour(juce::TextEditor::backgroundColourId, UIColors::backgroundMedium);
    prefixEditor_.setColour(juce::TextEditor::textColourId, UIColors::textPrimary);
    prefixEditor_.setColour(juce::TextEditor::outlineColourId, UIColors::panelBorder);
    prefixEditor_.setColour(juce::CaretComponent::caretColourId, UIColors::accent);
    addAndMakeVisible(prefixEditor_);

    tracksLabel_.setText(LOC(kExportStemsTracksLabel), juce::dontSendNotification);
    tracksLabel_.setColour(juce::Label::textColourId, UIColors::textPrimary);
    tracksLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(tracksLabel_);

    for (int i = 0; i < OpenTuneAudioProcessor::MAX_TRACKS; ++i)
    {
        if (!processor_.hasTrackAudio(i))
            continue;

        auto tb = std::make_unique<juce::ToggleButton>();
        juce::String label = processor_.getTrackName(i).trim();
        if (label.isEmpty())
            label = "Track " + juce::String(i + 1);
        tb->setButtonText(label);
        tb->setClickingTogglesState(true);
        tb->setColour(juce::ToggleButton::textColourId, UIColors::textPrimary);
        tb->setColour(juce::ToggleButton::tickColourId, UIColors::accent);
        tb->setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(*tb);
        trackToggles_.push_back(std::move(tb));
        trackIds_.push_back(i);
    }

    okButton_.setButtonText(LOC(kOk));
    okButton_.setColour(juce::TextButton::buttonColourId, UIColors::accent);
    okButton_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
    okButton_.onClick = [this] { okPressed(); };
    addAndMakeVisible(okButton_);

    cancelButton_.setButtonText(LOC(kCancel));
    cancelButton_.setColour(juce::TextButton::buttonColourId, UIColors::buttonNormal);
    cancelButton_.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
    cancelButton_.onClick = [this] { cancelPressed(); };
    addAndMakeVisible(cancelButton_);
}

void StemExportDialogContent::paint(juce::Graphics& g)
{
    g.fillAll(UIColors::backgroundDark);
}

void StemExportDialogContent::resized()
{
    auto bounds = getLocalBounds().reduced(14);
    const int btnH = 28;

    auto bottomRow = bounds.removeFromBottom(btnH);
    cancelButton_.setBounds(bottomRow.removeFromRight(88));
    bottomRow.removeFromRight(8);
    okButton_.setBounds(bottomRow.removeFromRight(88));

    bounds.removeFromBottom(10);

    prefixLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(4);
    prefixEditor_.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(10);

    tracksLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(6);

    const int n = static_cast<int>(trackToggles_.size());
    if (n == 0)
        return;

    const int cols = 2;
    const int rows = (n + cols - 1) / cols;
    const int cellH = juce::jmax(22, bounds.getHeight() / juce::jmax(1, rows));
    const int cellW = bounds.getWidth() / cols;

    for (int i = 0; i < n; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        trackToggles_[static_cast<size_t>(i)]->setBounds(
            bounds.getX() + col * cellW,
            bounds.getY() + row * cellH,
            cellW,
            cellH);
    }
}

void StemExportDialogContent::okPressed()
{
    juce::Array<int> selected;
    for (size_t k = 0; k < trackToggles_.size(); ++k)
    {
        if (trackToggles_[k]->getToggleState())
            selected.add(trackIds_[k]);
    }

    if (selected.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            LOC(kExportStemsTitle),
            LOC(kExportStemsPickTrackWarning));
        return;
    }

    const juce::String safePrefix = sanitizePrefixForFileNames(prefixEditor_.getText());
    OnAccepted onAcc = onAccepted_;

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);

    if (onAcc != nullptr)
    {
        juce::MessageManager::callAsync([onAcc, safePrefix, selected]() mutable
        {
            onAcc(safePrefix, selected);
        });
    }
}

void StemExportDialogContent::cancelPressed()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}

} // namespace OpenTune
