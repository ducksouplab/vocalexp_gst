#pragma once

#include <vector>
#include <string>
#include <memory>
#include "dsp/pitch_tracker.hpp"

namespace vocalexp {

/**
 * @brief Pitch tracker using the SWIFT-F0 ONNX model.
 * 
 * This tracker wraps the SWIFT-F0 neural network model using ONNX Runtime.
 * It expects mono audio at 16kHz and produces pitch estimates with a 
 * 256-sample hop size (16ms at 16kHz).
 */
class SwiftPitchTracker {
public:
    struct Config {
        std::string modelPath = "models/swift_f0.onnx";
        float sampleRate = 16000.0f; // Model fixed rate
        std::size_t hopSize = 256;    // Model fixed hop
    };

    explicit SwiftPitchTracker(const Config& config);
    ~SwiftPitchTracker();

    void reset();

    /**
     * @brief Pushes audio samples into the tracker's internal buffer.
     * @param samples Pointer to the samples (mono, 16kHz).
     * @param n Number of samples.
     */
    void push(const float* samples, std::size_t n);

    /**
     * @brief Runs inference on the accumulated buffer if enough samples are present.
     * @return The latest pitch estimate.
     */
    PitchEstimate estimate();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vocalexp
