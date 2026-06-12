#include "dsp/pitch_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vocalexp {

PitchTracker::PitchTracker(const Config& config) : config_(config) {
  if (config_.minFrequency <= 0.0f || config_.maxFrequency <= config_.minFrequency) {
    throw std::invalid_argument("require 0 < minFrequency < maxFrequency");
  }
  minLag_ = std::max<std::size_t>(
      2, static_cast<std::size_t>(config_.sampleRate / config_.maxFrequency));
  maxLag_ = static_cast<std::size_t>(config_.sampleRate / config_.minFrequency);
  if (maxLag_ <= minLag_ + 2) {
    throw std::invalid_argument("frequency range too narrow for this sample rate");
  }

  history_.assign(config_.windowSize + maxLag_, 0.0f);
  frame_.resize(history_.size());
  difference_.resize(maxLag_ + 1);
}

void PitchTracker::reset() {
  std::fill(history_.begin(), history_.end(), 0.0f);
  writePosition_ = 0;
  samplesSeen_ = 0;
}

void PitchTracker::push(const float* samples, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    history_[writePosition_] = samples[i];
    writePosition_ = (writePosition_ + 1) % history_.size();
  }
  samplesSeen_ = std::min(samplesSeen_ + n, history_.size());
}

PitchEstimate PitchTracker::estimate() {
  PitchEstimate result;
  if (samplesSeen_ < history_.size()) return result;  // not enough history yet

  // Linearise the ring buffer, oldest sample first.
  const std::size_t size = history_.size();
  for (std::size_t i = 0; i < size; ++i) {
    frame_[i] = history_[(writePosition_ + i) % size];
  }

  const std::size_t window = config_.windowSize;

  // Silence gate on the correlation window.
  double energy = 0.0;
  for (std::size_t i = 0; i < window; ++i) {
    energy += static_cast<double>(frame_[i]) * frame_[i];
  }
  const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(window)));
  if (rms < config_.silenceRms) return result;

  // Difference function d(τ) = Σ_j (x[j] - x[j+τ])², j in [0, W).
  difference_[0] = 0.0f;
  for (std::size_t tau = 1; tau <= maxLag_; ++tau) {
    double sum = 0.0;
    for (std::size_t j = 0; j < window; ++j) {
      const double delta = static_cast<double>(frame_[j]) - frame_[j + tau];
      sum += delta * delta;
    }
    difference_[tau] = static_cast<float>(sum);
  }

  // Cumulative-mean-normalised difference function (in place):
  // d'(τ) = d(τ) · τ / Σ_{j=1..τ} d(j); d'(0) = 1.
  difference_[0] = 1.0f;
  double runningSum = 0.0;
  for (std::size_t tau = 1; tau <= maxLag_; ++tau) {
    runningSum += difference_[tau];
    difference_[tau] = runningSum > 0.0
                           ? static_cast<float>(difference_[tau] * static_cast<double>(tau) / runningSum)
                           : 1.0f;
  }

  // Absolute threshold: first τ below threshold, refined to its local minimum.
  std::size_t bestLag = 0;
  for (std::size_t tau = minLag_; tau <= maxLag_; ++tau) {
    if (difference_[tau] < config_.threshold) {
      while (tau + 1 <= maxLag_ && difference_[tau + 1] < difference_[tau]) ++tau;
      bestLag = tau;
      break;
    }
  }
  // Fallback: global minimum over the search range.
  if (bestLag == 0) {
    bestLag = minLag_;
    for (std::size_t tau = minLag_ + 1; tau <= maxLag_; ++tau) {
      if (difference_[tau] < difference_[bestLag]) bestLag = tau;
    }
  }

  result.aperiodicity = difference_[bestLag];
  if (result.aperiodicity > config_.voicedThreshold) return result;  // unvoiced

  // Parabolic interpolation around the chosen lag for sub-sample precision.
  float refinedLag = static_cast<float>(bestLag);
  if (bestLag > minLag_ && bestLag < maxLag_) {
    const float left = difference_[bestLag - 1];
    const float centre = difference_[bestLag];
    const float right = difference_[bestLag + 1];
    const float denominator = left - 2.0f * centre + right;
    if (std::abs(denominator) > 1e-12f) {
      refinedLag += 0.5f * (left - right) / denominator;
    }
  }

  const float f0 = config_.sampleRate / refinedLag;
  if (f0 < config_.minFrequency || f0 > config_.maxFrequency) return result;

  result.f0 = f0;
  return result;
}

}  // namespace vocalexp
