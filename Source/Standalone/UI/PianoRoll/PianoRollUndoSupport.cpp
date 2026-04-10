#include "PianoRollUndoSupport.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <set>
#include <tuple>
#include <functional>

namespace OpenTune {

namespace {
    // Compute the actual affected frame range by comparing before/after segment lists
    void computeSegmentDiffRange(
        const std::vector<CorrectedSegmentsChangeAction::SegmentSnapshot>& before,
        const std::vector<CorrectedSegmentsChangeAction::SegmentSnapshot>& after,
        int& outStart,
        int& outEnd)
    {
        // Build fingerprint sets for fast lookup
        struct SegFP {
            int start;
            int end;
            size_t hash;
            bool operator<(const SegFP& o) const {
                return std::tie(start, end, hash) < std::tie(o.start, o.end, o.hash);
            }
        };
        
        auto makeFingerprint = [](const CorrectedSegmentsChangeAction::SegmentSnapshot& seg) -> SegFP {
            size_t h = 0;
            if (!seg.f0Data.empty()) {
                h ^= std::hash<float>{}(seg.f0Data.front());
                h ^= std::hash<float>{}(seg.f0Data.back()) << 1;
                h ^= std::hash<size_t>{}(seg.f0Data.size()) << 2;
            }
            h ^= std::hash<int>{}(static_cast<int>(seg.source)) << 3;
            return {seg.startFrame, seg.endFrame, h};
        };
        
        std::set<SegFP> beforeSet, afterSet;
        for (const auto& seg : before) beforeSet.insert(makeFingerprint(seg));
        for (const auto& seg : after) afterSet.insert(makeFingerprint(seg));
        
        // Find segments that differ
        int diffStart = INT_MAX, diffEnd = -1;
        for (const auto& fp : beforeSet) {
            if (afterSet.find(fp) == afterSet.end()) {
                diffStart = std::min(diffStart, fp.start);
                diffEnd = std::max(diffEnd, fp.end);
            }
        }
        for (const auto& fp : afterSet) {
            if (beforeSet.find(fp) == beforeSet.end()) {
                diffStart = std::min(diffStart, fp.start);
                diffEnd = std::max(diffEnd, fp.end);
            }
        }
        
        if (diffEnd >= diffStart && diffStart != INT_MAX) {
            outStart = diffStart;
            outEnd = diffEnd;
        }
        // else keep the passed-in defaults (full range)
    }
    
    bool nearlyEqualFloat(float a, float b, float epsilon = 1e-6f) {
        return std::abs(a - b) <= epsilon;
    }
}

bool PianoRollUndoSupport::notesEquivalent(const std::vector<Note>& a, const std::vector<Note>& b)
// 比较两个音符列表是否等价（时间、音高、偏移、颤音参数、力度）
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.startTime != y.startTime) return false;
        if (x.endTime != y.endTime) return false;
        if (!nearlyEqualFloat(x.pitch, y.pitch)) return false;
        if (!nearlyEqualFloat(x.originalPitch, y.originalPitch)) return false;
        if (!nearlyEqualFloat(x.pitchOffset, y.pitchOffset)) return false;
        if (!nearlyEqualFloat(x.retuneSpeed, y.retuneSpeed)) return false;
        if (!nearlyEqualFloat(x.vibratoDepth, y.vibratoDepth)) return false;
        if (!nearlyEqualFloat(x.vibratoRate, y.vibratoRate)) return false;
        if (!nearlyEqualFloat(x.velocity, y.velocity)) return false;
        if (x.isVoiced != y.isVoiced) return false;
    }
    return true;
}

void PianoRollUndoSupport::beginTransaction(const juce::String& description)
{
    jassert(!transactionActive_);

    transactionDescription_ = description;
    transactionBeforeNotes_ = ctx_.getNotesCopy ? ctx_.getNotesCopy() : std::vector<Note>();

    auto curve = ctx_.getPitchCurve ? ctx_.getPitchCurve() : nullptr;
    transactionBeforeSegments_ = CorrectedSegmentsChangeAction::captureSegments(curve);
    if (curve) {
        auto snap = curve->getSnapshot();
        transactionBeforeAnchors_ = snap ? snap->getAnchorGroups() : std::vector<AnchorGroup>();
    } else {
        transactionBeforeAnchors_.clear();
    }

    transactionActive_ = true;
}

bool PianoRollUndoSupport::isTransactionActive() const noexcept
{
    return transactionActive_;
}

void PianoRollUndoSupport::clearTransaction()
{
    transactionDescription_.clear();
    transactionBeforeNotes_.clear();
    transactionBeforeSegments_.clear();
    transactionBeforeAnchors_.clear();
    transactionActive_ = false;
}

void PianoRollUndoSupport::commitTransaction()
{
    if (!transactionActive_) {
        jassertfalse;
        return;
    }

    auto afterNotes = ctx_.getNotesCopy ? ctx_.getNotesCopy() : std::vector<Note>();
    const bool notesChanged = !notesEquivalent(transactionBeforeNotes_, afterNotes);

    auto curve = ctx_.getPitchCurve ? ctx_.getPitchCurve() : nullptr;
    auto afterSegments = CorrectedSegmentsChangeAction::captureSegments(curve);
    const bool segmentsChanged = !CorrectedSegmentsChangeAction::snapshotsEquivalent(
        transactionBeforeSegments_, afterSegments);

    // Check if anchor groups changed
    std::vector<AnchorGroup> afterAnchors;
    if (curve) {
        auto snap = curve->getSnapshot();
        afterAnchors = snap ? snap->getAnchorGroups() : std::vector<AnchorGroup>();
    }
    const bool anchorsChanged = (transactionBeforeAnchors_.size() != afterAnchors.size())
        || [&]() {
            for (size_t i = 0; i < transactionBeforeAnchors_.size(); ++i) {
                if (transactionBeforeAnchors_[i].points.size() != afterAnchors[i].points.size())
                    return true;
                for (size_t j = 0; j < transactionBeforeAnchors_[i].points.size(); ++j) {
                    const auto& a = transactionBeforeAnchors_[i].points[j];
                    const auto& b = afterAnchors[i].points[j];
                    if (std::abs(a.time - b.time) > 1e-9 || std::abs(a.pitch - b.pitch) > 1e-4f)
                        return true;
                }
            }
            return false;
        }();

    if (notesChanged || segmentsChanged || anchorsChanged) {
        const uint64_t clipId = ctx_.getCurrentClipId ? ctx_.getCurrentClipId() : 0;
        const int trackId = ctx_.getCurrentTrackId ? ctx_.getCurrentTrackId() : 0;
        auto* processor = ctx_.getProcessor ? ctx_.getProcessor() : nullptr;

        // Anchor changes use AnchorChangeAction which restores both anchors and segments
        if (anchorsChanged && curve) {
            if (notesChanged && processor) {
                auto compound = std::make_unique<CompoundUndoAction>(transactionDescription_);
                compound->addAction(std::make_unique<NotesChangeAction>(
                    *processor, trackId, clipId,
                    transactionBeforeNotes_, afterNotes, transactionDescription_));
                compound->addAction(std::make_unique<AnchorChangeAction>(
                    clipId,
                    transactionBeforeAnchors_, afterAnchors,
                    transactionBeforeSegments_, afterSegments,
                    curve, transactionDescription_));
                if (auto* um = getCurrentUndoManager())
                    um->addAction(std::move(compound));
            } else {
                if (auto* um = getCurrentUndoManager())
                    um->addAction(std::make_unique<AnchorChangeAction>(
                        clipId,
                        transactionBeforeAnchors_, afterAnchors,
                        transactionBeforeSegments_, afterSegments,
                        curve, transactionDescription_));
            }
        } else if (notesChanged && segmentsChanged && processor && curve) {
            auto action = std::make_unique<CompoundUndoAction>(transactionDescription_);
            action->addAction(std::make_unique<NotesChangeAction>(
                *processor,
                trackId,
                clipId,
                transactionBeforeNotes_,
                afterNotes,
                transactionDescription_));

            int affStart = 0, affEnd = static_cast<int>(curve->size()) - 1;
            computeSegmentDiffRange(transactionBeforeSegments_, afterSegments, affStart, affEnd);

            auto curveAction = CorrectedSegmentsChangeAction::createForCurve(
                clipId,
                transactionBeforeSegments_,
                afterSegments,
                curve,
                affStart,
                affEnd,
                transactionDescription_);
            if (curveAction) {
                action->addAction(std::move(curveAction));
            }

            if (auto* um = getCurrentUndoManager()) {
                um->addAction(std::move(action));
            }
        } else if (notesChanged && processor) {
            if (auto* um = getCurrentUndoManager()) {
                um->addAction(std::make_unique<NotesChangeAction>(
                    *processor,
                    trackId,
                    clipId,
                    transactionBeforeNotes_,
                    afterNotes,
                    transactionDescription_));
            }
        } else if (segmentsChanged && curve) {
            int affStart = 0, affEnd = static_cast<int>(curve->size()) - 1;
            computeSegmentDiffRange(transactionBeforeSegments_, afterSegments, affStart, affEnd);

            if (auto* um = getCurrentUndoManager()) {
                um->addAction(CorrectedSegmentsChangeAction::createForCurve(
                    clipId,
                    transactionBeforeSegments_,
                    afterSegments,
                    curve,
                    affStart,
                    affEnd,
                    transactionDescription_));
            }
        }
    }

    clearTransaction();
}

PianoRollUndoSupport::PianoRollUndoSupport(Context context)
    : ctx_(std::move(context))
{
}

UndoManager* PianoRollUndoSupport::getCurrentUndoManager() noexcept
{
    return ctx_.getUndoManager ? ctx_.getUndoManager() : nullptr;
}

} // namespace OpenTune
