#include "ParameterPanel.h"
#include "ToolbarIcons.h"
#include "../../Utils/PitchControlConfig.h"
#include "../../Utils/LocalizationManager.h"
#include <vector>

namespace OpenTune {

ParameterPanel::ToolIconButton::ToolIconButton(int toolId, const juce::String& name, const juce::String& tooltip)
    : juce::Button(name), toolId_(toolId)
{
    setTooltip(tooltip);
    setClickingTogglesState(true);
}

void ParameterPanel::ToolIconButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    juce::ignoreUnused(bounds);

    auto base = getToggleState() ? UIColors::accent : UIColors::buttonNormal;
    getLookAndFeel().drawButtonBackground(g, *this, base, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    if (textIcon_.isNotEmpty())
    {
        g.setColour(UIColors::textPrimary);
        g.setFont(UIColors::getUIFont(16.0f));
        g.drawText(textIcon_, getLocalBounds().toFloat(), juce::Justification::centred);
    }
    else
    {
        auto iconArea = getLocalBounds().toFloat().reduced(10.0f);
        ToolbarIcons::drawIcon(g, iconPath_, iconArea, UIColors::textPrimary, 2.0f, fillIcon_);
    }
}

// ============================================================================
// LargeKnobLookAndFeel Implementation
// ============================================================================

LargeKnobLookAndFeel::LargeKnobLookAndFeel()
{
    // Set default colors for text boxes
    setColour(juce::Slider::textBoxTextColourId, UIColors::textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, UIColors::backgroundDark);
    setColour(juce::Slider::textBoxOutlineColourId, UIColors::panelBorder);
}

void LargeKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosProportional, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider& slider)
{
    // Piano Black Minimalist Knob
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto center = bounds.getCentre();
    auto trackRadius = radius * 0.85f;

    // 1. Shadow for depth
    juce::DropShadow ds;
    ds.radius = 10;
    ds.offset = { 0, 4 };
    ds.colour = juce::Colours::black.withAlpha(0.5f);
    
    juce::Path shadowPath;
    shadowPath.addEllipse(center.x - trackRadius, center.y - trackRadius, trackRadius * 2, trackRadius * 2);
    ds.drawForPath(g, shadowPath);

    // 2. Piano Black Body
    // Main body - deep black
    g.setColour(juce::Colour(0xff0a0a0a)); 
    g.fillEllipse(center.x - trackRadius, center.y - trackRadius, trackRadius * 2, trackRadius * 2);

    // Glossy reflection (top half)
    juce::Path glossPath;
    glossPath.addEllipse(center.x - trackRadius * 0.9f, center.y - trackRadius * 0.9f, trackRadius * 1.8f, trackRadius * 1.0f);
    
    juce::ColourGradient glossGradient(
        juce::Colours::white.withAlpha(0.15f), center.x, center.y - trackRadius,
        juce::Colours::transparentWhite, center.x, center.y, false);
    
    g.setGradientFill(glossGradient);
    g.fillPath(glossPath);

    // Subtle rim highlight (bottom-right)
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawEllipse(center.x - trackRadius, center.y - trackRadius, trackRadius * 2, trackRadius * 2, 1.0f);

    // 3. Pointer (White Dot)
    float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    
    // Position dot slightly inside the rim
    float dotDistance = trackRadius * 0.75f; 
    float dotRadius = trackRadius * 0.08f; 
    
    float dotX = center.x + dotDistance * std::sin(currentAngle);
    float dotY = center.y - dotDistance * std::cos(currentAngle);
    
    // Dot shadow/glow
    juce::Path dotPath;
    dotPath.addEllipse(dotX - dotRadius, dotY - dotRadius, dotRadius * 2, dotRadius * 2);
    
    // Dot body
    g.setColour(juce::Colours::white);
    g.fillPath(dotPath);
}

// ============================================================================
// ParameterPanel Implementation
// ============================================================================

ParameterPanel::ParameterPanel()
{
    // ========== Pitch Correction Section ==========
    setupHeader(pitchCorrectionHeader_, LOC(kPitchCorrection));
    addAndMakeVisible(pitchCorrectionHeader_);

    setupLabel(retuneSpeedLabel_, LOC(kRetuneSpeed));
    retuneSpeedLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(retuneSpeedLabel_);

    setupLargeKnob(retuneSpeedSlider_, 0.0, 100.0, PitchControlConfig::kDefaultRetuneSpeedPercent, "%");
    retuneSpeedSlider_.getProperties().set("minimalKnob", true);
    retuneSpeedSlider_.onValueChange = [this] { onRetuneSpeedChanged(); };
    retuneSpeedSlider_.onDragStart = [this] { dragStartRetuneSpeed_ = static_cast<float>(retuneSpeedSlider_.getValue()); };
    retuneSpeedSlider_.onDragEnd = [this] {
        float newVal = static_cast<float>(retuneSpeedSlider_.getValue());
        if (std::abs(newVal - dragStartRetuneSpeed_) > 0.01f)
            listeners_.call([this, newVal](Listener& l) { l.parameterDragEnded(kParamRetuneSpeed, dragStartRetuneSpeed_, newVal); });
    };
    addAndMakeVisible(retuneSpeedSlider_);

    setupLabel(vibratoDepthLabel_, LOC(kVibratoDepth));
    vibratoDepthLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(vibratoDepthLabel_);

    setupLargeKnob(vibratoDepthSlider_, 0.0, 100.0, PitchControlConfig::kDefaultVibratoDepth, "%");
    vibratoDepthSlider_.getProperties().set("minimalKnob", true);
    vibratoDepthSlider_.onValueChange = [this] { onVibratoDepthChanged(); };
    vibratoDepthSlider_.onDragStart = [this] { dragStartVibratoDepth_ = static_cast<float>(vibratoDepthSlider_.getValue()); };
    vibratoDepthSlider_.onDragEnd = [this] {
        float newVal = static_cast<float>(vibratoDepthSlider_.getValue());
        if (std::abs(newVal - dragStartVibratoDepth_) > 0.01f)
            listeners_.call([this, newVal](Listener& l) { l.parameterDragEnded(kParamVibratoDepth, dragStartVibratoDepth_, newVal); });
    };
    addAndMakeVisible(vibratoDepthSlider_);

    setupLabel(vibratoRateLabel_, LOC(kVibratoRate));
    vibratoRateLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(vibratoRateLabel_);

    setupLargeKnob(vibratoRateSlider_, 3.0, 12.0, PitchControlConfig::kDefaultVibratoRateHz, " Hz");
    vibratoRateSlider_.getProperties().set("minimalKnob", true);
    vibratoRateSlider_.onValueChange = [this] { onVibratoRateChanged(); };
    vibratoRateSlider_.onDragStart = [this] { dragStartVibratoRate_ = static_cast<float>(vibratoRateSlider_.getValue()); };
    vibratoRateSlider_.onDragEnd = [this] {
        float newVal = static_cast<float>(vibratoRateSlider_.getValue());
        if (std::abs(newVal - dragStartVibratoRate_) > 0.01f)
            listeners_.call([this, newVal](Listener& l) { l.parameterDragEnded(kParamVibratoRate, dragStartVibratoRate_, newVal); });
    };
    addAndMakeVisible(vibratoRateSlider_);

    setupLabel(noteSplitLabel_, LOC(kNoteSplit));
    noteSplitLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noteSplitLabel_);

    setupLargeKnob(noteSplitSlider_,
                   PitchControlConfig::kMinNoteSplitCents,
                   PitchControlConfig::kMaxNoteSplitCents,
                   PitchControlConfig::kDefaultNoteSplitCents,
                   " cents");
    noteSplitSlider_.getProperties().set("minimalKnob", true);
    noteSplitSlider_.onValueChange = [this] { onNoteSplitChanged(); };
    noteSplitSlider_.onDragStart = [this] { dragStartNoteSplit_ = static_cast<float>(noteSplitSlider_.getValue()); };
    noteSplitSlider_.onDragEnd = [this] {
        float newVal = static_cast<float>(noteSplitSlider_.getValue());
        if (std::abs(newVal - dragStartNoteSplit_) > 0.01f)
            listeners_.call([this, newVal](Listener& l) { l.parameterDragEnded(kParamNoteSplit, dragStartNoteSplit_, newVal); });
    };
    addAndMakeVisible(noteSplitSlider_);

    // ========== Tools Section (Replaces Info Display) ==========
    setupHeader(toolsHeader_, LOC(kTools));
    addAndMakeVisible(toolsHeader_);

    autoTuneToolButton_ = std::make_unique<ToolIconButton>(0, "Auto", LOC(kAuto) + " (A)");
    autoTuneToolButton_->setRadioGroupId(1001);
    autoTuneToolButton_->setTextIcon("AUTO");
    autoTuneToolButton_->onClick = [this] { onToolClicked(0); };
    addAndMakeVisible(*autoTuneToolButton_);

    selectToolButton_ = std::make_unique<ToolIconButton>(1, "Select", LOC(kSelect) + " (Default)");
    selectToolButton_->setRadioGroupId(1001);
    selectToolButton_->setToggleState(true, juce::dontSendNotification);
    selectToolButton_->setIcon(ToolbarIcons::getSelectIcon(), true);
    selectToolButton_->onClick = [this] { onToolClicked(1); };
    addAndMakeVisible(*selectToolButton_);

    drawNoteToolButton_ = std::make_unique<ToolIconButton>(2, "DrawNote", LOC(kDrawNotes) + " (N)");
    drawNoteToolButton_->setRadioGroupId(1001);
    drawNoteToolButton_->setIcon(ToolbarIcons::getDrawNoteIcon(), false);
    drawNoteToolButton_->onClick = [this] { onToolClicked(2); };
    addAndMakeVisible(*drawNoteToolButton_);

    lineAnchorToolButton_ = std::make_unique<ToolIconButton>(3, "LineAnchor", LOC(kLineAnchor) + " (4)");
    lineAnchorToolButton_->setRadioGroupId(1001);
    lineAnchorToolButton_->setIcon(ToolbarIcons::getLineAnchorIcon(), false);
    lineAnchorToolButton_->onClick = [this] { onToolClicked(3); };
    addAndMakeVisible(*lineAnchorToolButton_);

    handDrawToolButton_ = std::make_unique<ToolIconButton>(4, "HandDraw", LOC(kHandDraw));
    handDrawToolButton_->setRadioGroupId(1001);
    handDrawToolButton_->setIcon(ToolbarIcons::getHandDrawIcon(), false);
    handDrawToolButton_->onClick = [this] { onToolClicked(4); };
    addAndMakeVisible(*handDrawToolButton_);

    splitNoteToolButton_ = std::make_unique<ToolIconButton>(5, "SplitNote", LOC(kSplitNote) + " (5)");
    splitNoteToolButton_->setRadioGroupId(1001);
    splitNoteToolButton_->setIcon(ToolbarIcons::getCutIcon(), false);
    splitNoteToolButton_->onClick = [this] { onToolClicked(5); };
    addAndMakeVisible(*splitNoteToolButton_);

    // Tool buttons are moved to PianoRoll (top-left compact toolbar).
    toolsHeader_.setVisible(false);
    autoTuneToolButton_->setVisible(false);
    selectToolButton_->setVisible(false);
    drawNoteToolButton_->setVisible(false);
    lineAnchorToolButton_->setVisible(false);
    handDrawToolButton_->setVisible(false);
    splitNoteToolButton_->setVisible(false);
}

ParameterPanel::~ParameterPanel()
{
    retuneSpeedSlider_.setLookAndFeel(nullptr);
    vibratoDepthSlider_.setLookAndFeel(nullptr);
    vibratoRateSlider_.setLookAndFeel(nullptr);
    noteSplitSlider_.setLookAndFeel(nullptr);
    f0MinSlider_.setLookAndFeel(nullptr);
    f0MaxSlider_.setLookAndFeel(nullptr);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    const auto& style = Theme::getActiveStyle();
    // 阴影边距：背景在 reduced(12) 区域内绘制，阴影在边距内渲染
    const float shadowMargin = 12.0f;
    auto bounds = getLocalBounds().toFloat().reduced(shadowMargin);

    // Background Shadow (if needed, though MainControlPanel usually handles the main shadow)
    UIColors::drawShadow(g, bounds);

    // Create rounded path for background and clipping
    juce::Path backgroundPath;
    backgroundPath.addRoundedRectangle(bounds, style.panelRadius);
    g.reduceClipRegion(backgroundPath);
    
    // Fill Background
    UIColors::fillPanelBackground(g, bounds, style.panelRadius);
    
    // Draw Frame
    UIColors::drawPanelFrame(g, bounds, style.panelRadius);
}

void ParameterPanel::resized()
{
    const auto themeId = Theme::getActiveTheme();
    const bool isBlueBreeze = (themeId == ThemeId::BlueBreeze);

    const int knobSize = isBlueBreeze ? 115 : 92;

    const int shadowMargin = 12;
    const int innerPadding = 8;
    auto mainArea = getLocalBounds().reduced(shadowMargin + innerPadding);

    const int headerHeight = 24;
    const int labelHeight = 20;
    const int spacing = 12;

    pitchCorrectionHeader_.setBounds(mainArea.removeFromTop(headerHeight));
    mainArea.removeFromTop(spacing);

    const int rowHeight = labelHeight + knobSize + 8;

    auto layoutKnobCell = [&](juce::Rectangle<int> area, juce::Label& label, juce::Slider& slider) {
        label.setBounds(area.removeFromTop(labelHeight));
        slider.setBounds(area.reduced(4, 0));
    };

    auto row1 = mainArea.removeFromTop(rowHeight);
    mainArea.removeFromTop(spacing);

    auto row2 = mainArea.removeFromTop(rowHeight);
    mainArea.removeFromTop(spacing);

    const int colWidth = row1.getWidth() / 2;

    layoutKnobCell(row1.removeFromLeft(colWidth), retuneSpeedLabel_, retuneSpeedSlider_);
    layoutKnobCell(row1, vibratoDepthLabel_, vibratoDepthSlider_);

    layoutKnobCell(row2.removeFromLeft(colWidth), vibratoRateLabel_, vibratoRateSlider_);
    layoutKnobCell(row2, noteSplitLabel_, noteSplitSlider_);

    toolsHeader_.setBounds(0, 0, 0, 0);
    if (autoTuneToolButton_) autoTuneToolButton_->setBounds(0, 0, 0, 0);
    if (selectToolButton_) selectToolButton_->setBounds(0, 0, 0, 0);
    if (drawNoteToolButton_) drawNoteToolButton_->setBounds(0, 0, 0, 0);
    if (lineAnchorToolButton_) lineAnchorToolButton_->setBounds(0, 0, 0, 0);
    if (handDrawToolButton_) handDrawToolButton_->setBounds(0, 0, 0, 0);
    if (splitNoteToolButton_) splitNoteToolButton_->setBounds(0, 0, 0, 0);
}

void ParameterPanel::applyTheme()
{
    pitchCorrectionHeader_.setColour(juce::Label::textColourId, UIColors::textPrimary);
    toolsHeader_.setColour(juce::Label::textColourId, UIColors::textPrimary);

    retuneSpeedLabel_.setColour(juce::Label::textColourId, UIColors::textSecondary);
    vibratoDepthLabel_.setColour(juce::Label::textColourId, UIColors::textSecondary);
    vibratoRateLabel_.setColour(juce::Label::textColourId, UIColors::textSecondary);
    noteSplitLabel_.setColour(juce::Label::textColourId, UIColors::textSecondary);

    largeKnobLookAndFeel_.setColour(juce::Slider::textBoxTextColourId, UIColors::textPrimary);
    largeKnobLookAndFeel_.setColour(juce::Slider::textBoxBackgroundColourId, UIColors::backgroundDark);
    largeKnobLookAndFeel_.setColour(juce::Slider::textBoxOutlineColourId, UIColors::panelBorder);

    resized();
    repaint();
}

void ParameterPanel::refreshLocalizedText()
{
    // 更新 Pitch Correction 部分
    pitchCorrectionHeader_.setText(LOC(kPitchCorrection), juce::dontSendNotification);
    retuneSpeedLabel_.setText(LOC(kRetuneSpeed), juce::dontSendNotification);
    vibratoDepthLabel_.setText(LOC(kVibratoDepth), juce::dontSendNotification);
    vibratoRateLabel_.setText(LOC(kVibratoRate), juce::dontSendNotification);
    noteSplitLabel_.setText(LOC(kNoteSplit), juce::dontSendNotification);
    
    // 更新 Tools 部分
    toolsHeader_.setText(LOC(kTools), juce::dontSendNotification);
    
    // 更新工具按钮 tooltip
    if (autoTuneToolButton_)
        autoTuneToolButton_->setTooltip(LOC(kAuto));
    if (selectToolButton_)
        selectToolButton_->setTooltip(LOC(kSelect));
    if (drawNoteToolButton_)
        drawNoteToolButton_->setTooltip(LOC(kDrawNotes));
    if (lineAnchorToolButton_)
        lineAnchorToolButton_->setTooltip(LOC(kLineAnchor));
    if (handDrawToolButton_)
        handDrawToolButton_->setTooltip(LOC(kHandDraw));
    if (splitNoteToolButton_)
        splitNoteToolButton_->setTooltip(LOC(kSplitNote) + " (5)");
    
    repaint();
}

void ParameterPanel::setupHeader(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(UIColors::getHeaderFont(14.0f)); // Bold header font
    label.setColour(juce::Label::textColourId, UIColors::textPrimary);
    label.setJustificationType(juce::Justification::centred);
}

void ParameterPanel::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(UIColors::getUIFont(12.0f));
    label.setColour(juce::Label::textColourId, UIColors::textSecondary);
}

void ParameterPanel::setupLargeKnob(juce::Slider& slider, double min, double max, double defaultVal, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    // Slightly larger textbox to improve readability.
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 28);
    slider.setRange(min, max, 0.01);
    slider.setValue(defaultVal);
    slider.setRotaryParameters(juce::degreesToRadians(210.0f), juce::degreesToRadians(510.0f), true);
    slider.setDoubleClickReturnValue(true, defaultVal);
    slider.setTextValueSuffix(suffix);
    slider.setLookAndFeel(&largeKnobLookAndFeel_);
}

void ParameterPanel::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void ParameterPanel::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void ParameterPanel::setActiveTool(int toolId)
{
    if (autoTuneToolButton_) autoTuneToolButton_->setToggleState(toolId == 0, juce::dontSendNotification);
    if (selectToolButton_) selectToolButton_->setToggleState(toolId == 1, juce::dontSendNotification);
    if (drawNoteToolButton_) drawNoteToolButton_->setToggleState(toolId == 2, juce::dontSendNotification);
    if (lineAnchorToolButton_) lineAnchorToolButton_->setToggleState(toolId == 3, juce::dontSendNotification);
    if (handDrawToolButton_) handDrawToolButton_->setToggleState(toolId == 4, juce::dontSendNotification);
    if (splitNoteToolButton_) splitNoteToolButton_->setToggleState(toolId == 5, juce::dontSendNotification);
}

// Getters and Setters

void ParameterPanel::setRetuneSpeed(float speed)
{
    retuneSpeedSlider_.setValue(speed, juce::dontSendNotification);
}

float ParameterPanel::getRetuneSpeed() const
{
    return static_cast<float>(retuneSpeedSlider_.getValue());
}

void ParameterPanel::setVibratoDepth(float value)
{
    vibratoDepthSlider_.setValue(value, juce::dontSendNotification);
}

float ParameterPanel::getVibratoDepth() const
{
    return static_cast<float>(vibratoDepthSlider_.getValue());
}

void ParameterPanel::setVibratoRate(float value)
{
    vibratoRateSlider_.setValue(value, juce::dontSendNotification);
}

float ParameterPanel::getVibratoRate() const
{
    return static_cast<float>(vibratoRateSlider_.getValue());
}

void ParameterPanel::setNoteSplit(float value)
{
    noteSplitSlider_.setValue(value, juce::dontSendNotification);
}

float ParameterPanel::getNoteSplit() const
{
    return static_cast<float>(noteSplitSlider_.getValue());
}

void ParameterPanel::setF0Min(float value)
{
    f0MinSlider_.setValue(value, juce::dontSendNotification);
}

float ParameterPanel::getF0Min() const
{
    return static_cast<float>(f0MinSlider_.getValue());
}

void ParameterPanel::setF0Max(float value)
{
    f0MaxSlider_.setValue(value, juce::dontSendNotification);
}

float ParameterPanel::getF0Max() const
{
    return static_cast<float>(f0MaxSlider_.getValue());
}

void ParameterPanel::onRetuneSpeedChanged()
{
    listeners_.call([this](Listener& l) { l.retuneSpeedChanged(static_cast<float>(retuneSpeedSlider_.getValue())); });
}

void ParameterPanel::onVibratoDepthChanged()
{
    listeners_.call([this](Listener& l) { l.vibratoDepthChanged(static_cast<float>(vibratoDepthSlider_.getValue())); });
}

void ParameterPanel::onVibratoRateChanged()
{
    listeners_.call([this](Listener& l) { l.vibratoRateChanged(static_cast<float>(vibratoRateSlider_.getValue())); });
}

void ParameterPanel::onNoteSplitChanged()
{
    listeners_.call([this](Listener& l) { l.noteSplitChanged(static_cast<float>(noteSplitSlider_.getValue())); });
}

void ParameterPanel::onToolClicked(int toolId)
{
    listeners_.call([this, toolId](Listener& l) { l.toolSelected(toolId); });
}

} // namespace OpenTune

