#include "TopBarComponent.h"
#include "ThemeTokens.h"

namespace OpenTune {

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
    transportBar_.refreshLocalizedText();
    repaint();
}

void TopBarComponent::paint(juce::Graphics& g)
{
    const auto& style = Theme::getActiveStyle();
    const float shadowMargin = 12.0f;
    auto bounds = getLocalBounds().toFloat().reduced(shadowMargin);

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
        menuBar_.setBounds(bounds.removeFromTop(25));
    else
        menuBar_.setBounds({});

    const int pad = 6;
    auto row = bounds.reduced(pad, pad);
    transportBar_.setBounds(row);
}

} // namespace OpenTune
