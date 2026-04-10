#include "ModelFactory.h"
#include "DmlConfig.h"
#include "RMVPEExtractor.h"
#include "../DSP/ResamplingManager.h"
#include "../Utils/CpuBudgetManager.h"
#include "../Utils/AccelerationDetector.h"
#include "../Utils/AppLogger.h"
#include "../Utils/Error.h"
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <iomanip>
#include <unordered_map>
#if defined(_WIN32)
#include <dml_provider_factory.h>
#endif

namespace OpenTune {

namespace {

bool shouldEnableOrtProfilingInDebug()
{
#if JUCE_DEBUG
    const char* envValue = std::getenv("OPENTUNE_ORT_PROFILE");
    if (envValue == nullptr) {
        return false;
    }

    const juce::String normalized = juce::String(envValue).trim().toLowerCase();
    return normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes";
#else
    return false;
#endif
}

void logOnnxSessionCpuConfig(const CpuBudgetManager::BudgetConfig& budget)
{
    AppLogger::info("[ModelFactory] ONNX session CPU config: totalBudget=" + juce::String(budget.totalBudget)
              + " onnxIntra=" + juce::String(budget.onnxIntra)
              + " onnxInter=" + juce::String(budget.onnxInter)
              + " sequential=" + juce::String(budget.onnxSequential ? 1 : 0)
              + " allowSpinning=" + juce::String(budget.allowSpinning ? 1 : 0));
}

/** Git LFS checkout without `git lfs pull` leaves tiny pointer files; ORT then fails with "Protobuf parsing failed". */
bool isGitLfsPointerFile(const juce::File& file)
{
    if (!file.existsAsFile()) {
        return false;
    }
    const juce::int64 sz = file.getSize();
    if (sz <= 0 || sz > 1024) {
        return false;
    }
    juce::FileInputStream in(file);
    if (!in.openedOk()) {
        return false;
    }
    const juce::String line = in.readNextLine().trimStart();
    return line.startsWith("version https://git-lfs.github.com/spec/v1");
}

Ort::SessionOptions buildF0SessionOptionsImpl(bool allowWin32DirectML,
                                              bool& outGpuMode,
                                              bool& outUsesDedicatedVramForPreflight)
{
    Ort::SessionOptions sessionOptions;

    bool gpuMode = false;
    outUsesDedicatedVramForPreflight = false;

#if defined(_WIN32)
    bool f0DmlAttached = false;
    if (allowWin32DirectML) {
        auto& gpuDet = AccelerationDetector::getInstance();
        if (gpuDet.getSelectedBackend() == AccelerationDetector::AccelBackend::DirectML) {
            auto& api = Ort::GetApi();
            const OrtDmlApi* dmlApi = nullptr;
            OrtStatus* probeStatus = api.GetExecutionProviderApi(
                "DML",
                ORT_API_VERSION,
                reinterpret_cast<const void**>(&dmlApi));
            if (probeStatus != nullptr) {
                const char* msg = api.GetErrorMessage(probeStatus);
                AppLogger::warn("[ModelFactory] F0: GetExecutionProviderApi(DML) failed: "
                    + juce::String(msg != nullptr ? msg : ""));
                api.ReleaseStatus(probeStatus);
            } else if (dmlApi != nullptr) {
                DmlConfig cfg;
                cfg.deviceId = gpuDet.getDirectMLDeviceId();
                cfg.performancePreference = 1;
                cfg.deviceFilter = 1;
                OrtDmlDeviceOptions devOpts{};
                devOpts.Preference = static_cast<OrtDmlPerformancePreference>(cfg.performancePreference);
                devOpts.Filter = static_cast<OrtDmlDeviceFilter>(cfg.deviceFilter);
                OrtStatus* dmlStatus = dmlApi->SessionOptionsAppendExecutionProvider_DML2(sessionOptions, &devOpts);
                if (dmlStatus != nullptr) {
                    const char* msg = api.GetErrorMessage(dmlStatus);
                    AppLogger::warn("[ModelFactory] F0: AppendExecutionProvider DML2 failed: "
                        + juce::String(msg != nullptr ? msg : ""));
                    api.ReleaseStatus(dmlStatus);
                } else {
                    gpuMode = true;
                    f0DmlAttached = true;
                    outUsesDedicatedVramForPreflight = true;
                    sessionOptions.DisableMemPattern();
                    AppLogger::info("[ModelFactory] F0 session: DirectML (DML2) EP added");
                }
            }
        }
    }
#endif

#if defined(__APPLE__)
    try {
        std::unordered_map<std::string, std::string> coremlOptions;
        coremlOptions["ModelFormat"] = "MLProgram";
        coremlOptions["MLComputeUnits"] = "CPUAndGPU";
        sessionOptions.AppendExecutionProvider("CoreML", coremlOptions);
        gpuMode = true;
        AppLogger::info("[ModelFactory] F0 session: CoreML EP added (macOS, MLProgram+CPUAndGPU)");
    } catch (const Ort::Exception& e) {
        AppLogger::warn("[ModelFactory] Failed to add CoreML EP for F0: " + juce::String(e.what()));
        AppLogger::info("[ModelFactory] F0 session: falling back to CPU");
    } catch (const std::exception& e) {
        AppLogger::warn("[ModelFactory] Failed to add CoreML EP for F0: " + juce::String(e.what()));
        AppLogger::info("[ModelFactory] F0 session: falling back to CPU");
    } catch (...) {
        AppLogger::warn("[ModelFactory] Failed to add CoreML EP for F0 (unknown error)");
        AppLogger::info("[ModelFactory] F0 session: falling back to CPU");
    }
#endif

#if defined(_WIN32)
    if (f0DmlAttached) {
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
        sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        sessionOptions.AddConfigEntry("session.intra_op.allow_spinning", "0");
        sessionOptions.AddConfigEntry("session.inter_op.allow_spinning", "0");
        AppLogger::info("[ModelFactory] F0 session: DML thread policy (intra=1 inter=1, sequential)");
    } else
#endif
    {
        const auto budget = CpuBudgetManager::buildConfig(gpuMode);
        sessionOptions.SetIntraOpNumThreads(budget.onnxIntra);
        sessionOptions.SetInterOpNumThreads(budget.onnxInter);
        sessionOptions.SetExecutionMode(budget.onnxSequential ? ExecutionMode::ORT_SEQUENTIAL : ExecutionMode::ORT_PARALLEL);
        sessionOptions.AddConfigEntry("session.intra_op.allow_spinning", budget.allowSpinning ? "1" : "0");
        sessionOptions.AddConfigEntry("session.inter_op.allow_spinning", budget.allowSpinning ? "1" : "0");

        logOnnxSessionCpuConfig(budget);
    }

    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (!gpuMode) {
        AppLogger::info("[ModelFactory] F0 session: CPU-only mode");
    }
    outGpuMode = gpuMode;
    return sessionOptions;
}

} // namespace

// ==============================================================================
// F0 Extractor Creation
// ==============================================================================

ModelFactory::F0ExtractorResult ModelFactory::createF0Extractor(
    F0ModelType type,
    const std::string& modelDir,
    Ort::Env& env,
    std::shared_ptr<ResamplingManager> resampler)
{
    std::string modelPath = getModelPath(type, modelDir);

    if (!isModelAvailable(type, modelDir)) {
        return F0ExtractorResult::failure(ErrorCode::ModelNotFound, 
            "F0 model file: " + modelPath);
    }

    if (isGitLfsPointerFile(juce::File(modelPath))) {
        AppLogger::error("[ModelFactory] F0 model is a Git LFS pointer, not real ONNX: " + juce::String(modelPath));
        return F0ExtractorResult::failure(ErrorCode::ModelLoadFailed,
            "rmvpe.onnx is a Git LFS pointer (approx. 130 bytes), not the model weights. "
            "Install Git LFS, then in the repo root run: git lfs install && git lfs pull "
            "(or obtain the full rmvpe.onnx, ~345 MB, and replace this file).");
    }

    try {
        bool gpuMode = false;
        bool vramPreflight = false;
        auto session = loadF0Session(modelPath, env, gpuMode, vramPreflight);
        if (!session) {
            return F0ExtractorResult::failure(ErrorCode::SessionCreationFailed,
                "Failed to create ONNX session for: " + modelPath);
        }

#if defined(_WIN32)
        const juce::String backendStr = vramPreflight ? "DirectML" : (gpuMode ? "GPU" : "CPU");
#else
        const juce::String backendStr = gpuMode ? "CoreML" : "CPU";
#endif
        AppLogger::info("[ModelFactory] Loaded F0 model (" + backendStr + "): " + juce::String(modelPath));

        switch (type) {
            case F0ModelType::RMVPE:
                return F0ExtractorResult::success(
                    std::make_unique<RMVPEExtractor>(std::move(session), resampler, vramPreflight));
        }

        return F0ExtractorResult::failure(ErrorCode::InvalidModelType,
            "Unknown F0 model type");

    } catch (const Ort::Exception& e) {
        return F0ExtractorResult::failure(ErrorCode::ModelLoadFailed,
            "ONNX error loading F0 model: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return F0ExtractorResult::failure(ErrorCode::ModelLoadFailed,
            "Error loading F0 model: " + std::string(e.what()));
    } catch (...) {
        return F0ExtractorResult::failure(ErrorCode::ModelLoadFailed,
            "Unknown error loading F0 model");
    }
}

// ==============================================================================
// Model Path Resolution
// ==============================================================================

std::string ModelFactory::getModelPath(F0ModelType type, const std::string& modelDir) {
    switch (type) {
        case F0ModelType::RMVPE:
            return modelDir + "/rmvpe.onnx";
    }
    return "";
}

// ==============================================================================
// Model Availability Checking
// ==============================================================================

bool ModelFactory::isModelAvailable(F0ModelType type, const std::string& modelDir) {
    std::string path = getModelPath(type, modelDir);
    juce::File file(path);
    return file.existsAsFile();
}

// ==============================================================================
// Model Discovery
// ==============================================================================

std::vector<F0ModelInfo> ModelFactory::getAvailableF0Models(const std::string& modelDir) {
    std::vector<F0ModelInfo> models;

    F0ModelInfo rmvpe;
    rmvpe.type = F0ModelType::RMVPE;
    rmvpe.name = "rmvpe";
    rmvpe.displayName = "RMVPE (Robust)";
    rmvpe.modelSizeBytes = 361 * 1024 * 1024;
    rmvpe.isAvailable = isModelAvailable(F0ModelType::RMVPE, modelDir);
    models.push_back(rmvpe);

    return models;
}

// ==============================================================================
// F0 Session Options
// ==============================================================================

Ort::SessionOptions ModelFactory::createF0SessionOptions(bool& outGpuMode,
                                                         bool& outUsesDedicatedVramForPreflight) {
    return buildF0SessionOptionsImpl(true, outGpuMode, outUsesDedicatedVramForPreflight);
}

// ==============================================================================
// Session Loading
// ==============================================================================

std::unique_ptr<Ort::Session> ModelFactory::loadF0Session(
    const std::string& modelPath,
    Ort::Env& env,
    bool& outGpuMode,
    bool& outUsesDedicatedVramForPreflight)
{
    auto tryCreate = [&](Ort::SessionOptions& sessionOptions) -> std::unique_ptr<Ort::Session> {
        if (shouldEnableOrtProfilingInDebug()) {
#ifdef _WIN32
            sessionOptions.EnableProfiling(L"opentune_f0_profile");
#else
            sessionOptions.EnableProfiling("opentune_f0_profile");
#endif
            AppLogger::info("[ModelFactory] ORT profiling enabled for F0");
        }

#ifdef _WIN32
        juce::File modelFile(modelPath);
        std::wstring wModelPath = modelFile.getFullPathName().toWideCharPointer();
        return std::make_unique<Ort::Session>(env, wModelPath.c_str(), sessionOptions);
#else
        return std::make_unique<Ort::Session>(env, modelPath.c_str(), sessionOptions);
#endif
    };

    try {
        auto sessionOptions = buildF0SessionOptionsImpl(true, outGpuMode, outUsesDedicatedVramForPreflight);
        return tryCreate(sessionOptions);
    } catch (const Ort::Exception& e) {
#if defined(_WIN32)
        if (outUsesDedicatedVramForPreflight) {
            AppLogger::warn("[ModelFactory] F0 session load failed with DirectML; retrying CPU: "
                + juce::String(e.what()));
            try {
                auto sessionOptions = buildF0SessionOptionsImpl(false, outGpuMode, outUsesDedicatedVramForPreflight);
                return tryCreate(sessionOptions);
            } catch (const Ort::Exception& e2) {
                AppLogger::error("[ModelFactory] F0 CPU session load failed: " + juce::String(e2.what()));
                return nullptr;
            }
        }
#endif
        AppLogger::error("[ModelFactory] Failed to load F0 session: " + juce::String(e.what()));
        return nullptr;
    }
}

} // namespace OpenTune
