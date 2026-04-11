#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Utils/AppLogger.h"

namespace OpenTune {

namespace {

static juce::String encodeFloatVectorBase64(const std::vector<float>& v)
{
    if (v.empty()) {
        return {};
    }
    return juce::Base64::toBase64(v.data(), v.size() * sizeof(float));
}

static bool decodeFloatVectorBase64(const juce::var& value, std::vector<float>& out)
{
    out.clear();
    const juce::String s = value.toString();
    if (s.isEmpty()) {
        return true;
    }

    juce::MemoryBlock mb;
    juce::MemoryOutputStream mos(mb, false);
    if (!juce::Base64::convertFromBase64(mos, s)) {
        return false;
    }
    if (mb.getSize() % sizeof(float) != 0) {
        return false;
    }

    const size_t n = mb.getSize() / sizeof(float);
    out.resize(n);
    std::memcpy(out.data(), mb.getData(), n * sizeof(float));
    return true;
}

static bool decodeAudioBufferBase64(const juce::String& b64,
                                    int numChannels,
                                    int numSamples,
                                    juce::AudioBuffer<float>& out)
{
    if (numChannels <= 0 || numSamples <= 0 || b64.isEmpty()) {
        return false;
    }

    juce::MemoryBlock mb;
    juce::MemoryOutputStream mos(mb, false);
    if (!juce::Base64::convertFromBase64(mos, b64)) {
        return false;
    }

    const auto expected = static_cast<size_t>(numChannels) * static_cast<size_t>(numSamples) * sizeof(float);
    if (mb.getSize() != expected) {
        return false;
    }

    out.setSize(numChannels, numSamples, false, true, true);
    const auto* p = static_cast<const float*>(mb.getData());
    for (int ch = 0; ch < numChannels; ++ch) {
        std::memcpy(out.getWritePointer(ch),
                    p + static_cast<size_t>(ch) * static_cast<size_t>(numSamples),
                    sizeof(float) * static_cast<size_t>(numSamples));
    }
    return true;
}

static bool readEmbeddedPcmB64FromClipState(const juce::ValueTree& clipState, juce::String& outConcat)
{
    outConcat = clipState.getProperty("audioPcmBase64").toString();
    if (outConcat.isNotEmpty()) {
        return true;
    }

    const auto shardsTree = clipState.getChildWithName("AudioPCMShards");
    if (!shardsTree.isValid()) {
        return false;
    }

    outConcat.clear();
    for (int i = 0; i < shardsTree.getNumChildren(); ++i) {
        const auto st = shardsTree.getChild(i);
        if (st.hasType("S")) {
            outConcat += st.getProperty("data").toString();
        }
    }
    return outConcat.isNotEmpty();
}

static juce::ValueTree pitchCurveToValueTree(const PitchCurve& pc)
{
    juce::ValueTree curveState("PitchCurve");
    auto snapshot = pc.getSnapshot();
    curveState.setProperty("hopSize", snapshot->getHopSize(), nullptr);
    curveState.setProperty("f0SampleRate", snapshot->getSampleRate(), nullptr);
    curveState.setProperty("originalF0", encodeFloatVectorBase64(snapshot->getOriginalF0()), nullptr);
    curveState.setProperty("originalEnergy", encodeFloatVectorBase64(snapshot->getOriginalEnergy()), nullptr);

    const auto& segments = snapshot->getCorrectedSegments();
    for (const auto& seg : segments) {
        juce::ValueTree segState("Segment");
        segState.setProperty("start", seg.startFrame, nullptr);
        segState.setProperty("end", seg.endFrame, nullptr);
        segState.setProperty("source", static_cast<int>(seg.source), nullptr);
        segState.setProperty("retuneSpeed", seg.retuneSpeed, nullptr);
        segState.setProperty("vibratoDepth", seg.vibratoDepth, nullptr);
        segState.setProperty("vibratoRate", seg.vibratoRate, nullptr);
        segState.setProperty("f0", encodeFloatVectorBase64(seg.f0Data), nullptr);
        curveState.addChild(segState, -1, nullptr);
    }

    return curveState;
}

static void restorePitchCurveFromValueTree(PitchCurve& pc, const juce::ValueTree& curveState)
{
    std::vector<float> originalF0;
    std::vector<float> originalEnergy;
    decodeFloatVectorBase64(curveState.getProperty("originalF0"), originalF0);
    decodeFloatVectorBase64(curveState.getProperty("originalEnergy"), originalEnergy);

    pc.setHopSize(static_cast<int>(curveState.getProperty("hopSize", 0)));
    pc.setSampleRate(static_cast<double>(curveState.getProperty("f0SampleRate", 0.0)));
    pc.setOriginalF0(originalF0);
    pc.setOriginalEnergy(originalEnergy);
    pc.clearAllCorrections();

    for (auto child : curveState) {
        if (!child.hasType("Segment")) {
            continue;
        }

        CorrectedSegment seg;
        seg.startFrame = static_cast<int>(child.getProperty("start", 0));
        seg.endFrame = static_cast<int>(child.getProperty("end", 0));
        seg.source = static_cast<CorrectedSegment::Source>(static_cast<int>(child.getProperty("source", 0)));
        seg.retuneSpeed = static_cast<float>(static_cast<double>(child.getProperty("retuneSpeed", 100.0)));
        seg.vibratoDepth = static_cast<float>(static_cast<double>(child.getProperty("vibratoDepth", 0.0)));
        seg.vibratoRate = static_cast<float>(static_cast<double>(child.getProperty("vibratoRate", 7.5)));
        decodeFloatVectorBase64(child.getProperty("f0"), seg.f0Data);

        if (seg.startFrame < seg.endFrame && !seg.f0Data.empty()) {
            pc.restoreCorrectedSegment(seg);
        }
    }
}

struct LoadedTrackData {
    juce::String name;
    juce::Colour colour{0xff000000};
    bool isMuted{false};
    bool isSolo{false};
    float volume{1.0f};
    int selectedClipIndex{0};
    std::vector<OpenTuneAudioProcessor::TrackState::AudioClip> clips;
};

struct ProjectFileGlobals {
    double bpm{120.0};
    double zoomLevel{1.0};
    int trackHeight{120};
    int activeTrackId{0};
    bool showWaveform{true};
    bool showLanes{true};
    bool loopEnabled{false};
    uint64_t nextClipId{1};
};

static bool clipValueTreeHasPitchData(const juce::ValueTree& curveState)
{
    if (!curveState.isValid()) {
        return false;
    }
    if (curveState.getProperty("originalF0").toString().isNotEmpty()) {
        return true;
    }
    if (curveState.getProperty("originalEnergy").toString().isNotEmpty()) {
        return true;
    }
    if (static_cast<int>(curveState.getProperty("hopSize", 0)) != 0) {
        return true;
    }
    for (int i = 0; i < curveState.getNumChildren(); ++i) {
        if (curveState.getChild(i).hasType("Segment")) {
            return true;
        }
    }
    return false;
}

static bool hasReadyOriginalF0Curve(const std::shared_ptr<PitchCurve>& curve)
{
    if (!curve) {
        return false;
    }
    return !curve->getSnapshot()->getOriginalF0().empty();
}

static bool doesSourceFileMatchClipBuffer(const juce::File& file,
                                          const OpenTuneAudioProcessor::TrackState::AudioClip& clip)
{
    if (!file.existsAsFile() || !clip.audioBuffer || clip.audioBuffer->getNumSamples() <= 0) {
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) {
        return false;
    }

    const int fileCh = static_cast<int>(reader->numChannels);
    if (fileCh != clip.audioBuffer->getNumChannels()) {
        return false;
    }

    const double fileDur = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const double clipDur =
        TimeCoordinate::samplesToSeconds(clip.audioBuffer->getNumSamples(), TimeCoordinate::kRenderSampleRate);
    return std::abs(fileDur - clipDur) < 0.05;
}

static juce::File resolveStoredAudioPath(const juce::File& projectFileOnDisk, const juce::String& stored)
{
    if (stored.isEmpty()) {
        return {};
    }
    if (juce::File::isAbsolutePath(stored)) {
        return juce::File(stored);
    }
    return projectFileOnDisk.getParentDirectory().getChildFile(stored);
}

static juce::String storedPathForProjectFile(const juce::File& projectFileOnDisk, const juce::File& audioFile)
{
    const juce::File projDir = projectFileOnDisk.getParentDirectory();
    if (audioFile.isAChildOf(projDir) || audioFile.getParentDirectory() == projDir) {
        return audioFile.getRelativePathFrom(projDir);
    }
    return audioFile.getFullPathName();
}

static bool writeHostRateDryWavFile(const juce::AudioBuffer<float>& buf, const juce::File& outFile)
{
    if (buf.getNumChannels() <= 0 || buf.getNumSamples() <= 0) {
        return false;
    }

    juce::File path = outFile;
    if (!path.hasFileExtension(".wav")) {
        path = path.withFileExtension(".wav");
    }
    (void)path.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(path.createOutputStream());
    if (!stream) {
        return false;
    }

    std::unique_ptr<juce::OutputStream> outStream(stream.release());
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate(TimeCoordinate::kRenderSampleRate)
                       .withNumChannels(buf.getNumChannels())
                       .withBitsPerSample(static_cast<int>(sizeof(float) * 8));
    auto writer = wav.createWriterFor(outStream, options);
    if (!writer) {
        return false;
    }
    return writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}

static bool parseProjectClip(const juce::ValueTree& clipState,
                             OpenTuneAudioProcessor::TrackState::AudioClip& clip,
                             int schemaVersion,
                             OpenTuneAudioProcessor* processorForAudioLoad,
                             const juce::File& projectFileOnDisk)
{
    if (!clipState.hasType("Clip")) {
        return false;
    }

    const auto clipId = static_cast<uint64_t>(static_cast<juce::int64>(clipState.getProperty("clipId", 0)));
    if (clipId == 0) {
        return false;
    }

    juce::AudioBuffer<float> hostBuffer;
    juce::String sourcePath;

    if (schemaVersion >= 2) {
        sourcePath = clipState.getProperty("sourceAudioPath", "").toString();
    }

    bool haveHostAudio = false;
    bool loadedFromSourcePathFile = false;
    if (schemaVersion >= 2 && sourcePath.isNotEmpty() && processorForAudioLoad != nullptr) {
        const juce::File srcFile = resolveStoredAudioPath(projectFileOnDisk, sourcePath);
        const int expectCh = static_cast<int>(clipState.getProperty("audioChannels", 0));
        const int expectN = static_cast<int>(clipState.getProperty("audioSamples", 0));
        if (srcFile.existsAsFile()
            && processorForAudioLoad->loadAudioFileToHostRateBuffer(srcFile, hostBuffer)
            && expectCh > 0
            && expectN > 0
            && hostBuffer.getNumChannels() == expectCh
            && hostBuffer.getNumSamples() == expectN) {
            haveHostAudio = true;
            loadedFromSourcePathFile = true;
        }
    }

    if (!haveHostAudio && schemaVersion >= 3) {
        return false;
    }

    if (!haveHostAudio) {
        const int nCh = static_cast<int>(clipState.getProperty("audioChannels", 0));
        const int nSamp = static_cast<int>(clipState.getProperty("audioSamples", 0));
        const double fileSr =
            static_cast<double>(clipState.getProperty("audioSampleRate", TimeCoordinate::kRenderSampleRate));
        if (nCh <= 0 || nSamp <= 0) {
            return false;
        }
        if (std::abs(fileSr - TimeCoordinate::kRenderSampleRate) > 1.0) {
            return false;
        }

        juce::String b64;
        if (!readEmbeddedPcmB64FromClipState(clipState, b64)) {
            return false;
        }
        if (!decodeAudioBufferBase64(b64, nCh, nSamp, hostBuffer)) {
            return false;
        }
        haveHostAudio = true;
    }

    if (!haveHostAudio || hostBuffer.getNumSamples() <= 0) {
        return false;
    }

    clip = OpenTuneAudioProcessor::TrackState::AudioClip();
    clip.clipId = clipId;
    clip.sourceAudioAbsolutePath =
        loadedFromSourcePathFile ? resolveStoredAudioPath(projectFileOnDisk, sourcePath).getFullPathName()
                                 : juce::String();
    clip.startSeconds = static_cast<double>(clipState.getProperty("startSeconds", 0.0));
    clip.gain = static_cast<float>(static_cast<double>(clipState.getProperty("gain", 1.0)));
    clip.fadeInDuration = static_cast<double>(clipState.getProperty("fadeInDuration", 0.0));
    clip.fadeOutDuration = static_cast<double>(clipState.getProperty("fadeOutDuration", 0.0));
    clip.name = clipState.getProperty("name", "").toString();
    clip.colour = juce::Colour(static_cast<juce::uint32>(static_cast<int>(clipState.getProperty("colour", 0))));
    clip.audioBuffer = std::make_shared<const juce::AudioBuffer<float>>(std::move(hostBuffer));
    clip.originalF0State =
        static_cast<OriginalF0State>(static_cast<int>(clipState.getProperty("originalF0State", 0)));

    DetectedKey dk;
    dk.root = static_cast<Key>(juce::jlimit(0, 11, static_cast<int>(clipState.getProperty("keyRoot", 0))));
    dk.scale = static_cast<Scale>(juce::jlimit(0, 2, static_cast<int>(clipState.getProperty("keyScale", 0))));
    dk.confidence = static_cast<float>(static_cast<double>(clipState.getProperty("keyConfidence", 0.0)));
    clip.detectedKey = dk;

    auto curveState = clipState.getChildWithName("PitchCurve");
    if (clipValueTreeHasPitchData(curveState)) {
        clip.pitchCurve = std::make_shared<PitchCurve>();
        restorePitchCurveFromValueTree(*clip.pitchCurve, curveState);
        if (hasReadyOriginalF0Curve(clip.pitchCurve)) {
            clip.originalF0State = OriginalF0State::Ready;
        }
    }

    clip.notes.clear();
    auto notesTree = clipState.getChildWithName("Notes");
    if (notesTree.isValid()) {
        for (auto nv : notesTree) {
            if (!nv.hasType("Note")) {
                continue;
            }

            Note n;
            n.startTime = static_cast<double>(nv.getProperty("startTime", 0.0));
            n.endTime = static_cast<double>(nv.getProperty("endTime", 0.0));
            n.pitch = static_cast<float>(static_cast<double>(nv.getProperty("pitch", 0.0)));
            n.originalPitch = static_cast<float>(static_cast<double>(nv.getProperty("originalPitch", 0.0)));
            n.pitchOffset = static_cast<float>(static_cast<double>(nv.getProperty("pitchOffset", 0.0)));
            n.retuneSpeed = static_cast<float>(static_cast<double>(nv.getProperty("retuneSpeed", -1.0)));
            n.vibratoDepth = static_cast<float>(static_cast<double>(nv.getProperty("vibratoDepth", -1.0)));
            n.vibratoRate = static_cast<float>(static_cast<double>(nv.getProperty("vibratoRate", -1.0)));
            n.velocity = static_cast<float>(static_cast<double>(nv.getProperty("velocity", 1.0)));
            n.isVoiced = static_cast<bool>(nv.getProperty("isVoiced", true));
            n.selected = static_cast<bool>(nv.getProperty("selected", false));
            n.dirty = static_cast<bool>(nv.getProperty("dirty", false));
            clip.notes.push_back(n);
        }
    }

    clip.silentGaps.clear();
    auto gapsTree = clipState.getChildWithName("SilentGaps");
    if (gapsTree.isValid()) {
        for (auto gv : gapsTree) {
            if (!gv.hasType("SilentGap")) {
                continue;
            }

            SilentGap g;
            g.startSeconds = static_cast<double>(gv.getProperty("start", 0.0));
            g.endSeconds = static_cast<double>(gv.getProperty("end", 0.0));
            g.minLevel_dB = static_cast<float>(static_cast<double>(gv.getProperty("minDb", -100.0)));
            clip.silentGaps.push_back(g);
        }
    }

    clip.renderCache = std::make_shared<RenderCache>();
    return true;
}

static bool parseProjectFileTree(const juce::ValueTree& root,
                                 std::array<LoadedTrackData, OpenTuneAudioProcessor::MAX_TRACKS>& outTracks,
                                 ProjectFileGlobals& outGlobals,
                                 OpenTuneAudioProcessor* processorForAudioLoad,
                                 const juce::File& projectFileOnDisk)
{
    if (!root.hasType("OpenTuneProject")) {
        return false;
    }

    const int schema = static_cast<int>(root.getProperty("schemaVersion", 0));
    if (schema < 1 || schema > 3) {
        return false;
    }

    outGlobals.bpm = static_cast<double>(root.getProperty("bpm", 120.0));
    outGlobals.zoomLevel = static_cast<double>(root.getProperty("zoomLevel", 1.0));
    outGlobals.trackHeight = static_cast<int>(root.getProperty("trackHeight", 120));
    outGlobals.activeTrackId =
        juce::jlimit(0, OpenTuneAudioProcessor::MAX_TRACKS - 1, static_cast<int>(root.getProperty("activeTrackId", 0)));
    outGlobals.showWaveform = static_cast<bool>(root.getProperty("showWaveform", true));
    outGlobals.showLanes = static_cast<bool>(root.getProperty("showLanes", true));
    outGlobals.loopEnabled = static_cast<bool>(root.getProperty("loopEnabled", false));
    outGlobals.nextClipId =
        static_cast<uint64_t>(static_cast<juce::int64>(root.getProperty("nextClipId", static_cast<juce::int64>(1))));

    for (auto& t : outTracks) {
        t = LoadedTrackData();
    }

    const auto tracksTree = root.getChildWithName("Tracks");
    if (!tracksTree.isValid()) {
        return false;
    }

    for (auto trackState : tracksTree) {
        if (!trackState.hasType("Track")) {
            continue;
        }

        const int trackId = static_cast<int>(trackState.getProperty("trackId", -1));
        if (trackId < 0 || trackId >= OpenTuneAudioProcessor::MAX_TRACKS) {
            continue;
        }

        LoadedTrackData& lt = outTracks[static_cast<size_t>(trackId)];
        lt.name = trackState.getProperty("name", "").toString();
        lt.colour = juce::Colour(static_cast<juce::uint32>(static_cast<int>(trackState.getProperty("colour", 0))));
        lt.isMuted = static_cast<bool>(trackState.getProperty("isMuted", false));
        lt.isSolo = static_cast<bool>(trackState.getProperty("isSolo", false));
        lt.volume = static_cast<float>(static_cast<double>(trackState.getProperty("volume", 1.0)));
        lt.selectedClipIndex = static_cast<int>(trackState.getProperty("selectedClipIndex", 0));
        lt.clips.clear();

        for (auto clipState : trackState) {
            if (!clipState.hasType("Clip")) {
                continue;
            }

            OpenTuneAudioProcessor::TrackState::AudioClip clip;
            if (!parseProjectClip(clipState, clip, schema, processorForAudioLoad, projectFileOnDisk)) {
                return false;
            }
            lt.clips.push_back(std::move(clip));
        }

        const int n = static_cast<int>(lt.clips.size());
        lt.selectedClipIndex = n > 0 ? juce::jlimit(0, n - 1, lt.selectedClipIndex) : 0;
    }

    return true;
}

} // namespace

void OpenTuneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedReadLock tracksReadLock(tracksLock_);

    juce::ValueTree state("OpenTuneState");
    state.setProperty("schemaVersion", 2, nullptr);
    state.setProperty("bpm", getBpm(), nullptr);
    state.setProperty("zoomLevel", zoomLevel_, nullptr);
    state.setProperty("trackHeight", trackHeight_, nullptr);

    juce::ValueTree tracksState("Tracks");
    for (int trackId = 0; trackId < MAX_TRACKS; ++trackId) {
        const auto& track = tracks_[trackId];
        juce::ValueTree trackState("Track");
        trackState.setProperty("trackId", trackId, nullptr);

        for (const auto& clip : track.clips) {
            if (!clip.pitchCurve) {
                continue;
            }

            juce::ValueTree clipState("Clip");
            clipState.setProperty("clipId", static_cast<juce::int64>(clip.clipId), nullptr);
            clipState.addChild(pitchCurveToValueTree(*clip.pitchCurve), -1, nullptr);
            trackState.addChild(clipState, -1, nullptr);
        }

        tracksState.addChild(trackState, -1, nullptr);
    }
    state.addChild(tracksState, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void OpenTuneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName("OpenTuneState")) {
        return;
    }

    juce::ValueTree state = juce::ValueTree::fromXml(*xml);
    setBpm(static_cast<double>(state.getProperty("bpm", 120.0)));
    zoomLevel_ = state.getProperty("zoomLevel", 1.0);
    trackHeight_ = state.getProperty("trackHeight", 120);

    const auto tracksState = state.getChildWithName("Tracks");
    if (!tracksState.isValid()) {
        return;
    }

    const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
    for (auto trackState : tracksState) {
        if (!trackState.hasType("Track")) {
            continue;
        }

        const int trackId = static_cast<int>(trackState.getProperty("trackId", -1));
        if (trackId < 0 || trackId >= MAX_TRACKS) {
            continue;
        }

        auto& clips = tracks_[trackId].clips;
        for (auto clipState : trackState) {
            if (!clipState.hasType("Clip")) {
                continue;
            }

            const auto clipId = static_cast<uint64_t>(static_cast<juce::int64>(clipState.getProperty("clipId", 0)));
            if (clipId == 0) {
                continue;
            }

            auto it = std::find_if(clips.begin(), clips.end(), [clipId](const TrackState::AudioClip& c) {
                return c.clipId == clipId;
            });
            if (it == clips.end()) {
                continue;
            }

            auto curveState = clipState.getChildWithName("PitchCurve");
            if (!curveState.isValid()) {
                continue;
            }

            if (!it->pitchCurve) {
                it->pitchCurve = std::make_shared<PitchCurve>();
            }

            restorePitchCurveFromValueTree(*it->pitchCurve, curveState);
            it->originalF0State = hasReadyOriginalF0Curve(it->pitchCurve)
                                      ? OriginalF0State::Ready
                                      : OriginalF0State::NotRequested;
            if (it->renderCache) {
                it->renderCache->clear();
            }
        }
    }
}

bool OpenTuneAudioProcessor::saveProjectToFile(const juce::File& file)
{
    juce::ValueTree root("OpenTuneProject");
    root.setProperty("schemaVersion", 3, nullptr);
    root.setProperty("bpm", getBpm(), nullptr);
    root.setProperty("zoomLevel", zoomLevel_, nullptr);
    root.setProperty("trackHeight", trackHeight_, nullptr);
    root.setProperty("activeTrackId", activeTrackId_, nullptr);
    root.setProperty("showWaveform", getShowWaveform(), nullptr);
    root.setProperty("showLanes", getShowLanes(), nullptr);
    root.setProperty("loopEnabled", isLoopEnabled(), nullptr);
    root.setProperty("nextClipId", static_cast<juce::int64>(nextClipId_.load()), nullptr);

    juce::ValueTree tracksTree("Tracks");
    const juce::ScopedReadLock tracksReadLock(tracksLock_);

    for (int trackId = 0; trackId < MAX_TRACKS; ++trackId) {
        const auto& track = tracks_[static_cast<size_t>(trackId)];
        juce::ValueTree trackState("Track");
        trackState.setProperty("trackId", trackId, nullptr);
        trackState.setProperty("name", track.name, nullptr);
        trackState.setProperty("colour", static_cast<int>(track.colour.getARGB()), nullptr);
        trackState.setProperty("isMuted", track.isMuted, nullptr);
        trackState.setProperty("isSolo", track.isSolo, nullptr);
        trackState.setProperty("volume", track.volume, nullptr);
        trackState.setProperty("selectedClipIndex", track.selectedClipIndex, nullptr);

        for (const auto& clip : track.clips) {
            if (!clip.audioBuffer || clip.audioBuffer->getNumSamples() <= 0) {
                continue;
            }

            juce::ValueTree clipState("Clip");
            clipState.setProperty("clipId", static_cast<juce::int64>(clip.clipId), nullptr);
            clipState.setProperty("startSeconds", clip.startSeconds, nullptr);
            clipState.setProperty("gain", clip.gain, nullptr);
            clipState.setProperty("fadeInDuration", clip.fadeInDuration, nullptr);
            clipState.setProperty("fadeOutDuration", clip.fadeOutDuration, nullptr);
            clipState.setProperty("name", clip.name, nullptr);
            clipState.setProperty("colour", static_cast<int>(clip.colour.getARGB()), nullptr);
            clipState.setProperty("originalF0State", static_cast<int>(clip.originalF0State), nullptr);
            clipState.setProperty("keyRoot", static_cast<int>(clip.detectedKey.root), nullptr);
            clipState.setProperty("keyScale", static_cast<int>(clip.detectedKey.scale), nullptr);
            clipState.setProperty("keyConfidence", clip.detectedKey.confidence, nullptr);
            clipState.setProperty("audioChannels", clip.audioBuffer->getNumChannels(), nullptr);
            clipState.setProperty("audioSamples", clip.audioBuffer->getNumSamples(), nullptr);
            clipState.setProperty("audioSampleRate", TimeCoordinate::kRenderSampleRate, nullptr);

            const juce::File srcFile(clip.sourceAudioAbsolutePath);
            const bool canReferenceSourceFile =
                clip.sourceAudioAbsolutePath.isNotEmpty() && doesSourceFileMatchClipBuffer(srcFile, clip);

            juce::String storedAudioPath;
            if (canReferenceSourceFile) {
                storedAudioPath = storedPathForProjectFile(file, srcFile);
            } else {
                const juce::File mediaDir = file.getSiblingFile(file.getFileNameWithoutExtension() + "_media");
                if (!mediaDir.createDirectory() && !mediaDir.isDirectory()) {
                    return false;
                }
                const juce::File outWav =
                    mediaDir.getChildFile("t" + juce::String(trackId) + "_c" + juce::String(clip.clipId) + ".wav");
                if (!writeHostRateDryWavFile(*clip.audioBuffer, outWav)) {
                    return false;
                }
                storedAudioPath = storedPathForProjectFile(file, outWav);
            }
            clipState.setProperty("sourceAudioPath", storedAudioPath, nullptr);

            if (clip.pitchCurve) {
                clipState.addChild(pitchCurveToValueTree(*clip.pitchCurve), -1, nullptr);
            }

            juce::ValueTree notesTree("Notes");
            for (const auto& n : clip.notes) {
                juce::ValueTree nv("Note");
                nv.setProperty("startTime", n.startTime, nullptr);
                nv.setProperty("endTime", n.endTime, nullptr);
                nv.setProperty("pitch", n.pitch, nullptr);
                nv.setProperty("originalPitch", n.originalPitch, nullptr);
                nv.setProperty("pitchOffset", n.pitchOffset, nullptr);
                nv.setProperty("retuneSpeed", n.retuneSpeed, nullptr);
                nv.setProperty("vibratoDepth", n.vibratoDepth, nullptr);
                nv.setProperty("vibratoRate", n.vibratoRate, nullptr);
                nv.setProperty("velocity", n.velocity, nullptr);
                nv.setProperty("isVoiced", n.isVoiced, nullptr);
                nv.setProperty("selected", n.selected, nullptr);
                nv.setProperty("dirty", n.dirty, nullptr);
                notesTree.addChild(nv, -1, nullptr);
            }
            clipState.addChild(notesTree, -1, nullptr);

            juce::ValueTree gapsTree("SilentGaps");
            for (const auto& g : clip.silentGaps) {
                juce::ValueTree gv("SilentGap");
                gv.setProperty("start", g.startSeconds, nullptr);
                gv.setProperty("end", g.endSeconds, nullptr);
                gv.setProperty("minDb", g.minLevel_dB, nullptr);
                gapsTree.addChild(gv, -1, nullptr);
            }
            clipState.addChild(gapsTree, -1, nullptr);

            trackState.addChild(clipState, -1, nullptr);
        }

        tracksTree.addChild(trackState, -1, nullptr);
    }

    root.addChild(tracksTree, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml(root.createXml());
    if (xml == nullptr) {
        return false;
    }

    const juce::File parent = file.getParentDirectory();
    if (!parent.exists()) {
        (void)parent.createDirectory();
    }

    return xml->writeTo(file);
}

bool OpenTuneAudioProcessor::loadProjectFromFile(const juce::File& file)
{
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName("OpenTuneProject")) {
        return false;
    }

    juce::ValueTree root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid()) {
        return false;
    }

    std::array<LoadedTrackData, MAX_TRACKS> loaded{};
    ProjectFileGlobals globals;
    if (!parseProjectFileTree(root, loaded, globals, this, file)) {
        return false;
    }

    globalUndoManager_.clear();
    setPlaying(false);
    setPosition(0.0);

    uint64_t maxClipIdSeen = 0;
    {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        const double deviceSr = currentSampleRate_.load(std::memory_order_relaxed);

        for (int i = 0; i < MAX_TRACKS; ++i) {
            LoadedTrackData& lt = loaded[static_cast<size_t>(i)];
            auto& t = tracks_[static_cast<size_t>(i)];

            t.clips = std::move(lt.clips);
            t.name = lt.name;
            t.colour = lt.colour;
            t.isMuted = lt.isMuted;
            t.isSolo = lt.isSolo;
            t.volume = lt.volume;
            t.selectedClipIndex = lt.selectedClipIndex;
            t.currentRMS.store(-100.0f);

            for (auto& c : t.clips) {
                maxClipIdSeen = std::max(maxClipIdSeen, c.clipId);
                resampleDrySignal(c, deviceSr);
            }
        }

        anyTrackSoloed_ = false;
        for (const auto& t : tracks_) {
            if (t.isSolo) {
                anyTrackSoloed_ = true;
                break;
            }
        }

        const uint64_t assignedNext = std::max(globals.nextClipId, maxClipIdSeen > 0 ? maxClipIdSeen + 1 : 1);
        nextClipId_.store(assignedNext);
    }

    setBpm(globals.bpm);
    zoomLevel_ = globals.zoomLevel;
    trackHeight_ = globals.trackHeight;
    activeTrackId_ = globals.activeTrackId;
    setShowWaveform(globals.showWaveform);
    setShowLanes(globals.showLanes);
    setLoopEnabled(globals.loopEnabled);

    bumpEditVersion();

    struct PendingRenderInfo {
        int trackId;
        int clipIndex;
        double duration;
    };

    std::vector<PendingRenderInfo> clipsToRender;
    {
        const juce::ScopedReadLock rl(tracksLock_);
        for (int trackId = 0; trackId < MAX_TRACKS; ++trackId) {
            const auto& track = tracks_[static_cast<size_t>(trackId)];
            for (int clipIdx = 0; clipIdx < static_cast<int>(track.clips.size()); ++clipIdx) {
                const auto& clip = track.clips[static_cast<size_t>(clipIdx)];
                if (clip.pitchCurve && clip.pitchCurve->hasAnyCorrection() && clip.audioBuffer) {
                    const double clipDuration =
                        TimeCoordinate::samplesToSeconds(clip.audioBuffer->getNumSamples(), TimeCoordinate::kRenderSampleRate);
                    clipsToRender.push_back({trackId, clipIdx, clipDuration});
                }
            }
        }
    }

    for (const auto& info : clipsToRender) {
        enqueuePartialRender(info.trackId, info.clipIndex, 0.0, info.duration);
    }

    return true;
}

void OpenTuneAudioProcessor::resetToNewEmptyProject()
{
    globalUndoManager_.clear();
    setPlaying(false);
    setPosition(0.0);

    {
        const juce::ScopedWriteLock tracksWriteLock(tracksLock_);
        for (int i = 0; i < MAX_TRACKS; ++i) {
            auto& t = tracks_[static_cast<size_t>(i)];
            t.clips.clear();
            t.name = "Track " + juce::String(i + 1);
            t.colour = juce::Colour::fromHSV(i * 0.3f, 0.6f, 0.8f, 1.0f);
            t.isMuted = false;
            t.isSolo = false;
            t.volume = 1.0f;
            t.selectedClipIndex = 0;
            t.currentRMS.store(-100.0f);
        }
        anyTrackSoloed_ = false;
        nextClipId_.store(1);
    }

    setBpm(120.0);
    zoomLevel_ = 1.0;
    trackHeight_ = 120;
    activeTrackId_ = 0;
    setShowWaveform(true);
    setShowLanes(true);
    setLoopEnabled(false);

    bumpEditVersion();
}

} // namespace OpenTune
