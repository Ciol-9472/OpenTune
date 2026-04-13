#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MenuBarComponent.h"
#include "TransportBarComponent.h"
#include "UIColors.h"

namespace OpenTune {

class TopBarComponent : public juce::Component
{
public:
    TopBarComponent(MenuBarComponent& menuBar, TransportBarComponent& transportBar);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void applyTheme();

    void refreshLocalizedText();

private:
    MenuBarComponent& menuBar_;
    TransportBarComponent& transportBar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBarComponent)
};

} // namespace OpenTune
