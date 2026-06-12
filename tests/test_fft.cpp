#include "dsp/fft.hpp"

#include <gtest/gtest.h>

#include <complex>
#include <random>
#include <vector>

#include "test_helpers.hpp"

namespace vocalexp {
namespace {

using testing::kPi;

TEST(Fft, RejectsNonPowerOfTwoSizes) {
  EXPECT_THROW(Fft(0), std::invalid_argument);
  EXPECT_THROW(Fft(1), std::invalid_argument);
  EXPECT_THROW(Fft(100), std::invalid_argument);
  EXPECT_NO_THROW(Fft(1024));
}

TEST(Fft, ImpulseHasFlatSpectrum) {
  constexpr std::size_t kSize = 64;
  Fft fft(kSize);
  std::vector<std::complex<float>> data(kSize, {0.0f, 0.0f});
  data[0] = {1.0f, 0.0f};

  fft.forward(data.data());

  for (std::size_t k = 0; k < kSize; ++k) {
    EXPECT_NEAR(data[k].real(), 1.0f, 1e-5f) << "bin " << k;
    EXPECT_NEAR(data[k].imag(), 0.0f, 1e-5f) << "bin " << k;
  }
}

TEST(Fft, SineAtBinCentreProducesSinglePeak) {
  constexpr std::size_t kSize = 256;
  constexpr std::size_t kBin = 17;
  Fft fft(kSize);

  std::vector<std::complex<float>> data(kSize);
  for (std::size_t i = 0; i < kSize; ++i) {
    data[i] = {static_cast<float>(std::sin(2.0 * kPi * kBin * i / kSize)), 0.0f};
  }
  fft.forward(data.data());

  // A real sine at an exact bin yields magnitude N/2 at bins kBin and N-kBin.
  for (std::size_t k = 0; k <= kSize / 2; ++k) {
    const float magnitude = std::abs(data[k]);
    if (k == kBin) {
      EXPECT_NEAR(magnitude, kSize / 2.0f, 1e-2f);
    } else {
      EXPECT_LT(magnitude, 1e-2f) << "leakage at bin " << k;
    }
  }
}

TEST(Fft, RoundTripRecoversSignal) {
  constexpr std::size_t kSize = 1024;
  Fft fft(kSize);

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<std::complex<float>> original(kSize);
  for (auto& v : original) v = {dist(rng), dist(rng)};

  std::vector<std::complex<float>> data = original;
  fft.forward(data.data());
  fft.inverse(data.data());

  for (std::size_t i = 0; i < kSize; ++i) {
    EXPECT_NEAR(data[i].real(), original[i].real(), 1e-4f);
    EXPECT_NEAR(data[i].imag(), original[i].imag(), 1e-4f);
  }
}

TEST(Fft, SatisfiesParseval) {
  constexpr std::size_t kSize = 512;
  Fft fft(kSize);

  std::mt19937 rng(7);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<std::complex<float>> data(kSize);
  double timeEnergy = 0.0;
  for (auto& v : data) {
    v = {dist(rng), 0.0f};
    timeEnergy += std::norm(v);
  }

  fft.forward(data.data());
  double freqEnergy = 0.0;
  for (const auto& v : data) freqEnergy += std::norm(v);
  freqEnergy /= static_cast<double>(kSize);

  EXPECT_NEAR(freqEnergy, timeEnergy, timeEnergy * 1e-5);
}

TEST(HannWindow, SquaredOverlapAddIsConstantAtQuarterHop) {
  constexpr std::size_t kSize = 1024;
  constexpr std::size_t kHop = kSize / 4;
  const std::vector<float> window = makeHannWindow(kSize);

  // Σ_m w²(n - m·hop) must be constant (COLA for w² at 75% overlap).
  std::vector<double> ola(kSize, 0.0);
  for (std::size_t offset = 0; offset < kSize; offset += kHop) {
    for (std::size_t i = 0; i < kSize; ++i) {
      ola[(i + offset) % kSize] += static_cast<double>(window[i]) * window[i];
    }
  }
  for (std::size_t i = 0; i < kSize; ++i) {
    EXPECT_NEAR(ola[i], 1.5, 1e-4) << "position " << i;
  }
  EXPECT_NEAR(windowOverlapGain(window, kHop), 1.5f, 1e-4f);
}

}  // namespace
}  // namespace vocalexp
