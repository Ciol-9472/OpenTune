#include "HostIntegration.h"
#include "../Standalone/UI/UIColors.h"
#include "../Utils/UserUiState.h"
#include "../Utils/LocalizationManager.h"

#include <cmath>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace OpenTune {

namespace {

bool tryApplySampleRateHz(juce::AudioDeviceManager& dm, double rateHz)
{
    juce::AudioDeviceManager::AudioDeviceSetup st;
    dm.getAudioDeviceSetup(st);
    st.sampleRate = rateHz;
    return dm.setAudioDeviceSetup(st, true).isEmpty();
}

class StandaloneAudioSettingsContent final : public juce::Component, private juce::ComboBox::Listener
{
public:
    explicit StandaloneAudioSettingsContent(juce::AudioDeviceManager& dm)
        : deviceManager_(dm)
    {
        constexpr int minInputCh = 0;
        constexpr int maxInputCh = 256;
        constexpr int minOutputCh = 0;
        constexpr int maxOutputCh = 256;

        selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager_,
            minInputCh, maxInputCh,
            minOutputCh, maxOutputCh,
            true,
            true,
            true,
            false);
        addAndMakeVisible(*selector_);

        rateLabel_.setText(LOC(kStandaloneAudioOutputSampleRate), juce::dontSendNotification);
        rateLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(rateLabel_);

        rateCombo_.addItem(LOC(kStandaloneAudioSampleRateDriverDefault), 1);
        rateCombo_.addItem("44100 Hz", 2);
        rateCombo_.addItem("48000 Hz", 3);
        rateCombo_.addListener(this);
        addAndMakeVisible(rateCombo_);

        refreshSelectionFromState();
    }

    ~StandaloneAudioSettingsContent() override { rateCombo_.removeListener(this); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(34);
        rateLabel_.setBounds(top.removeFromLeft(178).reduced(6, 0));
        rateCombo_.setBounds(top.reduced(6, 4));
        selector_->setBounds(r);
    }

private:
    juce::AudioDeviceManager& deviceManager_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
    juce::Label rateLabel_;
    juce::ComboBox rateCombo_;

    void refreshSelectionFromState()
    {
        double prefHz = 0.0;
        if (UserUiState::getPreferredStandaloneOutputSampleRate(prefHz))
        {
            if (std::abs(prefHz - 44100.0) < 0.5)
                rateCombo_.setSelectedId(2, juce::dontSendNotification);
            else if (std::abs(prefHz - 48000.0) < 0.5)
                rateCombo_.setSelectedId(3, juce::dontSendNotification);
            else
                rateCombo_.setSelectedId(1, juce::dontSendNotification);
            return;
        }

        juce::AudioDeviceManager::AudioDeviceSetup st;
        deviceManager_.getAudioDeviceSetup(st);
        if (std::abs(st.sampleRate - 44100.0) < 0.5)
            rateCombo_.setSelectedId(2, juce::dontSendNotification);
        else if (std::abs(st.sampleRate - 48000.0) < 0.5)
            rateCombo_.setSelectedId(3, juce::dontSendNotification);
        else
            rateCombo_.setSelectedId(1, juce::dontSendNotification);
    }

    void comboBoxChanged(juce::ComboBox*) override
    {
        const int id = rateCombo_.getSelectedId();
        if (id == 1)
        {
            UserUiState::setPreferredStandaloneOutputSampleRate(0.0);
            return;
        }

        const double hz = (id == 2) ? 44100.0 : 48000.0;
        if (tryApplySampleRateHz(deviceManager_, hz))
        {
            UserUiState::setPreferredStandaloneOutputSampleRate(hz);
            return;
        }

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio",
            juce::String("Unable to set sample rate to ") + juce::String(hz, 0) + " Hz.");
        refreshSelectionFromState();
    }
};

} // namespace

class HostIntegrationStandalone final : public HostIntegration {
public:
    void configureInitialState(OpenTuneAudioProcessor& processor) override
    {
        juce::ignoreUnused(processor);
    }

    bool processIfApplicable(OpenTuneAudioProcessor& processor,
                             juce::AudioBuffer<float>& buffer,
                             int totalNumInputChannels,
                             int totalNumOutputChannels,
                             int numSamples) override
    {
        juce::ignoreUnused(processor, buffer, totalNumInputChannels, totalNumOutputChannels, numSamples);
        return false;
    }

    void audioSettingsRequested(juce::AudioProcessorEditor& editor) override
    {
        juce::ignoreUnused(editor);
        if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
            double prefHz = 0.0;
            if (UserUiState::getPreferredStandaloneOutputSampleRate(prefHz) && prefHz > 0.0)
            {
                juce::AudioDeviceManager::AudioDeviceSetup st;
                holder->deviceManager.getAudioDeviceSetup(st);
                if (std::abs(st.sampleRate - prefHz) > 0.5)
                    (void) tryApplySampleRateHz(holder->deviceManager, prefHz);
            }

            auto* root = new StandaloneAudioSettingsContent(holder->deviceManager);
            root->setSize(500, 490);

            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned(root);
            o.dialogTitle = "Audio Settings";
            o.dialogBackgroundColour = UIColors::backgroundDark;
            o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = false;
            o.resizable = false;

            o.launchAsync();
            return;
        }

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio Settings",
            "Audio settings are not available."
        );
    }
};

std::unique_ptr<HostIntegration> createHostIntegration()
{
    return std::make_unique<HostIntegrationStandalone>();
}

} // namespace OpenTune
