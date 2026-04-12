#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "MenuBarComponent.h"
#include "TransportBarComponent.h"
#include "UIColors.h"
#include "../../Utils/LocalizationManager.h"

namespace OpenTune {

class TopBarComponent : public juce::Component
{
public:
    TopBarComponent(MenuBarComponent& menuBar, TransportBarComponent& transportBar);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void applyTheme();

    // 外部回调：由宿主（PluginEditor）接管右侧属性面板可见性
    std::function<void()> onToggleParameterPanel;

    // 同步右侧属性面板按钮状态（避免 ToggleState 与真实可见性不一致）
    void setParameterPanelToggleState(bool parameterPanelVisible);

    void refreshLocalizedText();  // 刷新本地化文本

private:
    MenuBarComponent& menuBar_;
    TransportBarComponent& transportBar_;

    UnifiedToolbarButton parameterPanelToggleButton_ { LOC(kProps), {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBarComponent)
};

} // namespace OpenTune
