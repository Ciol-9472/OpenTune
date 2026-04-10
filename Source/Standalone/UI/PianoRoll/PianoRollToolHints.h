#pragma once

#include <juce_core/juce_core.h>
#include <vector>

#include "../ToolIds.h"
#include "../../../Utils/LocalizationManager.h"

namespace OpenTune {

/** 钢琴卷帘当前工具的操作说明（多行），用于左下角半透明提示。 */
inline std::vector<juce::String> getPianoRollToolHintLines(ToolId tool)
{
    std::vector<juce::String> lines;
    switch (tool) {
        case ToolId::Select:
            lines.push_back(LOC(kPianoRollHintSelect1));
            lines.push_back(LOC(kPianoRollHintSelect2));
            lines.push_back(LOC(kPianoRollHintSelect3));
            lines.push_back(LOC(kPianoRollHintSelect4));
            break;
        case ToolId::DrawNote:
            lines.push_back(LOC(kPianoRollHintDrawNote1));
            lines.push_back(LOC(kPianoRollHintDrawNote2));
            lines.push_back(LOC(kPianoRollHintDrawNote3));
            break;
        case ToolId::LineAnchor:
            lines.push_back(LOC(kPianoRollHintAnchor1));
            lines.push_back(LOC(kPianoRollHintAnchor2));
            lines.push_back(LOC(kPianoRollHintAnchor3));
            lines.push_back(LOC(kPianoRollHintAnchor4));
            lines.push_back(LOC(kPianoRollHintAnchor5));
            break;
        case ToolId::HandDraw:
            lines.push_back(LOC(kPianoRollHintHandDraw1));
            lines.push_back(LOC(kPianoRollHintHandDraw2));
            lines.push_back(LOC(kPianoRollHintHandDraw3));
            break;
        case ToolId::SplitNote:
            lines.push_back(LOC(kPianoRollHintSplit1));
            lines.push_back(LOC(kPianoRollHintSplit2));
            break;
        case ToolId::AutoTune:
            lines.push_back(LOC(kPianoRollHintAuto1));
            lines.push_back(LOC(kPianoRollHintAuto2));
            break;
        default:
            break;
    }
    return lines;
}

} // namespace OpenTune
