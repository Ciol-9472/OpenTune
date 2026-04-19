#include "TopBarComponent.h"
#include "ThemeTokens.h"

namespace OpenTune {

namespace {
/** Keep in sync with OpenTuneAudioProcessorEditor::MENU_BAR_HEIGHT */
constexpr int kMenuBarStripHeight = 26;
} // namespace

TopBarComponent::TopBarComponent(MenuBarComponent& menuBar, TransportBarComponent& transportBar)
    : menuBar_(menuBar)
    , transportBar_(transportBar)
{
    transportBar_.setEmbeddedInTopBar(true);
    addAndMakeVisible(menuBar_);
    addAndMakeVisible(transportBar_);
}

void TopBarComponent::applyTheme()
{
    transportBar_.applyTheme();
    repaint();
}

void TopBarComponent::refreshLocalizedText()
{
    menuBar_.refreshLocalizedText();
    transportBar_.refreshLocalizedText();
    repaint();
}

void TopBarComponent::paint(juce::Graphics& g)
{
    const auto& style = Theme::getActiveStyle();
    const float shadowMargin = 12.0f;
    auto inner = getLocalBounds().toFloat().reduced(shadowMargin);

    if (menuBar_.isVisible())
    {
        auto menuStrip = inner.removeFromTop(static_cast<float>(kMenuBarStripHeight));
        // 浅色菜单条（接近 Windows / Audition 顶栏菜单区）
        g.setColour(juce::Colour(0xfff3f3f3));
        g.fillRect(menuStrip);
        g.setColour(juce::Colour(0xffc8c8c8));
        g.drawLine(menuStrip.getX(), menuStrip.getBottom(), menuStrip.getRight(), menuStrip.getBottom(), 1.0f);

        if (inner.getHeight() <= 0.5f)
            return;
    }

    auto bounds = inner;

    if (Theme::getActiveTheme() == ThemeId::Aurora)
    {
        UIColors::drawShadow(g, bounds, UIColors::ShadowLevel::Float);

        juce::ColourGradient bgGrad(UIColors::backgroundDark, 0.0f, 0.0f,
                                    UIColors::backgroundMedium, 0.0f, bounds.getHeight(), false);
        g.setGradientFill(bgGrad);
        g.fillRect(bounds);

        juce::ColourGradient bottomBlur(juce::Colours::transparentBlack, 0.0f, bounds.getBottom() - 4.0f,
                                        juce::Colour(Aurora::Colors::BorderGlow).withAlpha(0.2f), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(bottomBlur);
        g.fillRect(bounds.getX(), bounds.getBottom() - 4.0f, bounds.getWidth(), 4.0f);
    }
    else
    {
        UIColors::drawShadow(g, bounds, UIColors::ShadowLevel::Float);
        UIColors::fillPanelBackground(g, bounds, style.panelRadius);
        UIColors::drawPanelFrame(g, bounds, style.panelRadius);
    }
}

void TopBarComponent::resized()
{
    const int shadowMargin = 12;
    auto bounds = getLocalBounds().reduced(shadowMargin);

    if (menuBar_.isVisible())
        menuBar_.setBounds(bounds.removeFromTop(kMenuBarStripHeight));
    else
        menuBar_.setBounds({});

    const int pad = 6;
    auto row = bounds.reduced(pad, pad);
    const int prefTransportW = transportBar_.getPreferredContentWidth();
    const int transportW = juce::jmin(prefTransportW, row.getWidth());
    transportBar_.setBounds(row.withSizeKeepingCentre(transportW, row.getHeight()));
}

} // namespace OpenTune
