#include "dsp/fft.hpp"

#include <cmath>
#include <stdexcept>

namespace vocalexp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

Fft::Fft(std::size_t size) : size_(size) {
  if (!isPowerOfTwo(size)) {
    throw std::invalid_argument("Fft size must be a power of two >= 2");
  }

  bitReversal_.resize(size_);
  std::size_t bits = 0;
  while ((std::size_t{1} << bits) < size_) ++bits;
  for (std::size_t i = 0; i < size_; ++i) {
    std::size_t reversed = 0;
    for (std::size_t b = 0; b < bits; ++b) {
      if (i & (std::size_t{1} << b)) reversed |= std::size_t{1} << (bits - 1 - b);
    }
    bitReversal_[i] = reversed;
  }

  twiddles_.resize(size_ / 2);
  for (std::size_t k = 0; k < size_ / 2; ++k) {
    const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(size_);
    twiddles_[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
  }
}

void Fft::transform(std::complex<float>* data, bool inverse) const {
  for (std::size_t i = 0; i < size_; ++i) {
    const std::size_t j = bitReversal_[i];
    if (i < j) std::swap(data[i], data[j]);
  }

  for (std::size_t length = 2; length <= size_; length <<= 1) {
    const std::size_t half = length >> 1;
    const std::size_t twiddleStep = size_ / length;
    for (std::size_t start = 0; start < size_; start += length) {
      for (std::size_t k = 0; k < half; ++k) {
        std::complex<float> w = twiddles_[k * twiddleStep];
        if (inverse) w = std::conj(w);
        const std::complex<float> even = data[start + k];
        const std::complex<float> odd = data[start + k + half] * w;
        data[start + k] = even + odd;
        data[start + k + half] = even - odd;
      }
    }
  }
}

void Fft::forward(std::complex<float>* data) const { transform(data, /*inverse=*/false); }

void Fft::inverse(std::complex<float>* data) const {
  transform(data, /*inverse=*/true);
  const float scale = 1.0f / static_cast<float>(size_);
  for (std::size_t i = 0; i < size_; ++i) data[i] *= scale;
}

}  // namespace vocalexp
