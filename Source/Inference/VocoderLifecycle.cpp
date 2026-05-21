#include "VocoderLifecycle.h"
#include "VocoderInferenceService.h"
#include "VocoderRenderScheduler.h"
#include "../Utils/AppLogger.h"

namespace OpenTune {

class VocoderLifecycle::Impl {
public:
    Impl() : inferenceService_(std::make_unique<VocoderInferenceService>()) {}

    bool initialize(const std::string& modelDir) {
        if (!inferenceService_->initialize(modelDir)) {
            AppLogger::error("[VocoderLifecycle] Failed to initialize inference service");
            return false;
        }

        scheduler_ = std::make_unique<VocoderRenderScheduler>();
        if (!scheduler_->initialize(inferenceService_.get())) {
            AppLogger::error("[VocoderLifecycle] Failed to initialize scheduler");
            scheduler_.reset();
            inferenceService_->shutdown();
            return false;
        }

        AppLogger::info("[VocoderLifecycle] Initialized successfully");
        return true;
    }

    void shutdown() {
        if (scheduler_) {
            scheduler_->shutdown();
            scheduler_.reset();
        }
        if (inferenceService_) {
            inferenceService_->shutdown();
        }
        AppLogger::info("[VocoderLifecycle] Shutdown complete");
    }

    int getQueueDepth() const {
        return scheduler_ ? scheduler_->getQueueDepth() : 0;
    }

    bool isRunning() const {
        return scheduler_ && scheduler_->isRunning();
    }

    bool isInitialized() const {
        return inferenceService_ && inferenceService_->isInitialized();
    }

    int getVocoderHopSize() const {
        return inferenceService_ ? inferenceService_->getVocoderHopSize() : 0;
    }

    int getMelBins() const {
        return inferenceService_ ? inferenceService_->getMelBins() : 0;
    }

    VocoderInferenceService* getInferenceService() const {
        return inferenceService_.get();
    }

    VocoderRenderScheduler* getScheduler() const {
        return scheduler_.get();
    }

private:
    std::unique_ptr<VocoderInferenceService> inferenceService_;
    std::unique_ptr<VocoderRenderScheduler> scheduler_;
};

VocoderLifecycle::VocoderLifecycle() : pImpl_(std::make_unique<Impl>()) {}

VocoderLifecycle::~VocoderLifecycle() {
    if (pImpl_) {
        pImpl_->shutdown();
    }
}

bool VocoderLifecycle::initialize(const std::string& modelDir) {
    return pImpl_->initialize(modelDir);
}

void VocoderLifecycle::shutdown() {
    pImpl_->shutdown();
}

bool VocoderLifecycle::isInitialized() const {
    return pImpl_->isInitialized();
}

bool VocoderLifecycle::isRunning() const {
    return pImpl_->isRunning();
}

int VocoderLifecycle::getQueueDepth() const {
    return pImpl_->getQueueDepth();
}

int VocoderLifecycle::getVocoderHopSize() const {
    return pImpl_->getVocoderHopSize();
}

int VocoderLifecycle::getMelBins() const {
    return pImpl_->getMelBins();
}

VocoderInferenceService* VocoderLifecycle::getInferenceService() const {
    return pImpl_->getInferenceService();
}

VocoderRenderScheduler* VocoderLifecycle::getScheduler() const {
    return pImpl_->getScheduler();
}

} // namespace OpenTune
