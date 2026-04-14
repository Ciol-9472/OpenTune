#include "DiffSingerBridgeClient.h"

#include "DsJsonIO.h"

#include "../Utils/SingingEditDocument.h"

#include <thread>

#include <chrono>

#include <vector>



#if JUCE_WINDOWS

#ifndef NOMINMAX

#define NOMINMAX

#endif

#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#include <windows.h>

#endif



namespace OpenTune {



namespace {



juce::File pythonPackageParentDirectory()

{

    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    const juce::File exeDir = exe.getParentDirectory();

    return exeDir;

}



juce::File pythonModuleDirectory()

{

    return pythonPackageParentDirectory().getChildFile("python");

}



#if JUCE_WINDOWS



static void closeWinHandle(void*& h)

{

    if (h != nullptr) {

        CloseHandle(static_cast<HANDLE>(h));

        h = nullptr;

    }

}



static bool readAll(HANDLE h, void* dst, size_t len)

{

    auto* out = static_cast<std::uint8_t*>(dst);

    size_t off = 0;

    while (off < len) {

        DWORD got = 0;

        const DWORD chunk = static_cast<DWORD>(juce::jmin<size_t>(static_cast<size_t>(1) << 20, len - off));

        if (!ReadFile(h, out + off, chunk, &got, nullptr) || got == 0) {

            return false;

        }

        off += static_cast<size_t>(got);

    }

    return true;

}



static bool writeAll(HANDLE h, const void* src, size_t len)

{

    const auto* in = static_cast<const std::uint8_t*>(src);

    size_t off = 0;

    while (off < len) {

        DWORD wrote = 0;

        const DWORD chunk = static_cast<DWORD>(juce::jmin<size_t>(static_cast<size_t>(1) << 20, len - off));

        if (!WriteFile(h, in + off, chunk, &wrote, nullptr) || wrote == 0) {

            return false;

        }

        off += static_cast<size_t>(wrote);

    }

    return true;

}



#endif



} // namespace



DiffSingerBridgeClient::DiffSingerBridgeClient() = default;



DiffSingerBridgeClient::~DiffSingerBridgeClient()

{

    shutdownService();

}



void DiffSingerBridgeClient::shutdownService()

{

#if JUCE_WINDOWS

    closeWinHandle(winStdinWrite_);

    closeWinHandle(winStdoutRead_);

    closeWinHandle(winStderrNull_);

    if (winChildProcess_ != nullptr) {

        HANDLE p = static_cast<HANDLE>(winChildProcess_);

        TerminateProcess(p, 1);

        WaitForSingleObject(p, 2000);

        CloseHandle(p);

        winChildProcess_ = nullptr;

    }

#endif

    running_.store(false, std::memory_order_release);

}



void DiffSingerBridgeClient::closeWindowsService()

{

#if JUCE_WINDOWS

    shutdownService();

#endif

}



void DiffSingerBridgeClient::markUnavailable(const juce::String& reason)

{

    juce::ignoreUnused(reason);

    closeWindowsService();

    ++restartCount_;

    if (restartCount_ > 2) {

        cooldownUntilMs_ = juce::Time::currentTimeMillis() + 60000;

    }

}



#if JUCE_WINDOWS



bool DiffSingerBridgeClient::writeFramedJsonRequest(const juce::String& utf8Payload, juce::String& errorOut)

{

    if (winStdinWrite_ == nullptr) {

        errorOut = "Bridge stdin not open";

        return false;

    }

    const juce::CharPointer_UTF8 utf8 = utf8Payload.toUTF8();

    const size_t byteLen = utf8.sizeInBytes();

    if (byteLen > static_cast<size_t>(64u * 1024u * 1024u)) {

        errorOut = "Request too large";

        return false;

    }

    const auto len32 = static_cast<std::uint32_t>(byteLen);

    std::uint8_t hdr[4] = {static_cast<std::uint8_t>((len32 >> 24) & 0xffu),

                            static_cast<std::uint8_t>((len32 >> 16) & 0xffu),

                            static_cast<std::uint8_t>((len32 >> 8) & 0xffu),

                            static_cast<std::uint8_t>(len32 & 0xffu)};

    HANDLE w = static_cast<HANDLE>(winStdinWrite_);

    if (!writeAll(w, hdr, 4)) {

        errorOut = "Failed to write frame header";

        markUnavailable("write header");

        return false;

    }

    if (byteLen > 0 && !writeAll(w, utf8.getAddress(), byteLen)) {

        errorOut = "Failed to write frame body";

        markUnavailable("write body");

        return false;

    }

    return true;

}



bool DiffSingerBridgeClient::readFramedJsonResponse(juce::String& bodyOut, juce::String& errorOut)

{

    if (winStdoutRead_ == nullptr) {

        errorOut = "Bridge stdout not open";

        return false;

    }

    HANDLE r = static_cast<HANDLE>(winStdoutRead_);

    std::uint8_t hdr[4]{};

    if (!readAll(r, hdr, 4)) {

        errorOut = "Failed to read response header";

        markUnavailable("read header");

        return false;

    }

    const std::uint32_t len =

        (static_cast<std::uint32_t>(hdr[0]) << 24u) | (static_cast<std::uint32_t>(hdr[1]) << 16u)

        | (static_cast<std::uint32_t>(hdr[2]) << 8u) | static_cast<std::uint32_t>(hdr[3]);

    if (len > static_cast<std::uint32_t>(64u * 1024u * 1024u)) {

        errorOut = "Response too large";

        markUnavailable("response size");

        return false;

    }

    std::vector<std::uint8_t> buf(static_cast<size_t>(len));

    if (len > 0 && !readAll(r, buf.data(), static_cast<size_t>(len))) {

        errorOut = "Failed to read response body";

        markUnavailable("read body");

        return false;

    }

    bodyOut = juce::String::fromUTF8(reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()));

    return true;

}



#endif



bool DiffSingerBridgeClient::ensureServiceRunning(juce::String& errorOut)

{

    const int64_t now = juce::Time::currentTimeMillis();

    if (cooldownUntilMs_ > now) {

        errorOut = "DiffSinger bridge in cooldown";

        return false;

    }



    const juce::File workDir = pythonModuleDirectory();

    const juce::File marker = workDir.getChildFile("opentune_ds_service").getChildFile("__main__.py");

    if (!marker.existsAsFile()) {

        errorOut = "Python service not found: " + marker.getFullPathName();

        return false;

    }



    const juce::String debugEnv = juce::SystemStats::getEnvironmentVariable("OPENTUNE_DS_BRIDGE_DEBUG", {});

    debugSidecar_ = debugEnv == "1";



#if JUCE_WINDOWS

    if (winChildProcess_ != nullptr) {

        DWORD exitCode = STILL_ACTIVE;

        if (GetExitCodeProcess(static_cast<HANDLE>(winChildProcess_), &exitCode) != 0 && exitCode == STILL_ACTIVE) {

            running_.store(true, std::memory_order_release);

            return true;

        }

        closeWindowsService();

    }



    wchar_t pyExe[MAX_PATH]{};

    const DWORD found = SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, pyExe, nullptr);

    if (found == 0 || found >= MAX_PATH) {

        errorOut = "python.exe not found on PATH";

        return false;

    }



    SECURITY_ATTRIBUTES sa{};

    sa.nLength = sizeof(sa);

    sa.bInheritHandle = TRUE;

    sa.lpSecurityDescriptor = nullptr;



    HANDLE hChildStdoutRead = nullptr;

    HANDLE hChildStdoutWrite = nullptr;

    HANDLE hChildStdinRead = nullptr;

    HANDLE hChildStdinWrite = nullptr;

    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0)) {

        errorOut = "CreatePipe stdout failed";

        return false;

    }

    if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdoutWrite);

        errorOut = "SetHandleInformation stdout failed";

        return false;

    }



    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0)) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdoutWrite);

        errorOut = "CreatePipe stdin failed";

        return false;

    }

    if (!SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdoutWrite);

        CloseHandle(hChildStdinRead);

        CloseHandle(hChildStdinWrite);

        errorOut = "SetHandleInformation stdin failed";

        return false;

    }



    HANDLE hStderrNull =

        CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hStderrNull == INVALID_HANDLE_VALUE) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdoutWrite);

        CloseHandle(hChildStdinRead);

        CloseHandle(hChildStdinWrite);

        errorOut = "Open NUL failed";

        return false;

    }

    if (!SetHandleInformation(hStderrNull, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdoutWrite);

        CloseHandle(hChildStdinRead);

        CloseHandle(hChildStdinWrite);

        CloseHandle(hStderrNull);

        errorOut = "SetHandleInformation NUL failed";

        return false;

    }



    STARTUPINFOW si{};

    si.cb = sizeof(si);

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

    si.wShowWindow = SW_HIDE;

    si.hStdOutput = hChildStdoutWrite;

    si.hStdInput = hChildStdinRead;

    si.hStdError = hStderrNull;



    PROCESS_INFORMATION pi{};



    std::wstring cmd = L"\"";

    cmd += pyExe;

    cmd += L"\" -m opentune_ds_service --framed-stdio";

    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());

    cmdLine.push_back(0);



    const juce::String wdStr = workDir.getFullPathName();

    std::vector<wchar_t> wdBuf(wdStr.toWideCharPointer(), wdStr.toWideCharPointer() + wdStr.length());

    wdBuf.push_back(0);



    const BOOL ok = CreateProcessW(nullptr,

                                   cmdLine.data(),

                                   nullptr,

                                   nullptr,

                                   TRUE,

                                   CREATE_NO_WINDOW,

                                   nullptr,

                                   wdBuf.data(),

                                   &si,

                                   &pi);

    CloseHandle(hChildStdoutWrite);

    CloseHandle(hChildStdinRead);

    CloseHandle(hStderrNull);

    if (!ok) {

        CloseHandle(hChildStdoutRead);

        CloseHandle(hChildStdinWrite);

        errorOut = "CreateProcess python failed";

        return false;

    }

    CloseHandle(pi.hThread);



    winStdinWrite_ = hChildStdinWrite;

    winStdoutRead_ = hChildStdoutRead;

    winChildProcess_ = pi.hProcess;

    winStderrNull_ = nullptr;



    restartCount_ = 0;

    cooldownUntilMs_ = 0;

    running_.store(true, std::memory_order_release);

    return true;

#else

    errorOut = "DiffSinger refine bridge is only available on Windows in this build";

    return false;

#endif

}



bool DiffSingerBridgeClient::requestRefineDurations(SingingEditDocument& doc, uint64_t clipId, double clipDurationSec,

                                                      uint64_t clipGeneration, uint64_t requestId, int sampleRate,

                                                      juce::String& errorOut)

{

    if (!ensureServiceRunning(errorOut)) {

        return false;

    }

    const juce::String payload = DsJson::buildRefineDurationsRequest(

        doc, clipId, clipDurationSec, clipGeneration, requestId, sampleRate);

    juce::String response;

#if JUCE_WINDOWS

    if (!writeFramedJsonRequest(payload, errorOut)) {

        return false;

    }

    if (!readFramedJsonResponse(response, errorOut)) {

        return false;

    }

#else

    errorOut = "DiffSinger refine bridge is only available on Windows in this build";

    return false;

#endif

    response = response.trim();

    if (response.isEmpty()) {

        errorOut = "Bridge returned empty response";

#if JUCE_WINDOWS

        markUnavailable("empty response");

#endif

        return false;

    }

    if (!DsJson::tryApplyRefineDurationsResponse(response, doc, clipId, clipGeneration, requestId, errorOut)) {

        return false;

    }

    if (debugSidecar_) {

        const juce::File bridgeJobs =

            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OpenTune").getChildFile("ds_bridge_debug");

        const juce::File jobDir = bridgeJobs.getChildFile(juce::Uuid().toString());

        (void)jobDir.createDirectory();

        (void)jobDir.getChildFile("request.json").replaceWithText(payload);

        (void)jobDir.getChildFile("response.json").replaceWithText(response);

    }

    return true;

}



} // namespace OpenTune

