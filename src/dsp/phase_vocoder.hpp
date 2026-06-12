#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

#include "dsp/envelope_extractor.hpp"
#include "dsp/fft.hpp"

namespace vocalexp {

/// Wraps an angle to the principal interval (-π, π].
inline float principalArgument(float angle) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  float wrapped = std::fmod(angle + static_cast<float>(M_PI), kTwoPi);
  if (wrapped <= 0.0f) wrapped += kTwoPi;
  return wrapped - static_cast<float>(M_PI);
}

/// Estimates the true (instantaneous) frequency of FFT bin k from the phase
/// difference between two consecutive analysis frames spaced `hop` samples
/// apart — the core phase-unwrapping step of the phase vocoder.
///
/// The measured phase advance is compared to the advance a sinusoid exactly
/// at the bin centre would produce (2π·k·hop/N); the wrapped residual gives
/// the deviation from the bin-centre frequency.
float estimateTrueFrequency(std::size_t bin, float phase, float previousPhase,
                            std::size_t fftSize, std::size_t hop, float sampleRate);

/// Real-time pitch-shifting phase vocoder (STFT → phase unwrapping →
/// spectral bin remapping → phase accumulation → ISTFT with windowed
/// overlap-add), in the style of Laroche/Dolson and Bernsee's smbPitchShift,
/// with optional spectral-envelope preservation.
///
/// Streaming model: call processSample()/process() continuously; one output
/// sample is produced per input sample with an algorithmic latency of
/// latencySamples() = frameSize - hopSize. The pitch ratio may be changed at
/// any time and takes effect at the next analysis frame, which is what
/// allows the expressivity algorithm to drive it frame-by-frame.
///
/// After construction, the audio path performs no memory allocation.
class PhaseVocoder {
 public:
  struct Config {
    float sampleRate = 48000.0f;
    /// STFT window size in samples; power of two. 1024 @ 48 kHz ≈ 21.3 ms.
    std::size_t frameSize = 1024;
    /// Overlap factor; hop = frameSize / overlapFactor. 4 (75% overlap) is
    /// the standard quality/latency trade-off for Hann windows.
    std::size_t overlapFactor = 4;
    /// Re-apply the original spectral envelope after shifting so formants
    /// (vocal identity) stay in place — avoids the "chipmunk" effect.
    bool envelopePreservation = false;
    /// Cepstral order for envelope extraction (see EnvelopeExtractor).
    std::size_t envelopeOrder = 40;
    int trueEnvelopeIterations = 2;
  };

  explicit PhaseVocoder(const Config& config);

  /// Pitch-shift factor (1.0 = unchanged, 2.0 = up one octave). Clamped to
  /// [0.25, 4.0]. Takes effect at the next analysis frame.
  void setPitchRatio(float ratio);
  float pitchRatio() const { return pitchRatio_; }

  void setEnvelopePreservation(bool enabled) { envelopePreservation_ = enabled; }
  bool envelopePreservation() const { return envelopePreservation_; }

  std::size_t frameSize() const { return config_.frameSize; }
  std::size_t hopSize() const { return hopSize_; }
  /// Algorithmic latency: input-to-output delay in samples.
  std::size_t latencySamples() const { return config_.frameSize - hopSize_; }

  /// Processes one sample; returns the corresponding (latency-delayed) output.
  float processSample(float input);

  /// Processes n samples (in and out may alias).
  void process(const float* input, float* output, std::size_t n);

  /// Clears all internal state (FIFOs, phase accumulators).
  void reset();

 private:
  void processFrame();

  Config config_;
  Fft fft_;
  std::size_t hopSize_;
  std::size_t numBins_;
  float pitchRatio_ = 1.0f;
  bool envelopePreservation_ = false;

  std::vector<float> window_;
  float overlapGain_;  // OLA normalisation for analysis+synthesis windowing

  // Streaming FIFOs (Bernsee-style): input accumulates into inputFifo_; every
  // hop a frame is processed into outputAccumulator_, whose first hop samples
  // become the next block of output read through outputFifo_.
  std::vector<float> inputFifo_;
  std::vector<float> outputFifo_;
  std::vector<float> outputAccumulator_;
  std::size_t fifoPosition_;

  // Per-frame analysis/synthesis state.
  std::vector<std::complex<float>> spectrum_;
  std::vector<float> lastPhase_;       // analysis phase memory (unwrapping)
  std::vector<float> accumulatedPhase_;  // synthesis phase accumulators
  std::vector<float> analysisMagnitude_;
  std::vector<float> analysisFrequency_;
  std::vector<float> synthesisMagnitude_;
  std::vector<float> synthesisFrequency_;
  std::vector<float> envelope_;
  std::unique_ptr<EnvelopeExtractor> envelopeExtractor_;
};

}  // namespace vocalexp
