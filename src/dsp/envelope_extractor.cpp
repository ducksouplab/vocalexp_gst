#include "dsp/envelope_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vocalexp {

namespace {
// Floor for log-magnitude computation; about -180 dB.
constexpr float kMagnitudeFloor = 1e-9f;
}  // namespace

EnvelopeExtractor::EnvelopeExtractor(const Config& config)
    : config_(config), fft_(config.fftSize) {
  if (config_.cepstralOrder < 2 || config_.cepstralOrder >= config_.fftSize / 2) {
    throw std::invalid_argument("cepstralOrder must be in [2, fftSize/2)");
  }
  logSpectrum_.resize(numBins());
  smoothedLog_.resize(numBins());
  scratch_.resize(config_.fftSize);
}

void EnvelopeExtractor::cepstralSmooth() {
  const std::size_t n = config_.fftSize;
  const std::size_t bins = numBins();

  // Build the full conjugate-symmetric log spectrum (real, even sequence).
  for (std::size_t k = 0; k < bins; ++k) scratch_[k] = {logSpectrum_[k], 0.0f};
  for (std::size_t k = bins; k < n; ++k) scratch_[k] = {logSpectrum_[n - k], 0.0f};

  // Real cepstrum: IDFT of the log spectrum.
  fft_.inverse(scratch_.data());

  // Lifter: keep low-quefrency coefficients (the slowly varying envelope),
  // zero everything at or above the cutoff. The kept range is symmetric
  // because the cepstrum of a real, even log spectrum is real and even.
  const std::size_t cutoff = config_.cepstralOrder;
  for (std::size_t q = cutoff; q <= n - cutoff; ++q) scratch_[q] = {0.0f, 0.0f};

  // Back to the log-spectral domain.
  fft_.forward(scratch_.data());
  for (std::size_t k = 0; k < bins; ++k) smoothedLog_[k] = scratch_[k].real();
}

void EnvelopeExtractor::extract(const float* magnitude, float* envelope) {
  const std::size_t bins = numBins();

  float peakLog = std::log(kMagnitudeFloor);
  for (std::size_t k = 0; k < bins; ++k) {
    logSpectrum_[k] = std::log(std::max(magnitude[k], kMagnitudeFloor));
    peakLog = std::max(peakLog, logSpectrum_[k]);
  }
  // Clamp the dynamic range relative to the spectral peak (dB → nepers).
  const float floorLog = peakLog - config_.dynamicRangeDb * 0.115129254f;
  for (std::size_t k = 0; k < bins; ++k) {
    logSpectrum_[k] = std::max(logSpectrum_[k], floorLog);
  }
  // Keep a copy of the original log spectrum for the True Envelope updates.
  std::vector<float>& target = logSpectrum_;

  cepstralSmooth();

  for (int it = 0; it < config_.trueEnvelopeIterations; ++it) {
    // True Envelope step: lift the working spectrum up to the current
    // envelope wherever it dips below, then re-smooth. This pulls the
    // estimate onto the harmonic peaks instead of averaging across them.
    bool changed = false;
    for (std::size_t k = 0; k < bins; ++k) {
      if (smoothedLog_[k] > target[k]) {
        target[k] = smoothedLog_[k];
        changed = true;
      }
    }
    if (!changed) break;
    cepstralSmooth();
  }

  for (std::size_t k = 0; k < bins; ++k) envelope[k] = std::exp(smoothedLog_[k]);
}

}  // namespace vocalexp
