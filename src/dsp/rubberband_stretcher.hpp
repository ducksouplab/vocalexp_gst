#pragma once

#include <cstddef>
#include <memory>

namespace vocalexp {

/**
 * @brief Pitch shifter using the Rubber Band library.
 * 
 * Provides high-quality time-stretching and pitch-shifting with 
 * built-in formant (envelope) preservation.
 */
class RubberBandStretcher {
public:
    struct Config {
        float sampleRate = 48000.0f;
        std::size_t channels = 1;
        bool preserveFormants = true;
    };

    explicit RubberBandStretcher(const Config& config);
    ~RubberBandStretcher();

    void reset();

    /**
     * @brief Sets the pitch shift ratio.
     * @param ratio 1.0 = unchanged, >1.0 = higher, <1.0 = lower.
     */
    void setPitchRatio(float ratio);

    /**
     * @brief Processes a chunk of audio.
     * @param input Mono input samples.
     * @param output Mono output samples.
     * @param n Number of samples.
     */
    void process(const float* input, float* output, std::size_t n);

    /**
     * @brief Returns the internal latency in samples.
     */
    std::size_t latency() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vocalexp
