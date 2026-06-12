#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/fft.hpp"

namespace vocalexp {

/// Spectral envelope extraction by cepstral smoothing, with optional
/// "True Envelope" refinement (Röbel & Rodet, 2005).
///
/// Plain cepstral smoothing low-pass filters log|X(k)| along the frequency
/// axis, which tends to average across harmonic peaks and dips, slightly
/// underestimating the envelope at the peaks. The True Envelope iteration
/// fixes this by repeatedly smoothing max(log|X|, current envelope) so the
/// estimate converges onto the harmonic peaks from above.
///
/// extract() performs no allocation after construction and is real-time safe.
class EnvelopeExtractor {
 public:
  struct Config {
    std::size_t fftSize = 1024;  ///< Must match the analysis FFT size (power of two).
    /// Cepstral lifter cutoff in quefrency bins. Must be below the pitch
    /// period in samples (sampleRate / f0) or the envelope will start to
    /// follow individual harmonics. ~40 suits speech at 44.1/48 kHz.
    std::size_t cepstralOrder = 40;
    /// True Envelope refinement iterations. 0 = plain cepstral smoothing.
    int trueEnvelopeIterations = 4;
    /// Log magnitudes are floored at (spectral peak − dynamicRangeDb) before
    /// smoothing. Bounds the influence of near-silent bins on the cepstral
    /// mean and keeps the True Envelope iteration fast to converge.
    float dynamicRangeDb = 80.0f;
  };

  explicit EnvelopeExtractor(const Config& config);

  /// Computes the spectral envelope of a magnitude spectrum.
  /// @param magnitude  Input, fftSize/2 + 1 linear-magnitude bins.
  /// @param envelope   Output, fftSize/2 + 1 linear-magnitude bins.
  void extract(const float* magnitude, float* envelope);

  std::size_t numBins() const { return config_.fftSize / 2 + 1; }
  const Config& config() const { return config_; }

 private:
  /// Cepstrally smooths logSpectrum_ (half spectrum) into smoothedLog_.
  void cepstralSmooth();

  Config config_;
  Fft fft_;
  std::vector<float> logSpectrum_;            // numBins values of log|X|
  std::vector<float> smoothedLog_;            // numBins values, smoothed
  std::vector<std::complex<float>> scratch_;  // fftSize complex work buffer
};

}  // namespace vocalexp
