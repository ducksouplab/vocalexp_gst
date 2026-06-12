#include "dsp/swift_pitch_tracker.hpp"
#include <onnxruntime_c_api.h>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace vocalexp {

struct SwiftPitchTracker::Impl {
    const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    OrtEnv* env = nullptr;
    OrtSessionOptions* sessionOptions = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memoryInfo = nullptr;

    std::vector<float> audioBuffer;
    const std::size_t modelInputSize = 512; // SWIFT-F0 standard window
    
    float lastF0 = 0.0f;

    Impl(const Config& config) {
        std::string modelPath = "/opt/vocalexp_gst/models/swift_f0.onnx";
        ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "SwiftPitchTracker", &env);
        ort->CreateSessionOptions(&sessionOptions);
        ort->SetIntraOpNumThreads(sessionOptions, 1);
        
        OrtStatus* status = ort->CreateSession(env, modelPath.c_str(), sessionOptions, &session);
        if (status != nullptr) {
            const char* msg = ort->GetErrorMessage(status);
            std::cerr << "[Swift] Error creating session: " << msg << std::endl;
            ort->ReleaseStatus(status);
            session = nullptr;
        }
        ort->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &memoryInfo);
        audioBuffer.reserve(modelInputSize);
    }

    ~Impl() {
        if (session) ort->ReleaseSession(session);
        if (sessionOptions) ort->ReleaseSessionOptions(sessionOptions);
        if (env) ort->ReleaseEnv(env);
        if (memoryInfo) ort->ReleaseMemoryInfo(memoryInfo);
    }
};

SwiftPitchTracker::SwiftPitchTracker(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

SwiftPitchTracker::~SwiftPitchTracker() = default;

void SwiftPitchTracker::reset() {
    impl_->audioBuffer.clear();
    impl_->lastF0 = 0.0f;
}

void SwiftPitchTracker::push(const float* samples, std::size_t n) {
    impl_->audioBuffer.insert(impl_->audioBuffer.end(), samples, samples + n);
}

PitchEstimate SwiftPitchTracker::estimate() {
    if (!impl_->session || impl_->audioBuffer.size() < impl_->modelInputSize) {
        return {impl_->lastF0, 0.0f};
    }

    // Run inference
    const char* inputNames[] = {"input_audio"};
    const char* outputNames[] = {"pitch_hz", "confidence"};
    int64_t inputDims[] = {1, static_cast<int64_t>(impl_->modelInputSize)};
    
    OrtValue* inputValue = nullptr;
    impl_->ort->CreateTensorWithDataAsOrtValue(
        impl_->memoryInfo, 
        impl_->audioBuffer.data(), 
        impl_->modelInputSize * sizeof(float),
        inputDims, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputValue);

    OrtValue* outputValues[2] = {nullptr, nullptr};
    OrtStatus* status = impl_->ort->Run(impl_->session, nullptr, inputNames, (const OrtValue**)&inputValue, 1, outputNames, 2, outputValues);
    
    if (status != nullptr) {
        const char* msg = impl_->ort->GetErrorMessage(status);
        std::cerr << "[Swift] Error running inference: " << msg << std::endl;
        impl_->ort->ReleaseStatus(status);
        return {impl_->lastF0, 0.0f};
    }

    float* pitchData;
    float* confData;
    impl_->ort->GetTensorMutableData(outputValues[0], (void**)&pitchData);
    impl_->ort->GetTensorMutableData(outputValues[1], (void**)&confData);

    float f0 = pitchData[0];
    float confidence = confData[0];

    impl_->ort->ReleaseValue(inputValue);
    impl_->ort->ReleaseValue(outputValues[0]);
    impl_->ort->ReleaseValue(outputValues[1]);

    // Apply confidence threshold and smoothing
    if (confidence < 0.5f) {
        f0 = 0.0f;
    } else {
        // Simple smoothing to bridge frames
        if (impl_->lastF0 > 0.0f) {
            f0 = 0.8f * f0 + 0.2f * impl_->lastF0;
        }
    }
    impl_->lastF0 = f0;

    // Shift buffer by hop size
    if (impl_->audioBuffer.size() >= 256) {
        impl_->audioBuffer.erase(impl_->audioBuffer.begin(), impl_->audioBuffer.begin() + 256);
    } else {
        impl_->audioBuffer.clear();
    }

    return {f0, confidence};
}

} // namespace vocalexp
