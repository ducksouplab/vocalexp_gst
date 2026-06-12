#pragma once

#include <cstddef>
#include <vector>

namespace vocalexp {

/// Result of a pitch estimate.
struct PitchEstimate {
  /// Fundamental frequency in Hz; 0 when the frame is unvoiced/silent.
  float f0 = 0.0f;
  /// Aperiodicity of the detected candidate (CMNDF value at the chosen lag);
  /// lower is more periodic. 1.0 when no estimate was possible.
  float aperiodicity = 1.0f;

  bool voiced() const { return f0 > 0.0f; }
};

/// Streaming monophonic pitch tracker based on YIN
/// (de Cheveigné & Kawahara, 2002): cumulative-mean-normalised difference
/// function (CMNDF), absolute-threshold candidate selection and parabolic
/// interpolation of the lag, plus an RMS silence gate.
///
/// Feed audio continuously with push(); call estimate() at the analysis hop
/// rate. estimate() looks at the most recent windowSize + maxLag samples, so
/// its decision latency is bounded by the window length.
class PitchTracker {
 public:
  struct Config {
    float sampleRate = 48000.0f;
    /// Correlation window W in samples. Larger = more robust on low pitch,
    /// higher latency. 1024 @ 48 kHz ≈ 21.3 ms.
    std::size_t windowSize = 1024;
    float minFrequency = 60.0f;    ///< Lowest detectable f0 (sets the max lag).
    float maxFrequency = 1000.0f;  ///< Highest detectable f0 (sets the min lag).
    /// CMNDF absolute threshold for picking the first periodicity dip.
    float threshold = 0.15f;
    /// Frames whose best CMNDF exceeds this are declared unvoiced.
    float voicedThreshold = 0.30f;
    /// Frames with RMS below this are declared silent (unvoiced).
    float silenceRms = 1e-4f;
  };

  explicit PitchTracker(const Config& config);

  /// Appends n samples of audio to the internal history.
  void push(const float* samples, std::size_t n);

  /// Estimates the pitch of the most recent window. O(W · maxLag).
  PitchEstimate estimate();

  /// Clears the audio history.
  void reset();

  std::size_t minLag() const { return minLag_; }
  std::size_t maxLag() const { return maxLag_; }
  const Config& config() const { return config_; }

 private:
  Config config_;
  std::size_t minLag_;
  std::size_t maxLag_;

  std::vector<float> history_;  // ring buffer, windowSize + maxLag samples
  std::size_t writePosition_ = 0;
  std::size_t samplesSeen_ = 0;

  std::vector<float> frame_;       // linearised history, oldest first
  std::vector<float> difference_;  // d(τ) then CMNDF d'(τ), maxLag + 1 values
};

}  // namespace vocalexp
