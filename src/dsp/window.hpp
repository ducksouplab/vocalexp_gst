#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace vocalexp {

/// Periodic Hann window of length n: w[k] = 0.5 * (1 - cos(2πk / n)).
///
/// The periodic (DFT-even) variant satisfies the constant-overlap-add
/// property required by the phase vocoder for hop sizes n/2, n/4, n/8, ...
inline std::vector<float> makeHannWindow(std::size_t n) {
  std::vector<float> w(n);
  for (std::size_t k = 0; k < n; ++k) {
    w[k] = 0.5f - 0.5f * std::cos(2.0 * M_PI * static_cast<double>(k) / static_cast<double>(n));
  }
  return w;
}

/// Constant gain of overlap-adding window^2 at the given hop size:
/// sum_n w[n]^2 / hop. Used to normalise analysis+synthesis windowed OLA.
inline float windowOverlapGain(const std::vector<float>& w, std::size_t hop) {
  double sum = 0.0;
  for (float v : w) sum += static_cast<double>(v) * v;
  return static_cast<float>(sum / static_cast<double>(hop));
}

}  // namespace vocalexp
