#include "ArrangementViewComponent.h"

#include <algorithm>
#include <limits>

namespace OpenTune {

bool ArrangementViewComponent::isClipSelected(int trackId, uint64_t clipId) const
{
    return selectedClips_.count(ClipSelectionKey{ trackId, clipId }) > 0;
}

void ArrangementViewComponent::toggleClipSelection(int trackId, uint64_t clipId, int clipIndex)
{
    juce::ignoreUnused(clipIndex);

    ClipSelectionKey key{ trackId, clipId };
    auto it = selectedClips_.find(key);
    if (it != selectedClips_.end())
    {
        if (selectedClips_.size() > 1)
            selectedClips_.erase(it);
    }
    else
    {
        selectedClips_.insert(key);
    }
}

void ArrangementViewComponent::clearClipSelection()
{
    selectedClips_.clear();
    hasShiftAnchor_ = false;
}

void ArrangementViewComponent::syncSelectionFromProcessor(int trackId)
{
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS)
        return;

    selectedTrack_ = trackId;
    selectedClip_ = processor_.getSelectedClip(trackId);
    if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(trackId))
        selectedClipId_ = processor_.getClipId(trackId, selectedClip_);
    else
        selectedClipId_ = 0;

    clearClipSelection();
    if (selectedClipId_ != 0)
        selectedClips_.insert(ClipSelectionKey{ selectedTrack_, selectedClipId_ });

    repaint();
}

void ArrangementViewComponent::selectClipsInRange(const ClipSelectionKey& from, const ClipSelectionKey& to)
{
    if (from.trackId == to.trackId)
    {
        double fromStart = processor_.getClipStartSecondsById(from.trackId, from.clipId);
        double toStart = processor_.getClipStartSecondsById(to.trackId, to.clipId);
        double minTime = std::min(fromStart, toStart);
        double maxTime = std::max(fromStart, toStart);

        for (int i = 0; i < processor_.getNumClips(from.trackId); ++i)
        {
            uint64_t clipId = processor_.getClipId(from.trackId, i);
            double clipStart = processor_.getClipStartSeconds(from.trackId, i);
            if (clipStart >= minTime && clipStart <= maxTime)
                selectedClips_.insert(ClipSelectionKey{ from.trackId, clipId });
        }
    }
    else
    {
        int minTrack = std::min(from.trackId, to.trackId);
        int maxTrack = std::max(from.trackId, to.trackId);
        double fromStart = processor_.getClipStartSecondsById(from.trackId, from.clipId);
        double toStart = processor_.getClipStartSecondsById(to.trackId, to.clipId);
        double minTime = std::min(fromStart, toStart);
        double maxTime = std::max(fromStart, toStart);

        for (int trackId = minTrack; trackId <= maxTrack; ++trackId)
        {
            for (int i = 0; i < processor_.getNumClips(trackId); ++i)
            {
                uint64_t clipId = processor_.getClipId(trackId, i);
                double clipStart = processor_.getClipStartSeconds(trackId, i);
                if (clipStart >= minTime && clipStart <= maxTime)
                    selectedClips_.insert(ClipSelectionKey{ trackId, clipId });
            }
        }
    }
}

void ArrangementViewComponent::copySelectedClips()
{
    if (selectedClips_.empty())
        return;

    clipboard_.clear();
    clipboardReferenceTime_ = std::numeric_limits<double>::max();

    for (const auto& sel : selectedClips_)
    {
        OpenTuneAudioProcessor::ClipSnapshot snap;
        if (processor_.getClipSnapshot(sel.trackId, sel.clipId, snap))
        {
            double clipStart = snap.startSeconds;
            if (clipStart < clipboardReferenceTime_)
                clipboardReferenceTime_ = clipStart;

            clipboard_.push_back({ sel.trackId, std::move(snap), 0.0 });
        }
    }

    for (auto& item : clipboard_)
        item.relativeOffset = item.snapshot.startSeconds - clipboardReferenceTime_;
}

void ArrangementViewComponent::cutSelectedClips()
{
    if (selectedClips_.empty())
        return;

    copySelectedClips();

    std::vector<std::tuple<int, uint64_t, int>> toDelete;
    for (const auto& sel : selectedClips_)
    {
        int clipIndex = processor_.findClipIndexById(sel.trackId, sel.clipId);
        if (clipIndex >= 0)
            toDelete.emplace_back(sel.trackId, sel.clipId, clipIndex);
    }

    std::sort(toDelete.begin(), toDelete.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) > std::get<0>(b);

        return std::get<2>(a) > std::get<2>(b);
    });

    std::unique_ptr<CompoundUndoAction> compoundAction;
    if (toDelete.size() > 1)
        compoundAction = std::make_unique<CompoundUndoAction>("Cut Multiple Clips");

    for (const auto& [trackId, clipId, clipIndex] : toDelete)
    {
        OpenTuneAudioProcessor::ClipSnapshot snap;
        int deletedIndex = -1;
        if (processor_.deleteClipById(trackId, clipId, &snap, &deletedIndex))
        {
            auto action = std::make_unique<ClipDeleteAction>(processor_, trackId, clipId, deletedIndex, std::move(snap));
            if (compoundAction)
                compoundAction->addAction(std::move(action));
            else
                processor_.getUndoManager().addAction(std::move(action));
        }
    }

    if (compoundAction && !compoundAction->isEmpty())
        processor_.getUndoManager().addAction(std::move(compoundAction));

    clearClipSelection();
    selectedClip_ = processor_.getSelectedClip(selectedTrack_);
    if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(selectedTrack_))
        selectedClipId_ = processor_.getClipId(selectedTrack_, selectedClip_);
    else
        selectedClipId_ = 0;

    repaint();
}

void ArrangementViewComponent::pasteClips()
{
    if (clipboard_.empty())
        return;

    double playheadTime = processor_.getPosition();
    int targetTrack = processor_.getActiveTrackId();
    if (targetTrack < 0 || targetTrack >= OpenTuneAudioProcessor::MAX_TRACKS)
        return;

    clearClipSelection();

    std::unique_ptr<CompoundUndoAction> compoundAction;
    if (clipboard_.size() > 1)
        compoundAction = std::make_unique<CompoundUndoAction>("Paste Clips");

    for (const auto& item : clipboard_)
    {
        double newStart = playheadTime + item.relativeOffset;
        if (newStart < 0.0)
            newStart = 0.0;

        int insertIndex = 0;
        int numClips = processor_.getNumClips(targetTrack);
        for (int i = 0; i < numClips; ++i)
        {
            double clipStart = processor_.getClipStartSeconds(targetTrack, i);
            if (newStart >= clipStart)
                insertIndex = i + 1;
        }

        ClipSnapshot pasteSnap = item.snapshot;
        pasteSnap.startSeconds = newStart;

        if (processor_.insertClipSnapshot(targetTrack, insertIndex, pasteSnap, 0))
        {
            uint64_t newClipId = processor_.getClipId(targetTrack, insertIndex);
            selectedClips_.insert(ClipSelectionKey{ targetTrack, newClipId });
            auto action = std::make_unique<ClipCreateAction>(processor_, targetTrack, newClipId);
            if (compoundAction)
                compoundAction->addAction(std::move(action));
            else
                processor_.getUndoManager().addAction(std::move(action));
        }
    }

    if (compoundAction && !compoundAction->isEmpty())
        processor_.getUndoManager().addAction(std::move(compoundAction));

    if (!selectedClips_.empty())
    {
        selectedTrack_ = targetTrack;
        selectedClip_ = processor_.getSelectedClip(targetTrack);
        if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(targetTrack))
            selectedClipId_ = processor_.getClipId(targetTrack, selectedClip_);
    }

    listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
    repaint();
}

void ArrangementViewComponent::deleteSelectedClips()
{
    if (selectedClips_.empty())
    {
        if (selectedTrack_ >= 0 && selectedTrack_ < OpenTuneAudioProcessor::MAX_TRACKS)
        {
            uint64_t clipId = selectedClipId_;
            if (clipId == 0 && selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(selectedTrack_))
                clipId = processor_.getClipId(selectedTrack_, selectedClip_);

            OpenTuneAudioProcessor::ClipSnapshot snap;
            int deletedIndex = -1;
            if (clipId != 0 && processor_.deleteClipById(selectedTrack_, clipId, &snap, &deletedIndex))
            {
                processor_.getUndoManager().addAction(std::make_unique<ClipDeleteAction>(
                    processor_, selectedTrack_, clipId, deletedIndex, std::move(snap)));
                selectedClip_ = processor_.getSelectedClip(selectedTrack_);
                if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(selectedTrack_))
                    selectedClipId_ = processor_.getClipId(selectedTrack_, selectedClip_);
                else
                    selectedClipId_ = 0;

                repaint();
            }
        }
        return;
    }

    std::vector<std::tuple<int, uint64_t, int>> toDelete;
    for (const auto& sel : selectedClips_)
    {
        int clipIndex = processor_.findClipIndexById(sel.trackId, sel.clipId);
        if (clipIndex >= 0)
            toDelete.emplace_back(sel.trackId, sel.clipId, clipIndex);
    }

    std::sort(toDelete.begin(), toDelete.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) > std::get<0>(b);

        return std::get<2>(a) > std::get<2>(b);
    });

    std::unique_ptr<CompoundUndoAction> compoundAction;
    if (toDelete.size() > 1)
        compoundAction = std::make_unique<CompoundUndoAction>("Delete Multiple Clips");

    for (const auto& [trackId, clipId, clipIndex] : toDelete)
    {
        OpenTuneAudioProcessor::ClipSnapshot snap;
        int deletedIndex = -1;
        if (processor_.deleteClipById(trackId, clipId, &snap, &deletedIndex))
        {
            auto action = std::make_unique<ClipDeleteAction>(processor_, trackId, clipId, deletedIndex, std::move(snap));
            if (compoundAction)
                compoundAction->addAction(std::move(action));
            else
                processor_.getUndoManager().addAction(std::move(action));
        }
    }

    if (compoundAction && !compoundAction->isEmpty())
        processor_.getUndoManager().addAction(std::move(compoundAction));

    clearClipSelection();
    selectedClip_ = processor_.getSelectedClip(selectedTrack_);
    if (selectedClip_ >= 0 && selectedClip_ < processor_.getNumClips(selectedTrack_))
        selectedClipId_ = processor_.getClipId(selectedTrack_, selectedClip_);
    else
        selectedClipId_ = 0;

    repaint();
}

void ArrangementViewComponent::selectAllClipsInTrack(int trackId)
{
    if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS)
        return;

    clearClipSelection();

    int numClips = processor_.getNumClips(trackId);
    for (int i = 0; i < numClips; ++i)
    {
        uint64_t clipId = processor_.getClipId(trackId, i);
        selectedClips_.insert(ClipSelectionKey{ trackId, clipId });
    }

    if (!selectedClips_.empty())
    {
        selectedTrack_ = trackId;
        selectedClip_ = numClips > 0 ? 0 : -1;
        selectedClipId_ = numClips > 0 ? processor_.getClipId(trackId, 0) : 0;
    }

    listeners_.call([this](Listener& l) { l.clipSelectionChanged(selectedTrack_, selectedClip_); });
    repaint();
}

} // namespace OpenTune
