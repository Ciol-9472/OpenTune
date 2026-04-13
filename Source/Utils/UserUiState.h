#pragma once

#include <juce_core/juce_core.h>

namespace OpenTune {

class UserUiState
{
public:
    static bool getStandaloneWindowState(juce::String& stateOut, bool& maximisedOut);
    static void setStandaloneWindowState(const juce::String& state, bool maximised);

    static bool getPianoRollViewState(double& horizontalZoomOut, float& verticalZoomOut, float& verticalScrollOut);
    static void setPianoRollViewState(double horizontalZoom, float verticalZoom, float verticalScroll);

    static bool getWorkspaceSplitRatio(double& splitRatioOut);
    static void setWorkspaceSplitRatio(double splitRatio);

    static bool getAutoTunePromptSettings(float& retuneSpeedPercentOut, float& noteSplitCentsOut, bool& skipPromptOut);
    static void setAutoTunePromptSettings(float retuneSpeedPercent, float noteSplitCents, bool skipPrompt);

private:
    static juce::File storageFile();
    static void ensureLoaded();
    static void persist();
};

} // namespace OpenTune
