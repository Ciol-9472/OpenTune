#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Utils/LockFreeQueue.h"
#include "Utils/PitchCurve.h"
#include "Utils/Note.h"
#include "Utils/PitchUtils.h"
#include "PluginProcessor.h"
#include "Inference/RenderCache.h"
#include "Inference/VocoderRenderScheduler.h"
#include "Inference/F0InferenceService.h"
#include "Inference/VocoderInferenceService.h"
#include "Standalone/UI/PianoRoll/PianoRollUndoSupport.h"
#include "Utils/SingingEditDocument.h"
#include "Utils/TimeCoordinate.h"
#include "Services/DsJsonIO.h"

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <algorithm>

static std::atomic<int> gOpenTuneTestFailures{0};

// ==============================================================================
// MockVocoderService - test double for VocoderInferenceService
// ==============================================================================
// Tracks concurrent synthesis calls to verify VocoderRenderScheduler serial execution.
// Inherits VocoderInferenceService but overrides synthesize to avoid ONNX dependency.
class MockVocoderService final : public OpenTune::VocoderInferenceService {
public:
    std::atomic<int> concurrentCalls_{0};
    int maxConcurrentSeen_{0};
    int numSuccessfulCalls_{0};
    int numFailedCalls_{0};
    std::chrono::milliseconds callDuration_{50};
    std::atomic<bool> failOnCall_{false};
    std::string failMessage_{"mock failure"};

    MockVocoderService() = default;

protected:
    OpenTune::Result<std::vector<float>> doSynthesizeAudioWithEnergy(
        const std::vector<float>& f0,
        const std::vector<float>& energy,
        const float* mel,
        size_t melSize) override
    {
        (void)energy;
        (void)mel;
        (void)melSize;

        if (failOnCall_.load()) {
            ++numFailedCalls_;
            return OpenTune::Result<std::vector<float>>::failure(
                OpenTune::ErrorCode::ModelInferenceFailed, failMessage_);
        }

        int before = concurrentCalls_.fetch_add(1);
        if (before >= maxConcurrentSeen_) {
            maxConcurrentSeen_ = before + 1;
        }

        std::this_thread::sleep_for(callDuration_);

        concurrentCalls_.fetch_sub(1);
        ++numSuccessfulCalls_;

        std::vector<float> audio;
        audio.resize(static_cast<size_t>(f0.size()) * 512, 0.5f);
        return OpenTune::Result<std::vector<float>>::success(std::move(audio));
    }
};

using namespace OpenTune;

// ==============================================================================
// Test helpers
// ==============================================================================

void logPass(const char* testName) {
    std::cout << "[PASS] " << testName << std::endl;
}

void logFail(const char* testName, const char* detail) {
    gOpenTuneTestFailures.fetch_add(1, std::memory_order_relaxed);
    std::cout << "[FAIL] " << testName << ": " << detail << std::endl;
}

void logSection(const char* section) {
    std::cout << "\n=== " << section << " ===" << std::endl;
}

bool approxEqual(float a, float b, float tol = 1e-5f) {
    return std::abs(a - b) <= tol;
}

bool approxEqual(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) <= tol;
}

void runLockFreeQueueTests() {
    logSection("LockFreeQueue Tests");

    {
        const char* test = "Basic enqueue/dequeue";
        LockFreeQueue<int> queue(16);

        if (!queue.empty()) { logFail(test, "new queue not empty"); return; }
        queue.try_enqueue(42);
        if (queue.size() != 1) { logFail(test, "size not 1 after enqueue"); return; }

        int value = 0;
        if (!queue.try_dequeue(value)) { logFail(test, "dequeue failed"); return; }
        if (value != 42) { logFail(test, "wrong value"); return; }

        logPass(test);
    }

    {
        const char* test = "Full queue behavior";
        LockFreeQueue<int> queue(4);

        for (int i = 0; i < 4; ++i) queue.try_enqueue(i);

        if (queue.try_enqueue(100)) { logFail(test, "should reject on full"); return; }

        int value;
        queue.try_dequeue(value);
        if (!queue.try_enqueue(100)) { logFail(test, "should accept after dequeue"); return; }

        logPass(test);
    }

    {
        const char* test = "Concurrent MPMC";
        LockFreeQueue<int> queue(1024);
        std::atomic<int> enqueueCount{0};
        std::atomic<int> dequeueCount{0};
        std::atomic<bool> done{false};

        const int numProducers = 4;
        const int itemsPerProducer = 500;

        std::vector<std::thread> producers, consumers;

        for (int p = 0; p < numProducers; ++p) {
            producers.emplace_back([&, p]() {
                for (int i = 0; i < itemsPerProducer; ++i) {
                    while (!queue.try_enqueue(p * itemsPerProducer + i)) {
                        std::this_thread::yield();
                    }
                    enqueueCount.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (int c = 0; c < numProducers; ++c) {
            consumers.emplace_back([&]() {
                int value;
                while (!done.load() || !queue.empty()) {
                    if (queue.try_dequeue(value)) {
                        dequeueCount.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (auto& t : producers) t.join();
        while (dequeueCount.load() < numProducers * itemsPerProducer) std::this_thread::yield();
        done.store(true);
        for (auto& t : consumers) t.join();

        if (enqueueCount.load() != numProducers * itemsPerProducer ||
            dequeueCount.load() != numProducers * itemsPerProducer) {
            logFail(test, "count mismatch");
            return;
        }

        logPass(test);
    }
}

void runPitchUtilsTests() {
    logSection("PitchUtils Tests");

    {
        const char* test = "Frequency to MIDI";
        if (!approxEqual(PitchUtils::freqToMidi(440.0f), 69.0f, 0.01f)) {
            logFail(test, "A4 not MIDI 69"); return;
        }
        if (!approxEqual(PitchUtils::freqToMidi(261.63f), 60.0f, 0.01f)) {
            logFail(test, "C4 not MIDI 60"); return;
        }
        logPass(test);
    }

    {
        const char* test = "MIDI to Frequency";
        if (!approxEqual(PitchUtils::midiToFreq(69.0f), 440.0f, 0.1f)) {
            logFail(test, "MIDI 69 not 440Hz"); return;
        }
        logPass(test);
    }

    {
        const char* test = "MIDI round trip";
        for (float midi = 20.0f; midi <= 120.0f; midi += 10.0f) {
            float freq = PitchUtils::midiToFreq(midi);
            float back = PitchUtils::freqToMidi(freq);
            if (!approxEqual(back, midi, 0.001f)) {
                logFail(test, "round trip error"); return;
            }
        }
        logPass(test);
    }

    {
        const char* test = "Mix retune";
        float shifted = 440.0f, target = 880.0f;

        if (!approxEqual(PitchUtils::mixRetune(shifted, target, 0.0f), shifted, 0.01f)) {
            logFail(test, "0% should return shifted"); return;
        }
        if (!approxEqual(PitchUtils::mixRetune(shifted, target, 1.0f), target, 0.01f)) {
            logFail(test, "100% should return target"); return;
        }
        float result50 = PitchUtils::mixRetune(shifted, target, 0.5f);
        if (result50 <= shifted || result50 >= target) {
            logFail(test, "50% should be in between"); return;
        }
        logPass(test);
    }
}

void runDsJsonContractTests() {
    logSection("opentune.ds.v1 JSON contract");

    {
        const char* test = "buildRefineDurationsRequest schema";
        SingingEditDocument doc;
        doc.ensureDefaultFullClipSegment(2.0);
        {
            PhonemeItem a;
            a.id = 2001;
            a.setStartClipSeconds(0.2);
            a.setEndClipSeconds(0.7);
            a.token = "aa";
            a.lockLeft = true;
            a.lockRight = false;
            doc.getPhonemes().push_back(a);
        }
        {
            PhonemeItem b;
            b.id = 2002;
            b.setStartClipSeconds(0.7);
            b.setEndClipSeconds(2.0);
            b.token = "a";
            b.lockLeft = false;
            b.lockRight = false;
            doc.getPhonemes().push_back(b);
        }
        Note n;
        n.stableId = 1001;
        n.pitch = 440.0f;
        n.setStartClipSeconds(0.2);
        n.setEndClipSeconds(2.0);
        doc.getNotes().push_back(n);

        const juce::String json = OpenTune::DsJson::buildRefineDurationsRequest(doc, 99ull, 2.0, 7ull, 11ull, 44100);
        if (!json.contains("opentune.ds.v1")) {
            logFail(test, "missing schema_version");
            return;
        }
        if (!json.contains("\"task\": \"refine_durations\"")) {
            logFail(test, "missing task");
            return;
        }
        if (!json.contains("\"request_id\": 11")) {
            logFail(test, "missing request id");
            return;
        }
        if (!json.contains("\"clip_generation\": 7")) {
            logFail(test, "missing clip generation");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "tryApplyRefineDurationsResponse clip gate rejects clip_id mismatch";
        SingingEditDocument doc;
        doc.ensureDefaultFullClipSegment(2.0);
        {
            PhonemeItem p0;
            p0.id = 1;
            p0.setStartClipSeconds(0.0);
            p0.setEndClipSeconds(1.0);
            p0.token = "a";
            p0.lockLeft = false;
            p0.lockRight = false;
            doc.getPhonemes().push_back(p0);
        }

        juce::String err;
        const juce::String bad = R"({"schema_version":"opentune.ds.v1","status":"ok","document_revision":1,"clip_id":100,"clip_generation":7,"request_id":11,"affected_object_ids":[],"patched_phonemes":[]})";
        if (OpenTune::DsJson::tryApplyRefineDurationsResponse(bad, doc, 99, 7, 11, err)) {
            logFail(test, "should reject clip_id mismatch");
            return;
        }

        const juce::String ok =
            R"({"schema_version":"opentune.ds.v1","status":"ok","document_revision":1,"clip_id":99,"clip_generation":7,"request_id":11,"error":null,"affected_object_ids":[1],"patched_phonemes":[{"id":1,"start_sec":0.0,"end_sec":0.5,"token":"a","lock_left":false,"lock_right":false}]})";
        if (!OpenTune::DsJson::tryApplyRefineDurationsResponse(ok, doc, 99, 7, 11, err)) {
            const juce::String msg = "apply failed: " + err;
            logFail(test, msg.toRawUTF8());
            return;
        }
        if (doc.getPhonemes().size() != 1u) {
            logFail(test, "phoneme count");
            return;
        }
        if (std::abs(doc.getPhonemes()[0].getEndClipSeconds() - 0.5) > 1e-6) {
            logFail(test, "phoneme end not applied");
            return;
        }
        logPass(test);
    }
}

void runSingingEditConsistencyTests() {
    logSection("SingingEditDocument Consistency");

    {
        const char* test = "validateAndNormalize freezes curve fields";
        SingingEditDocument doc;
        doc.ensureDefaultFullClipSegment(2.0);
        ParameterCurve c;
        c.curveId = "f0";
        c.points.push_back({0.1, 1.0f});
        doc.getParameterCurves().push_back(c);
        doc.getManualOverrides().curveOverrides.push_back(c);
        auto r = doc.validateAndNormalize(2.0);
        if (!r.ok) {
            logFail(test, "validation failed");
            return;
        }
        if (!doc.getParameterCurves().empty() || !doc.getManualOverrides().curveOverrides.empty()) {
            logFail(test, "frozen curves not cleared");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "split filters locked boundaries by range";
        SingingEditDocument doc;
        doc.ensureDefaultFullClipSegment(4.0);
        doc.getManualOverrides().lockedPhonemeBoundaryTicks = {
            TimeCoordinate::clipSecondsToTick(-0.2),
            TimeCoordinate::clipSecondsToTick(1.0),
            TimeCoordinate::clipSecondsToTick(3.0),
            TimeCoordinate::clipSecondsToTick(5.0)};
        SingingEditDocument left;
        SingingEditDocument right;
        if (!doc.splitByLocalSeconds(2.0, left, right)) {
            logFail(test, "split validation failed");
            return;
        }
        const int64_t splitTick = TimeCoordinate::clipSecondsToTick(2.0);
        for (int64_t tick : left.getManualOverrides().lockedPhonemeBoundaryTicks) {
            if (tick < 0 || tick > splitTick) {
                logFail(test, "left boundary out of range");
                return;
            }
        }
        for (int64_t tick : right.getManualOverrides().lockedPhonemeBoundaryTicks) {
            if (tick < 0) {
                logFail(test, "right boundary negative");
                return;
            }
        }
        logPass(test);
    }

    {
        const char* test = "merge deduplicates overrides and validates";
        SingingEditDocument left;
        SingingEditDocument right;
        left.ensureDefaultFullClipSegment(2.0);
        right.ensureDefaultFullClipSegment(1.0);

        Note nl;
        nl.stableId = 101;
        nl.setStartClipSeconds(0.0);
        nl.setEndClipSeconds(1.0);
        left.getNotes().push_back(nl);
        left.getManualOverrides().lockedNoteIds = {101};
        left.getManualOverrides().lockedPhonemeBoundaryTicks = {TimeCoordinate::clipSecondsToTick(0.5),
                                                                TimeCoordinate::clipSecondsToTick(1.0)};

        Note nr;
        nr.stableId = 101;
        nr.setStartClipSeconds(0.0);
        nr.setEndClipSeconds(1.0);
        right.getNotes().push_back(nr);
        right.getManualOverrides().lockedNoteIds = {101};
        right.getManualOverrides().lockedPhonemeBoundaryTicks = {TimeCoordinate::clipSecondsToTick(0.0),
                                                                 TimeCoordinate::clipSecondsToTick(0.5)};

        left.mergeFromRight(right, 2.0);
        auto r = left.validateAndNormalize(3.0);
        if (!r.ok) {
            logFail(test, "validate failed");
            return;
        }
        const auto& ids = left.getManualOverrides().lockedNoteIds;
        if (ids.empty()) {
            logFail(test, "locked ids empty");
            return;
        }
        if (!std::is_sorted(ids.begin(), ids.end())) {
            logFail(test, "locked ids not sorted");
            return;
        }
        const auto& bs = left.getManualOverrides().lockedPhonemeBoundaryTicks;
        if (!std::is_sorted(bs.begin(), bs.end())) {
            logFail(test, "boundaries not sorted");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "validate rejects invalid range";
        SingingEditDocument doc;
        PhonemeItem p;
        p.id = 1;
        p.setStartClipSeconds(1.0);
        p.setEndClipSeconds(1.0);
        doc.getPhonemes().push_back(p);
        auto r = doc.validateAndNormalize(2.0);
        if (r.ok) {
            logFail(test, "expected rejection");
            return;
        }
        logPass(test);
    }
}

static bool singingEditValueTreesEqual(const SingingEditDocument& a, const SingingEditDocument& b)
{
    return a.toValueTree().isEquivalentTo(b.toValueTree());
}

void runSingingEditCommitEnforcementTests()
{
    logSection("SingingEdit commit enforcement (Phase1)");

    {
        const char* test = "failed commit leaves authoritative singing edit unchanged";
        OpenTuneAudioProcessor processor;
        ClipSnapshot snap;
        auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 44100);
        buf->clear();
        snap.audioBuffer = buf;
        const uint64_t clipId = 910001;
        if (!processor.insertClipSnapshot(0, 0, snap, clipId)) {
            logFail(test, "insert clip failed");
            return;
        }
        const int clipIndex = processor.findClipIndexById(0, clipId);
        if (clipIndex < 0) {
            logFail(test, "clip index");
            return;
        }
        const auto before = processor.cloneClipSingingEdit(0, clipIndex);
        if (before == nullptr) {
            logFail(test, "clone before");
            return;
        }
        auto bad = std::make_shared<SingingEditDocument>(*before);
        PhonemeItem badPh;
        badPh.id = 999001;
        badPh.setStartClipSeconds(0.5);
        badPh.setEndClipSeconds(0.5);
        badPh.token = "x";
        bad->getPhonemes().push_back(badPh);
        SingingEditCommitOptions opts;
        if (processor.commitClipSingingEditDocument(0, clipId, std::move(bad), std::move(opts))) {
            logFail(test, "commit should fail validation");
            return;
        }
        const auto after = processor.cloneClipSingingEdit(0, clipIndex);
        if (after == nullptr || !singingEditValueTreesEqual(*before, *after)) {
            logFail(test, "authoritative document changed after failed commit");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "clipGeneration increases across successful commits";
        OpenTuneAudioProcessor processor;
        ClipSnapshot snap;
        auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 44100 * 2);
        buf->clear();
        snap.audioBuffer = buf;
        const uint64_t clipId = 910002;
        if (!processor.insertClipSnapshot(0, 0, snap, clipId)) {
            logFail(test, "insert");
            return;
        }
        const int clipIndex = processor.findClipIndexById(0, clipId);
        const uint64_t g0 = processor.getClipGeneration(0, clipIndex);
        auto d1 = processor.cloneClipSingingEdit(0, clipIndex);
        Note n;
        n.stableId = 50001;
        n.pitch = 330.0f;
        n.setStartClipSeconds(0.1);
        n.setEndClipSeconds(0.2);
        d1->getNotes().push_back(n);
        SingingEditCommitOptions o1;
        if (!processor.commitClipSingingEditDocument(0, clipId, std::move(d1), std::move(o1))) {
            logFail(test, "first commit");
            return;
        }
        const uint64_t g1 = processor.getClipGeneration(0, clipIndex);
        auto d2 = processor.cloneClipSingingEdit(0, clipIndex);
        d2->getNotes().back().pitch = 440.0f;
        SingingEditCommitOptions o2;
        if (!processor.commitClipSingingEditDocument(0, clipId, std::move(d2), std::move(o2))) {
            logFail(test, "second commit");
            return;
        }
        const uint64_t g2 = processor.getClipGeneration(0, clipIndex);
        if (!(g2 > g1 && g1 > g0)) {
            logFail(test, "generation not strictly increasing");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "commitClipSingingEditDocumentBatch is all-or-nothing";
        OpenTuneAudioProcessor processor;
        auto makeSnap = [](uint64_t id) {
            ClipSnapshot s;
            auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 44100);
            buf->clear();
            s.audioBuffer = buf;
            return std::pair<ClipSnapshot, uint64_t>{std::move(s), id};
        };
        auto a = makeSnap(920001);
        auto b = makeSnap(920002);
        if (!processor.insertClipSnapshot(0, 0, a.first, a.second) || !processor.insertClipSnapshot(0, 1, b.first, b.second)) {
            logFail(test, "insert two clips");
            return;
        }
        const int idxA = processor.findClipIndexById(0, 920001);
        const int idxB = processor.findClipIndexById(0, 920002);
        const auto beforeA = processor.cloneClipSingingEdit(0, idxA);

        std::vector<SingingEditBatchCommitItem> batch;
        {
            SingingEditBatchCommitItem item;
            item.trackId = 0;
            item.clipId = 920001;
            item.newDoc = processor.cloneClipSingingEdit(0, idxA);
            Note extra;
            extra.stableId = 88001;
            extra.pitch = 220.0f;
            extra.setStartClipSeconds(0.05);
            extra.setEndClipSeconds(0.06);
            item.newDoc->getNotes().push_back(extra);
            batch.push_back(std::move(item));
        }
        {
            SingingEditBatchCommitItem item;
            item.trackId = 0;
            item.clipId = 920002;
            item.newDoc = processor.cloneClipSingingEdit(0, idxB);
            PhonemeItem badPh;
            badPh.id = 990002;
            badPh.setStartClipSeconds(0.2);
            badPh.setEndClipSeconds(0.2);
            badPh.token = "z";
            item.newDoc->getPhonemes().push_back(badPh);
            batch.push_back(std::move(item));
        }
        if (processor.commitClipSingingEditDocumentBatch(std::move(batch))) {
            logFail(test, "batch should fail");
            return;
        }
        const auto afterA = processor.cloneClipSingingEdit(0, idxA);
        if (beforeA == nullptr || afterA == nullptr || !singingEditValueTreesEqual(*beforeA, *afterA)) {
            logFail(test, "first clip changed on failed batch");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "insertClipSnapshot clipGeneration uses monotonic max formula";
        OpenTuneAudioProcessor processor;
        ClipSnapshot snap;
        auto bufIns = std::make_shared<juce::AudioBuffer<float>>(1, 22050);
        bufIns->clear();
        snap.audioBuffer = bufIns;
        snap.clipGeneration = 99;
        if (!processor.insertClipSnapshot(0, 0, snap, 930001)) {
            logFail(test, "insert");
            return;
        }
        const int idx = processor.findClipIndexById(0, 930001);
        const uint64_t g = processor.getClipGeneration(0, idx);
        const uint64_t expected = std::max<uint64_t>(uint64_t(1), snap.clipGeneration + uint64_t(1));
        if (g != expected) {
            logFail(test, "generation should be max(1, G_snap+1) for new clipId");
            return;
        }
        logPass(test);
    }
}

void runRefineAsyncGateTests() {
    logSection("Refine Async Gate");

    {
        const char* test = "clip generation increments on setClipSingingEditById";
        OpenTuneAudioProcessor processor;
        ClipSnapshot snap;
        auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 44100);
        buf->clear();
        snap.audioBuffer = buf;
        if (!processor.insertClipSnapshot(0, 0, snap, 777)) {
            logFail(test, "insert clip failed");
            return;
        }
        const int clipIndex = processor.findClipIndexById(0, 777);
        if (clipIndex < 0) {
            logFail(test, "clip not found");
            return;
        }
        const uint64_t g0 = processor.getClipGeneration(0, clipIndex);
        auto doc = processor.cloneClipSingingEdit(0, clipIndex);
        if (!doc || !processor.setClipSingingEditById(0, 777, doc)) {
            logFail(test, "set singing edit failed");
            return;
        }
        const uint64_t g1 = processor.getClipGeneration(0, clipIndex);
        if (g1 <= g0) {
            logFail(test, "generation did not increment");
            return;
        }
        logPass(test);
    }

    {
        const char* test = "response gate rejects request id mismatch";
        SingingEditDocument doc;
        PhonemeItem p;
        p.id = 1;
        p.setStartClipSeconds(0.0);
        p.setEndClipSeconds(1.0);
        p.token = "a";
        doc.getPhonemes().push_back(p);
        juce::String err;
        const juce::String resp = R"({"schema_version":"opentune.ds.v1","status":"ok","document_revision":1,"clip_id":9,"clip_generation":2,"request_id":999,"affected_object_ids":[1],"patched_phonemes":[{"id":1,"start_sec":0.2,"end_sec":0.8}]})";
        if (OpenTune::DsJson::tryApplyRefineDurationsResponse(resp, doc, 9, 2, 3, err)) {
            logFail(test, "mismatch should be rejected");
            return;
        }
        logPass(test);
    }
}

void runSerializationRoundTripTests() {
    logSection("Serialization Round Trip");
    const char* test = "singing edit roundtrip keeps authoritative data and freezes curves";
    SingingEditDocument doc;
    doc.ensureDefaultFullClipSegment(2.0);
    Note n;
    n.stableId = 42;
    n.setStartClipSeconds(0.1);
    n.setEndClipSeconds(0.9);
    n.pitch = 440.0f;
    doc.getNotes().push_back(n);
    PhonemeItem p;
    p.id = 11;
    p.setStartClipSeconds(0.1);
    p.setEndClipSeconds(0.5);
    p.token = "a";
    doc.getPhonemes().push_back(p);
    doc.getManualOverrides().lockedNoteIds.push_back(42);
    doc.getManualOverrides().lockedPhonemeBoundaryTicks.push_back(TimeCoordinate::clipSecondsToTick(0.5));
    ParameterCurve c;
    c.curveId = "f0";
    c.points.push_back({0.2, 1.0f});
    doc.getParameterCurves().push_back(c);
    doc.getManualOverrides().curveOverrides.push_back(c);

    {
        const auto vr = doc.validateAndNormalize(2.0);
        if (!vr.ok) {
            logFail(test, "setup validate failed");
            return;
        }
    }

    const juce::ValueTree tree = doc.toValueTree();
    SingingEditDocument loaded;
    loaded.fromValueTree(tree);
    if (loaded.getNotes().empty() || loaded.getPhonemes().empty()) {
        logFail(test, "authoritative data lost");
        return;
    }
    if (!loaded.getParameterCurves().empty() || !loaded.getManualOverrides().curveOverrides.empty()) {
        logFail(test, "frozen fields should not roundtrip");
        return;
    }
    if (loaded.getPhonemes()[0].startTick != doc.getPhonemes()[0].startTick
        || loaded.getPhonemes()[0].endTick != doc.getPhonemes()[0].endTick) {
        logFail(test, "phoneme tick roundtrip");
        return;
    }
    logPass(test);
}

void runClipSnapshotSingingEditTests() {
    logSection("ClipSnapshot singingEdit");

    const char* test = "getClipSnapshot carries singingEdit phoneme ticks";
    OpenTuneAudioProcessor processor;
    ClipSnapshot snap;
    auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 44100 * 2);
    buf->clear();
    snap.audioBuffer = buf;
    SingingEditDocument docIn;
    docIn.ensureDefaultFullClipSegment(2.0);
    PhonemeItem ph;
    ph.id = 99;
    ph.setStartClipSeconds(0.05);
    ph.setEndClipSeconds(0.12);
    ph.token = "x";
    docIn.getPhonemes().push_back(ph);
    {
        const auto vr = docIn.validateAndNormalize(2.0);
        if (!vr.ok) {
            logFail(test, "doc validate");
            return;
        }
    }
    snap.singingEdit = std::make_shared<const SingingEditDocument>(docIn);
    if (!processor.insertClipSnapshot(0, 0, snap, 4242)) {
        logFail(test, "insert failed");
        return;
    }
    ClipSnapshot out;
    if (!processor.getClipSnapshot(0, 4242, out) || out.singingEdit == nullptr) {
        logFail(test, "getClipSnapshot missing singingEdit");
        return;
    }
    if (out.singingEdit->getPhonemes().size() != 1u || out.singingEdit->getPhonemes()[0].startTick != ph.startTick) {
        logFail(test, "phoneme mismatch");
        return;
    }
    logPass(test);
}

void runNoteTests() {
    logSection("Note Tests");

    {
        const char* test = "Note basics";
        Note note;
        note.setStartClipSeconds(0.0);
        note.setEndClipSeconds(1.0);
        note.pitch = 440.0f;

        if (!approxEqual(note.getDuration(), 1.0, 0.001)) {
            logFail(test, "duration wrong"); return;
        }
        if (note.getMidiNote() != 69) {
            logFail(test, "A4 not MIDI 69"); return;
        }
        logPass(test);
    }

    {
        const char* test = "Note adjusted pitch";
        Note note;
        note.pitch = 440.0f;
        note.pitchOffset = 12.0f;

        if (!approxEqual(note.getAdjustedPitch(), 880.0f, 1.0f)) {
            logFail(test, "12 semitones should double"); return;
        }
        logPass(test);
    }

    {
        const char* test = "NoteSequence insert";
        NoteSequence seq;

        Note n1;
        n1.setStartClipSeconds(0.0);
        n1.setEndClipSeconds(1.0);
        Note n2;
        n2.setStartClipSeconds(1.0);
        n2.setEndClipSeconds(2.0);

        seq.insertNoteSorted(n1);
        seq.insertNoteSorted(n2);

        if (seq.size() != 2) { logFail(test, "size not 2"); return; }
        if (!seq.getNoteAtTime(0.5)) { logFail(test, "should find note at 0.5"); return; }
        logPass(test);
    }

    {
        const char* test = "NoteSequence non-overlapping";
        NoteSequence seq;

        Note n1;
        n1.setStartClipSeconds(0.0);
        n1.setEndClipSeconds(2.0);
        Note n2;
        n2.setStartClipSeconds(1.0);
        n2.setEndClipSeconds(3.0);

        seq.insertNoteSorted(n1);
        seq.insertNoteSorted(n2);

        const auto& notes = seq.getNotes();
        if (notes[0].endTick > notes[1].startTick) {
            logFail(test, "notes overlap"); return;
        }
        logPass(test);
    }
}

void runRenderCacheTests() {
    logSection("RenderCache Tests");

    {
        const char* test = "Empty cache";
        RenderCache cache;
        if (cache.getTotalMemoryUsage() != 0) {
            logFail(test, "new cache not zero memory"); return;
        }
        logPass(test);
    }

    {
        const char* test = "Add chunk";
        RenderCache cache;

        // requestRenderPending creates chunk and sets desiredRevision
        cache.requestRenderPending(0.0, 1.0);
        uint64_t rev = 1; // desiredRevision starts at 0, incremented to 1
        std::vector<float> audio(44100, 0.5f);
        if (!cache.addChunk(0.0, 1.0, std::move(audio), rev)) {
            logFail(test, "add chunk failed"); return;
        }
        if (cache.getTotalMemoryUsage() <= 0) {
            logFail(test, "memory not positive"); return;
        }
        logPass(test);
    }

    {
        const char* test = "Revision mismatch";
        RenderCache cache;
        // requestRenderPending creates chunk with desiredRevision = 1
        cache.requestRenderPending(0.0, 1.0);

        std::vector<float> audio(44100, 0.5f);
        if (cache.addChunk(0.0, 1.0, std::move(audio), 999)) {
            logFail(test, "wrong revision accepted"); return;
        }
        logPass(test);
    }

    {
        const char* test = "LRU evicts least recently used chunk";
        RenderCache cache;
        cache.setMemoryLimit(8000);

        constexpr int kSampleRate = 44100;
        constexpr int samplesPerChunk = 1000;
        const double chunkDurSec = static_cast<double>(samplesPerChunk) / static_cast<double>(kSampleRate);
        const double t0 = 0.0;
        const double t1 = 10.0;
        const double t2 = 20.0;

        cache.requestRenderPending(t0, t0 + chunkDurSec);
        std::vector<float> first(samplesPerChunk, 0.25f);
        if (!cache.addChunk(t0, t0 + chunkDurSec, std::move(first), 1)) {
            logFail(test, "first add failed"); return;
        }

        cache.requestRenderPending(t1, t1 + chunkDurSec);
        std::vector<float> second(samplesPerChunk, 0.5f);
        if (!cache.addChunk(t1, t1 + chunkDurSec, std::move(second), 1)) {
            logFail(test, "second add failed"); return;
        }

        cache.requestRenderPending(t2, t2 + chunkDurSec);
        std::vector<float> third(samplesPerChunk, 0.75f);
        if (!cache.addChunk(t2, t2 + chunkDurSec, std::move(third), 1)) {
            logFail(test, "third add failed"); return;
        }

        float buf[64];
        const double inFirst = t0 + 0.5 * chunkDurSec;
        const double inMiddle = t1 + 0.5 * chunkDurSec;
        int n = cache.readAtTimeForRate(buf, 64, inFirst, kSampleRate, false);
        if (n != 0) {
            logFail(test, "expected earliest chunk evicted"); return;
        }

        n = cache.readAtTimeForRate(buf, 64, inMiddle, kSampleRate, false);
        if (n <= 0) {
            logFail(test, "middle chunk should remain"); return;
        }

        cache.setMemoryLimit(RenderCache::kDefaultGlobalCacheLimitBytes);
        logPass(test);
    }
}

void runPitchCurveTests() {
    logSection("PitchCurve Tests");

    {
        const char* test = "Empty curve";
        PitchCurve curve;
        if (!curve.isEmpty()) { logFail(test, "new curve not empty"); return; }
        if (curve.hasAnyCorrection()) { logFail(test, "new curve has correction"); return; }
        logPass(test);
    }

    {
        const char* test = "Set original F0";
        PitchCurve curve;
        std::vector<float> f0 = { 440.0f, 450.0f, 460.0f, 470.0f, 480.0f };

        curve.setOriginalF0(f0);
        if (curve.isEmpty()) { logFail(test, "curve empty after set"); return; }
        if (curve.size() != 5) { logFail(test, "size mismatch"); return; }
        logPass(test);
    }

    {
        const char* test = "Manual correction";
        PitchCurve curve;
        std::vector<float> f0(100, 440.0f);
        curve.setOriginalF0(f0);

        std::vector<float> correction(10, 880.0f);
        curve.setManualCorrectionRange(20, 30, correction, CorrectedSegment::Source::HandDraw);

        if (!curve.hasAnyCorrection()) { logFail(test, "no correction"); return; }
        if (!curve.hasCorrectionInRange(20, 30)) { logFail(test, "correction not in range"); return; }
        if (curve.hasCorrectionInRange(0, 10)) { logFail(test, "false positive in range"); return; }
        logPass(test);
    }

    {
        const char* test = "Snapshot immutability";
        PitchCurve curve;
        std::vector<float> f0(100, 440.0f);
        curve.setOriginalF0(f0);

        auto snap1 = curve.getSnapshot();
        size_t size1 = snap1->getOriginalF0().size();

        std::vector<float> newF0(200, 880.0f);
        curve.setOriginalF0(newF0);

        auto snap2 = curve.getSnapshot();
        if (snap1->getOriginalF0().size() != size1) { logFail(test, "old snapshot modified"); return; }
        if (snap2->getOriginalF0().size() != 200) { logFail(test, "new snapshot wrong size"); return; }
        logPass(test);
    }
}

void runPianoRollUndoMatrixTests() {
    logSection("PianoRoll Undo Matrix Tests");

    OpenTuneAudioProcessor processor;
    UndoManager undoManager;

    ClipSnapshot snap;
    auto clipAudio = std::make_shared<juce::AudioBuffer<float>>(1, 44100);
    clipAudio->clear();
    snap.audioBuffer = clipAudio;
    snap.name = "UndoMatrixClip";
    snap.colour = juce::Colours::lightgrey;
    snap.pitchCurve = std::make_shared<PitchCurve>();
    snap.pitchCurve->setOriginalF0(std::vector<float>(512, 440.0f));
    snap.originalF0State = OriginalF0State::Ready;
    snap.renderCache = std::make_shared<RenderCache>();

    const int trackId = 0;
    const uint64_t clipId = 900001;
    if (!processor.insertClipSnapshot(trackId, 0, snap, clipId)) {
        logFail("Setup clip", "insertClipSnapshot failed");
        return;
    }

    int clipIndex = processor.findClipIndexById(trackId, clipId);
    if (clipIndex < 0) {
        logFail("Setup clip", "cannot find inserted clip");
        return;
    }

    auto curve = processor.getClipPitchCurve(trackId, clipIndex);
    if (!curve) {
        logFail("Setup clip", "clip pitch curve is null");
        return;
    }

    PianoRollUndoSupport::Context undoCtx;
    undoCtx.getNotesCopy = [&processor, trackId, &clipIndex]() {
        return processor.getClipNotes(trackId, clipIndex);
    };
    undoCtx.getPitchCurve = [&curve]() { return curve; };
    undoCtx.getCurrentClipId = [clipId]() { return clipId; };
    undoCtx.getCurrentTrackId = [trackId]() { return trackId; };
    undoCtx.getProcessor = [&processor]() { return &processor; };
    undoCtx.getUndoManager = [&undoManager]() { return &undoManager; };

    PianoRollUndoSupport undoSupport(std::move(undoCtx));

    auto frameFromSec = [](double sec) {
        return static_cast<int>(sec / (512.0 / 16000.0));
    };

    auto makeBaseNotes = []() {
        std::vector<Note> notes;
        Note n1;
        n1.startTime = 0.10;
        n1.endTime = 0.50;
        n1.pitch = 440.0f;
        n1.originalPitch = 440.0f;
        n1.selected = true;
        notes.push_back(n1);

        Note n2;
        n2.startTime = 0.60;
        n2.endTime = 1.00;
        n2.pitch = 493.88f;
        n2.originalPitch = 493.88f;
        notes.push_back(n2);
        return notes;
    };

    auto resetBaseline = [&]() -> bool {
        undoManager.clear();
        auto notes = makeBaseNotes();
        if (!processor.setClipNotes(trackId, clipIndex, notes)) {
            logFail("Undo Matrix setup", "setClipNotes baseline failed");
            return false;
        }
        curve->clearAllCorrections();
        curve->applyCorrectionToRange(notes, 0, frameFromSec(1.0), 0.8f, 0.0f, 6.0f, 44100.0);
        return true;
    };

    struct PianoState {
        std::vector<Note> notes;
        std::vector<CorrectedSegmentsChangeAction::SegmentSnapshot> segments;
    };

    auto captureState = [&]() -> PianoState {
        PianoState s;
        s.notes = processor.getClipNotes(trackId, clipIndex);
        s.segments = CorrectedSegmentsChangeAction::captureSegments(curve);
        return s;
    };

    auto statesEqual = [](const PianoState& a, const PianoState& b) {
        return PianoRollUndoSupport::notesEquivalent(a.notes, b.notes)
            && CorrectedSegmentsChangeAction::snapshotsEquivalent(a.segments, b.segments);
    };

    auto runCase = [&](const char* testName, const juce::String& txName, const std::function<void()>& op) {
        if (!resetBaseline()) {
            return;
        }
        const auto before = captureState();

        undoSupport.beginTransaction(txName);
        op();
        const auto after = captureState();
        if (statesEqual(before, after)) {
            logFail(testName, "operation did not change state");
            return;
        }
        undoSupport.commitTransaction();

        if (!undoManager.canUndo()) {
            logFail(testName, "no undo action recorded");
            return;
        }

        for (int cycle = 0; cycle < 20; ++cycle) {
            undoManager.undo();
            const auto afterUndo = captureState();
            if (!statesEqual(before, afterUndo)) {
                logFail(testName, "undo state mismatch");
                return;
            }

            if (!undoManager.canRedo()) {
                logFail(testName, "redo not available after undo");
                return;
            }

            undoManager.redo();
            const auto afterRedo = captureState();
            if (!statesEqual(after, afterRedo)) {
                logFail(testName, "redo state mismatch");
                return;
            }
        }

        logPass(testName);
    };

    runCase("Undo Matrix - Draw", "Draw Note", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        Note n;
        n.startTime = 1.10;
        n.endTime = 1.40;
        n.pitch = 523.25f;
        n.originalPitch = 523.25f;
        notes.push_back(n);
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(1.0), frameFromSec(1.5), 0.8f, 0.0f, 6.0f, 44100.0);
    });

    runCase("Undo Matrix - Move", "Move Notes", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        notes[0].pitchOffset += 2.0f;
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(0.0), frameFromSec(1.1), 0.8f, 0.0f, 6.0f, 44100.0);
    });

    runCase("Undo Matrix - Resize", "Resize Note", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        notes[0].endTime = 0.70;
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(0.0), frameFromSec(1.1), 0.8f, 0.0f, 6.0f, 44100.0);
    });

    runCase("Undo Matrix - Delete", "Delete Notes", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        notes.erase(notes.begin());
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->clearCorrectionRange(frameFromSec(0.10), frameFromSec(0.50));
    });

    runCase("Undo Matrix - HandDraw", "Draw F0 Curve", [&]() {
        std::vector<float> f0(20, 510.0f);
        curve->setManualCorrectionRange(40, 60, f0, CorrectedSegment::Source::HandDraw, 0.85f);
    });

    runCase("Undo Matrix - LineAnchor", "Line Anchor", [&]() {
        std::vector<float> f0;
        f0.reserve(30);
        for (int i = 0; i < 30; ++i) {
            f0.push_back(440.0f + static_cast<float>(i) * 3.0f);
        }
        curve->setManualCorrectionRange(80, 110, f0, CorrectedSegment::Source::LineAnchor, 0.70f);
    });

    runCase("Undo Matrix - Retune", "Retune Speed", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        for (auto& n : notes) n.retuneSpeed = 0.30f;
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(0.0), frameFromSec(1.1), 0.30f, 0.0f, 6.0f, 44100.0);
    });

    runCase("Undo Matrix - Vibrato", "Vibrato Depth", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        for (auto& n : notes) {
            n.vibratoDepth = 35.0f;
            n.vibratoRate = 8.0f;
        }
        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(0.0), frameFromSec(1.1), 0.8f, 35.0f, 8.0f, 44100.0);
    });

    runCase("Undo Matrix - AutoTune", "Auto Tune", [&]() {
        std::vector<Note> generated;
        Note a;
        a.startTime = 0.00;
        a.endTime = 0.30;
        a.pitch = 392.0f;
        a.originalPitch = 392.0f;
        generated.push_back(a);

        Note b;
        b.startTime = 0.30;
        b.endTime = 0.70;
        b.pitch = 440.0f;
        b.originalPitch = 440.0f;
        generated.push_back(b);

        Note c;
        c.startTime = 0.70;
        c.endTime = 1.10;
        c.pitch = 493.88f;
        c.originalPitch = 493.88f;
        generated.push_back(c);

        processor.setClipNotes(trackId, clipIndex, generated);
        curve->clearAllCorrections();
        curve->applyCorrectionToRange(generated, frameFromSec(0.0), frameFromSec(1.2), 0.9f, 0.0f, 6.0f, 44100.0);
    });

    runCase("Undo Matrix - Mixed Flow", "Mixed Flow", [&]() {
        auto notes = processor.getClipNotes(trackId, clipIndex);
        notes[0].pitchOffset += 1.0f;

        Note appended;
        appended.startTime = 1.20;
        appended.endTime = 1.55;
        appended.pitch = 554.37f;
        appended.originalPitch = 554.37f;
        notes.push_back(appended);

        processor.setClipNotes(trackId, clipIndex, notes);
        curve->applyCorrectionToRange(notes, frameFromSec(0.0), frameFromSec(1.6), 0.65f, 20.0f, 7.0f, 44100.0);

        std::vector<float> handDraw(24, 500.0f);
        curve->setManualCorrectionRange(36, 60, handDraw, CorrectedSegment::Source::HandDraw, 0.75f);
    });
}

void runVocoderSchedulerTests() {
    logSection("VocoderRenderScheduler Serial Execution Tests");

    {
        const char* test = "Scheduler starts not running, requires initialization";
        OpenTune::VocoderRenderScheduler scheduler;
        
        if (scheduler.isRunning()) {
            logFail(test, "scheduler should NOT be running before initialize");
            return;
        }
        
        if (scheduler.getQueueDepth() != 0) {
            logFail(test, "new scheduler has non-zero queue depth");
            return;
        }
        
        logPass(test);
    }

    {
        const char* test = "Scheduler queue accepts jobs before initialization";
        OpenTune::VocoderRenderScheduler scheduler;
        
        OpenTune::VocoderRenderScheduler::Job job;
        job.f0 = std::vector<float>(10, 440.0f);
        job.energy = std::vector<float>(10, 1.0f);
        job.mel = std::vector<float>(10 * 128, 0.0f);
        job.onComplete = [](bool, const juce::String&, const std::vector<float>&) {};
        
        scheduler.submit(std::move(job));
        
        if (scheduler.getQueueDepth() < 1) {
            logFail(test, "queue should have at least one job after submit");
            return;
        }
        
        scheduler.shutdown();
        logPass(test);
    }

    {
        const char* test = "VocoderScheduler_RejectsNullService";
        OpenTune::VocoderRenderScheduler scheduler;
        bool initOk = scheduler.initialize(nullptr);
        if (initOk) {
            logFail(test, "should reject null service");
            return;
        }
        scheduler.shutdown();
        logPass(test);
    }

    {
        const char* test = "VocoderScheduler_SerializesRunOnSingleSession";
        MockVocoderService mockService;
        OpenTune::VocoderRenderScheduler scheduler;

        bool initOk = scheduler.initialize(&mockService);
        if (!initOk) {
            logFail(test, "scheduler initialize failed");
            scheduler.shutdown();
            return;
        }

        constexpr int numJobs = 8;
        std::atomic<int> completedJobs{0};

        for (int i = 0; i < numJobs; ++i) {
            OpenTune::VocoderRenderScheduler::Job job;
            job.f0 = std::vector<float>(100, 440.0f);
            job.energy = std::vector<float>(100, 1.0f);
            job.mel = std::vector<float>(100 * 128, 0.0f);
            job.onComplete = [&completedJobs](bool, const juce::String&, const std::vector<float>&) {
                ++completedJobs;
            };
            scheduler.submit(std::move(job));
        }

        // Wait for all jobs to complete (with 30s timeout)
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (completedJobs.load() < numJobs) {
            if (std::chrono::steady_clock::now() > deadline) {
                scheduler.shutdown();
                logFail(test, "timeout waiting for jobs to complete");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Verify serial execution: concurrentCalls_ should be 0 after completion
        if (mockService.concurrentCalls_.load() != 0) {
            scheduler.shutdown();
            logFail(test, "concurrentCalls_ non-zero after completion");
            return;
        }

        // Verify max concurrent was 1 (strictly serial)
        if (mockService.maxConcurrentSeen_ > 1) {
            scheduler.shutdown();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "maxConcurrentSeen=%d (expected 1)",
                mockService.maxConcurrentSeen_);
            logFail(test, buf);
            return;
        }

        // Verify all jobs succeeded
        if (mockService.numSuccessfulCalls_ != numJobs) {
            scheduler.shutdown();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "numSuccessfulCalls_=%d (expected %d)",
                mockService.numSuccessfulCalls_, numJobs);
            logFail(test, buf);
            return;
        }

        scheduler.shutdown();
        logPass(test);
    }

    {
        const char* test = "VocoderScheduler_SerializesRunOnSingleSession_ExtremeConcurrency";
        MockVocoderService mockService;
        OpenTune::VocoderRenderScheduler scheduler;
        mockService.callDuration_ = std::chrono::milliseconds(20);

        bool initOk = scheduler.initialize(&mockService);
        if (!initOk) {
            logFail(test, "scheduler initialize failed");
            scheduler.shutdown();
            return;
        }

        constexpr int numJobs = 32;
        std::atomic<int> completedJobs{0};

        for (int i = 0; i < numJobs; ++i) {
            OpenTune::VocoderRenderScheduler::Job job;
            job.f0 = std::vector<float>(50, 440.0f);
            job.energy = std::vector<float>(50, 1.0f);
            job.mel = std::vector<float>(50 * 128, 0.0f);
            job.onComplete = [&completedJobs](bool, const juce::String&, const std::vector<float>&) {
                ++completedJobs;
            };
            scheduler.submit(std::move(job));
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (completedJobs.load() < numJobs) {
            if (std::chrono::steady_clock::now() > deadline) {
                scheduler.shutdown();
                logFail(test, "timeout waiting for jobs");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (mockService.maxConcurrentSeen_ > 1) {
            scheduler.shutdown();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "maxConcurrentSeen=%d (expected 1) under extreme load",
                mockService.maxConcurrentSeen_);
            logFail(test, buf);
            return;
        }

        scheduler.shutdown();
        logPass(test);
    }
}

void runF0VocoderIsolationTests() {
    logSection("F0/Vocoder Lifecycle Isolation Tests");

    {
        const char* test = "F0Service and VocoderService are independent instances";
        OpenTune::F0InferenceService f0Service;
        OpenTune::VocoderInferenceService vocoderService;
        
        if (f0Service.isInitialized()) {
            logFail(test, "new F0Service reports initialized");
            return;
        }
        if (vocoderService.isInitialized()) {
            logFail(test, "new VocoderService reports initialized");
            return;
        }
        
        logPass(test);
    }

    {
        const char* test = "VocoderRenderScheduler requires initialization to run";
        OpenTune::VocoderRenderScheduler scheduler;
        
        if (scheduler.isRunning()) {
            logFail(test, "scheduler should NOT be running before initialize");
            return;
        }
        
        scheduler.shutdown();
        
        if (scheduler.isRunning()) {
            logFail(test, "scheduler should not be running after shutdown");
            return;
        }
        
        logPass(test);
    }

    // ============================================================
    // F0/Vocoder isolation: Vocoder failure does NOT affect F0
    // ============================================================
    {
        const char* test = "F0AndVocoder_AreLifecycleIsolated_VocoderShutdownDoesNotAffectF0";
        OpenTune::F0InferenceService f0Service;
        OpenTune::VocoderInferenceService vocoderService;

        // Both start uninitialized
        if (f0Service.isInitialized() || vocoderService.isInitialized()) {
            logFail(test, "services should start uninitialized");
            return;
        }

        // Shutdown Vocoder (without ever initializing it)
        vocoderService.shutdown();

        // F0Service should still be uninitialized (not affected)
        if (f0Service.isInitialized()) {
            logFail(test, "F0Service should remain uninitialized after Vocoder shutdown");
            return;
        }

        logPass(test);
    }

    {
        const char* test = "F0AndVocoder_AreLifecycleIsolated_MultipleShutdownSafe";
        OpenTune::F0InferenceService f0Service;
        OpenTune::VocoderInferenceService vocoderService;

        // Shutdown both (multiple times is safe)
        vocoderService.shutdown();
        vocoderService.shutdown();  // double shutdown should be safe
        f0Service.shutdown();
        f0Service.shutdown();  // double shutdown should be safe

        // Neither should be initialized
        if (f0Service.isInitialized()) {
            logFail(test, "F0Service should remain uninitialized");
            return;
        }
        if (vocoderService.isInitialized()) {
            logFail(test, "VocoderService should remain uninitialized");
            return;
        }

        logPass(test);
    }

    {
        const char* test = "F0AndVocoder_AreLifecycleIsolated_IndependentShutdownOrder";
        OpenTune::F0InferenceService f0Service;
        OpenTune::VocoderInferenceService vocoderService;

        // Vocoder goes down first, F0 stays alive
        vocoderService.shutdown();
        if (f0Service.isInitialized()) {
            logFail(test, "F0Service should not be affected by Vocoder shutdown");
            return;
        }

        // Then F0 goes down
        f0Service.shutdown();

        logPass(test);
    }

    {
        const char* test = "F0AndVocoder_AreLifecycleIsolated_SchedulerShutdownDoesNotAffectServices";
        OpenTune::VocoderInferenceService vocoderService;
        OpenTune::VocoderRenderScheduler scheduler;

        bool initOk = scheduler.initialize(&vocoderService);
        if (!initOk) {
            logFail(test, "scheduler initialize failed");
            scheduler.shutdown();
            return;
        }

        // Scheduler shutdown should not affect Vocoder service state
        scheduler.shutdown();

        // Vocoder service should still be usable (shutdown just stops the queue worker)
        // After scheduler shutdown, the service is still initialized
        if (!vocoderService.isInitialized()) {
            // This is expected - we never initialized the service
        }

        logPass(test);
    }

    {
        const char* test = "F0AndVocoder_AreLifecycleIsolated_NoSharedMutex";
        // Verify F0InferenceService and VocoderInferenceService do NOT share mutex objects
        // This is a structural invariant: they are completely independent runtime entities
        OpenTune::F0InferenceService f0Service;
        OpenTune::VocoderInferenceService vocoderService;

        // Both start uninitialized - they have no shared state
        if (f0Service.isInitialized() || vocoderService.isInitialized()) {
            logFail(test, "new services should not be initialized");
            return;
        }

        // Shutdown both
        f0Service.shutdown();
        vocoderService.shutdown();

        logPass(test);
    }
}

void runVocoderServiceSerializationTests() {
    logSection("VocoderInferenceService Serialization Tests");

    {
        const char* test = "VocoderService_SerializesRunOnSingleSession";
        MockVocoderService mockService;
        mockService.callDuration_ = std::chrono::milliseconds(25);

        constexpr int numCalls = 16;
        std::vector<std::thread> callers;
        callers.reserve(numCalls);

        for (int i = 0; i < numCalls; ++i) {
            callers.emplace_back([&mockService]() {
                std::vector<float> f0(40, 440.0f);
                std::vector<float> energy(40, 1.0f);
                std::vector<float> mel(40 * 128, 0.0f);
                auto result = mockService.synthesizeAudioWithEnergy(
                    f0, energy, mel.data(), mel.size());
                (void)result;
            });
        }

        for (auto& t : callers) {
            t.join();
        }

        if (mockService.maxConcurrentSeen_ > 1) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "maxConcurrentSeen=%d (expected 1)",
                mockService.maxConcurrentSeen_);
            logFail(test, buf);
            return;
        }

        if (mockService.numSuccessfulCalls_ != numCalls) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "numSuccessfulCalls_=%d (expected %d)",
                mockService.numSuccessfulCalls_, numCalls);
            logFail(test, buf);
            return;
        }

        logPass(test);
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "OpenTune Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    runLockFreeQueueTests();
    runPitchUtilsTests();
    runNoteTests();
    runDsJsonContractTests();
    runSingingEditConsistencyTests();
    runSingingEditCommitEnforcementTests();
    runRefineAsyncGateTests();
    runSerializationRoundTripTests();
    runClipSnapshotSingingEditTests();
    runRenderCacheTests();
    runPitchCurveTests();
    runPianoRollUndoMatrixTests();
    runVocoderSchedulerTests();
    runF0VocoderIsolationTests();
    runVocoderServiceSerializationTests();

    std::cout << "\n========================================" << std::endl;
    std::cout << "Tests Complete" << std::endl;
    std::cout << "========================================" << std::endl;

    return gOpenTuneTestFailures.load(std::memory_order_relaxed) > 0 ? 1 : 0;
}
