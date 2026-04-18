#include "UserUiState.h"

namespace OpenTune {

namespace {

juce::CriticalSection gLock;
bool gLoaded = false;

bool gHasStandaloneWindowState = false;
juce::String gStandaloneWindowState;
bool gStandaloneWindowMaximised = false;

bool gHasPianoRollZoom = false;
double gPianoRollHorizontalZoom = 1.0;
float gPianoRollVerticalZoom = 50.0f;
float gPianoRollVerticalScroll = 0.0f;

bool gHasWorkspaceSplitRatio = false;
double gWorkspaceSplitRatio = 0.38;

bool gHasAutoTunePromptSettings = false;
float gAutoTuneRetuneSpeedPercent = 15.0f;
float gAutoTuneNoteSplitCents = 80.0f;
bool gAutoTuneSkipPrompt = false;

bool gHasStandalonePreferredSampleRate = false;
double gStandalonePreferredSampleRateHz = 0.0;

} // namespace

juce::File UserUiState::storageFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("OpenTune")
        .getChildFile("user_ui_state.xml");
}

void UserUiState::ensureLoaded()
{
    const juce::ScopedLock sl(gLock);
    if (gLoaded)
        return;

    gLoaded = true;
    gHasStandaloneWindowState = false;
    gStandaloneWindowState.clear();
    gStandaloneWindowMaximised = false;
    gHasPianoRollZoom = false;
    gPianoRollHorizontalZoom = 1.0;
    gPianoRollVerticalZoom = 50.0f;
    gPianoRollVerticalScroll = 0.0f;
    gHasWorkspaceSplitRatio = false;
    gWorkspaceSplitRatio = 0.38;
    gHasAutoTunePromptSettings = false;
    gAutoTuneRetuneSpeedPercent = 15.0f;
    gAutoTuneNoteSplitCents = 80.0f;
    gAutoTuneSkipPrompt = false;
    gHasStandalonePreferredSampleRate = false;
    gStandalonePreferredSampleRateHz = 0.0;

    const juce::File file = storageFile();
    if (!file.existsAsFile())
        return;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName("UserUiState"))
        return;

    if (xml->hasAttribute("standaloneWindowState"))
    {
        gStandaloneWindowState = xml->getStringAttribute("standaloneWindowState");
        gHasStandaloneWindowState = gStandaloneWindowState.isNotEmpty();
        gStandaloneWindowMaximised = xml->getBoolAttribute("standaloneWindowMaximised", false);
    }

    if (xml->hasAttribute("pianoRollHorizontalZoom") && xml->hasAttribute("pianoRollVerticalZoom"))
    {
        gPianoRollHorizontalZoom = xml->getDoubleAttribute("pianoRollHorizontalZoom", 1.0);
        gPianoRollVerticalZoom = static_cast<float>(xml->getDoubleAttribute("pianoRollVerticalZoom", 50.0));
        gPianoRollVerticalScroll = static_cast<float>(xml->getDoubleAttribute("pianoRollVerticalScroll", 0.0));
        gHasPianoRollZoom = true;
    }

    if (xml->hasAttribute("workspaceSplitRatio"))
    {
        gWorkspaceSplitRatio = xml->getDoubleAttribute("workspaceSplitRatio", 0.38);
        gHasWorkspaceSplitRatio = gWorkspaceSplitRatio > 0.0;
    }

    if (xml->hasAttribute("autoTuneRetuneSpeedPercent") && xml->hasAttribute("autoTuneNoteSplitCents"))
    {
        gAutoTuneRetuneSpeedPercent = static_cast<float>(xml->getDoubleAttribute("autoTuneRetuneSpeedPercent", 15.0));
        gAutoTuneNoteSplitCents = static_cast<float>(xml->getDoubleAttribute("autoTuneNoteSplitCents", 80.0));
        gAutoTuneSkipPrompt = xml->getBoolAttribute("autoTuneSkipPrompt", false);
        gHasAutoTunePromptSettings = true;
    }

    if (xml->hasAttribute("standalonePreferredSampleRateHz"))
    {
        const double v = xml->getDoubleAttribute("standalonePreferredSampleRateHz", 0.0);
        if (v > 0.0)
        {
            gStandalonePreferredSampleRateHz = v;
            gHasStandalonePreferredSampleRate = true;
        }
    }
}

void UserUiState::persist()
{
    juce::XmlElement root("UserUiState");

    {
        const juce::ScopedLock sl(gLock);

        if (gHasStandaloneWindowState && gStandaloneWindowState.isNotEmpty())
        {
            root.setAttribute("standaloneWindowState", gStandaloneWindowState);
            root.setAttribute("standaloneWindowMaximised", gStandaloneWindowMaximised);
        }

        if (gHasPianoRollZoom)
        {
            root.setAttribute("pianoRollHorizontalZoom", gPianoRollHorizontalZoom);
            root.setAttribute("pianoRollVerticalZoom", static_cast<double>(gPianoRollVerticalZoom));
            root.setAttribute("pianoRollVerticalScroll", static_cast<double>(gPianoRollVerticalScroll));
        }

        if (gHasWorkspaceSplitRatio)
            root.setAttribute("workspaceSplitRatio", gWorkspaceSplitRatio);

        if (gHasAutoTunePromptSettings)
        {
            root.setAttribute("autoTuneRetuneSpeedPercent", static_cast<double>(gAutoTuneRetuneSpeedPercent));
            root.setAttribute("autoTuneNoteSplitCents", static_cast<double>(gAutoTuneNoteSplitCents));
            root.setAttribute("autoTuneSkipPrompt", gAutoTuneSkipPrompt);
        }

        if (gHasStandalonePreferredSampleRate && gStandalonePreferredSampleRateHz > 0.0)
            root.setAttribute("standalonePreferredSampleRateHz", gStandalonePreferredSampleRateHz);
    }

    const juce::File file = storageFile();
    (void)file.getParentDirectory().createDirectory();
    (void)root.writeTo(file);
}

bool UserUiState::getStandaloneWindowState(juce::String& stateOut, bool& maximisedOut)
{
    ensureLoaded();

    const juce::ScopedLock sl(gLock);
    if (!gHasStandaloneWindowState || gStandaloneWindowState.isEmpty())
        return false;

    stateOut = gStandaloneWindowState;
    maximisedOut = gStandaloneWindowMaximised;
    return true;
}

void UserUiState::setStandaloneWindowState(const juce::String& state, bool maximised)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        gStandaloneWindowState = state;
        gStandaloneWindowMaximised = maximised;
        gHasStandaloneWindowState = state.isNotEmpty();
    }

    persist();
}

bool UserUiState::getPianoRollViewState(double& horizontalZoomOut, float& verticalZoomOut, float& verticalScrollOut)
{
    ensureLoaded();

    const juce::ScopedLock sl(gLock);
    if (!gHasPianoRollZoom)
        return false;

    horizontalZoomOut = gPianoRollHorizontalZoom;
    verticalZoomOut = gPianoRollVerticalZoom;
    verticalScrollOut = gPianoRollVerticalScroll;
    return true;
}

void UserUiState::setPianoRollViewState(double horizontalZoom, float verticalZoom, float verticalScroll)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        gPianoRollHorizontalZoom = horizontalZoom;
        gPianoRollVerticalZoom = verticalZoom;
        gPianoRollVerticalScroll = verticalScroll;
        gHasPianoRollZoom = true;
    }

    persist();
}

bool UserUiState::getWorkspaceSplitRatio(double& splitRatioOut)
{
    ensureLoaded();

    const juce::ScopedLock sl(gLock);
    if (!gHasWorkspaceSplitRatio)
        return false;

    splitRatioOut = gWorkspaceSplitRatio;
    return true;
}

void UserUiState::setWorkspaceSplitRatio(double splitRatio)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        gWorkspaceSplitRatio = splitRatio;
        gHasWorkspaceSplitRatio = splitRatio > 0.0;
    }

    persist();
}

bool UserUiState::getAutoTunePromptSettings(float& retuneSpeedPercentOut, float& noteSplitCentsOut, bool& skipPromptOut)
{
    ensureLoaded();

    const juce::ScopedLock sl(gLock);
    if (!gHasAutoTunePromptSettings)
        return false;

    retuneSpeedPercentOut = gAutoTuneRetuneSpeedPercent;
    noteSplitCentsOut = gAutoTuneNoteSplitCents;
    skipPromptOut = gAutoTuneSkipPrompt;
    return true;
}

void UserUiState::setAutoTunePromptSettings(float retuneSpeedPercent, float noteSplitCents, bool skipPrompt)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        gAutoTuneRetuneSpeedPercent = retuneSpeedPercent;
        gAutoTuneNoteSplitCents = noteSplitCents;
        gAutoTuneSkipPrompt = skipPrompt;
        gHasAutoTunePromptSettings = true;
    }

    persist();
}

bool UserUiState::getPreferredStandaloneOutputSampleRate(double& sampleRateOut)
{
    ensureLoaded();

    const juce::ScopedLock sl(gLock);
    if (!gHasStandalonePreferredSampleRate || gStandalonePreferredSampleRateHz <= 0.0)
        return false;

    sampleRateOut = gStandalonePreferredSampleRateHz;
    return true;
}

void UserUiState::setPreferredStandaloneOutputSampleRate(double sampleRateHz)
{
    {
        const juce::ScopedLock sl(gLock);
        gLoaded = true;
        if (sampleRateHz <= 0.0)
        {
            gHasStandalonePreferredSampleRate = false;
            gStandalonePreferredSampleRateHz = 0.0;
        }
        else
        {
            gHasStandalonePreferredSampleRate = true;
            gStandalonePreferredSampleRateHz = sampleRateHz;
        }
    }

    persist();
}

} // namespace OpenTune
