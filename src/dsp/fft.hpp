#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace vocalexp {

/// Radix-2 iterative Cooley-Tukey FFT for power-of-two sizes.
///
/// Self-contained (no external dependency) so the plugin carries no FFT
/// library license constraint. Tables (bit-reversal permutation, twiddle
/// factors) are precomputed at construction; forward()/inverse() perform no
/// allocation and are safe to call from a real-time audio thread.
class Fft {
 public:
  /// @param size Transform length; must be a power of two >= 2.
  explicit Fft(std::size_t size);

  std::size_t size() const { return size_; }

  /// In-place forward DFT. No scaling is applied.
  void forward(std::complex<float>* data) const;

  /// In-place inverse DFT, scaled by 1/N so that inverse(forward(x)) == x.
  void inverse(std::complex<float>* data) const;

 private:
  void transform(std::complex<float>* data, bool inverse) const;

  std::size_t size_;
  std::vector<std::size_t> bitReversal_;
  std::vector<std::complex<float>> twiddles_;  // e^{-2πi k / N}, k in [0, N/2)
};

constexpr bool isPowerOfTwo(std::size_t n) { return n >= 2 && (n & (n - 1)) == 0; }

}  // namespace vocalexp
