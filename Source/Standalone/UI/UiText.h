#pragma once

#include <juce_core/juce_core.h>
#include "../../Utils/LocalizationManager.h"

namespace OpenTune {
namespace UiText {

inline juce::String pianoRollToolSelect()
{
    return LOC(kMouseSelectTool) + "\t[1]";
}

inline juce::String pianoRollToolDrawNote()
{
    return LOC(kDrawNoteTool) + "\t[2]";
}

inline juce::String pianoRollToolLineAnchor()
{
    return LOC(kLineAnchorTool) + "\t[3]";
}

inline juce::String pianoRollToolHandDraw()
{
    return LOC(kHandDrawTool) + "\t[4]";
}

inline juce::String pianoRollToolSplitNote()
{
    return LOC(kSplitNoteTool) + "\t[5]";
}

} // namespace UiText
} // namespace OpenTune
