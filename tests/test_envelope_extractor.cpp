#include "dsp/envelope_extractor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/fft.hpp"
#include "dsp/window.hpp"

namespace vocalexp {
namespace {

constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kFftSize = 1024;
const std::size_t kBins = kFftSize / 2 + 1;
const float kBinHz = kSampleRate / kFftSize;

/// Smooth two-formant "vocal tract" magnitude response.
float formantEnvelope(float hz) {
  const float f1 = std::exp(-0.5f * std::pow((hz - 700.0f) / 200.0f, 2.0f));
  const float f2 = 0.6f * std::exp(-0.5f * std::pow((hz - 1800.0f) / 300.0f, 2.0f));
  return 0.02f + f1 + f2;
}

/// Magnitude spectrum the extractor sees in production: a synthetic vowel
/// (harmonics of f0 weighted by the formant envelope) analysed through a
/// Hann window and FFT.
std::vector<float> makeVoicedMagnitudeSpectrum(float f0) {
  std::vector<float> signal(kFftSize, 0.0f);
  for (int h = 1; h * f0 < kSampleRate / 2.0f; ++h) {
    const float amplitude = formantEnvelope(h * f0);
    const double increment = 2.0 * M_PI * h * f0 / kSampleRate;
    double phase = 0.7 * h;  // deterministic phase spread
    for (std::size_t i = 0; i < kFftSize; ++i) {
      signal[i] += amplitude * static_cast<float>(std::sin(phase));
      phase += increment;
    }
  }

  const std::vector<float> window = makeHannWindow(kFftSize);
  Fft fft(kFftSize);
  std::vector<std::complex<float>> buffer(kFftSize);
  for (std::size_t i = 0; i < kFftSize; ++i) buffer[i] = {signal[i] * window[i], 0.0f};
  fft.forward(buffer.data());

  std::vector<float> magnitude(kBins);
  for (std::size_t k = 0; k < kBins; ++k) magnitude[k] = std::abs(buffer[k]);
  return magnitude;
}

/// Bin of the strongest magnitude within ±2 bins of the nominal harmonic.
std::size_t harmonicPeakBin(const std::vector<float>& magnitude, float hz) {
  const auto nominal = static_cast<std::size_t>(hz / kBinHz + 0.5f);
  std::size_t best = nominal;
  for (std::size_t k = nominal - 2; k <= nominal + 2 && k < kBins; ++k) {
    if (magnitude[k] > magnitude[best]) best = k;
  }
  return best;
}

float toDb(float linear) { return 20.0f * std::log10(std::max(linear, 1e-12f)); }

EnvelopeExtractor::Config makeConfig(int trueEnvelopeIterations) {
  EnvelopeExtractor::Config config;
  config.fftSize = kFftSize;
  config.cepstralOrder = 40;
  config.trueEnvelopeIterations = trueEnvelopeIterations;
  return config;
}

TEST(EnvelopeExtractor, RejectsInvalidOrder) {
  EnvelopeExtractor::Config config;
  config.fftSize = 1024;
  config.cepstralOrder = 1;
  EXPECT_THROW(EnvelopeExtractor extractor(config), std::invalid_argument);
  config.cepstralOrder = 512;
  EXPECT_THROW(EnvelopeExtractor extractor(config), std::invalid_argument);
}

TEST(EnvelopeExtractor, FlatSpectrumYieldsFlatEnvelope) {
  EnvelopeExtractor extractor(makeConfig(0));
  std::vector<float> magnitude(kBins, 0.7f);
  std::vector<float> envelope(kBins);
  extractor.extract(magnitude.data(), envelope.data());

  for (std::size_t k = 0; k < kBins; ++k) {
    EXPECT_NEAR(envelope[k], 0.7f, 0.02f) << "bin " << k;
  }
}

TEST(EnvelopeExtractor, RemovesHarmonicRipple) {
  // At f0 = 350 Hz the harmonics are ~7.5 bins apart, with deep leakage
  // valleys in between. The envelope must not follow that comb structure:
  // its peak-to-valley ripple in dB must be a small fraction of the raw
  // spectrum's.
  constexpr float kF0 = 350.0f;
  EnvelopeExtractor extractor(makeConfig(4));
  const std::vector<float> magnitude = makeVoicedMagnitudeSpectrum(kF0);
  std::vector<float> envelope(kBins);
  extractor.extract(magnitude.data(), envelope.data());

  double rawRipple = 0.0, envelopeRipple = 0.0;
  int count = 0;
  for (int h = 1; (h + 1) * kF0 < 4000.0f; ++h) {
    const std::size_t peak = harmonicPeakBin(magnitude, h * kF0);
    const std::size_t mid = static_cast<std::size_t>((h + 0.5f) * kF0 / kBinHz + 0.5f);
    rawRipple += toDb(magnitude[peak]) - toDb(magnitude[mid]);
    envelopeRipple += std::abs(toDb(envelope[peak]) - toDb(envelope[mid]));
    ++count;
  }
  rawRipple /= count;
  envelopeRipple /= count;

  EXPECT_GT(rawRipple, 20.0);                      // the comb structure is real
  EXPECT_LT(envelopeRipple, 0.25 * rawRipple);     // and the envelope ignores it
}

TEST(EnvelopeExtractor, TrueEnvelopeHugsHarmonicPeaks) {
  // Plain cepstral smoothing averages peaks and valleys; the True Envelope
  // iteration must sit close to (or above) the harmonic peaks instead.
  constexpr float kF0 = 200.0f;
  EnvelopeExtractor extractor(makeConfig(16));
  const std::vector<float> magnitude = makeVoicedMagnitudeSpectrum(kF0);
  std::vector<float> envelope(kBins);
  extractor.extract(magnitude.data(), envelope.data());

  for (int h = 2; h * kF0 < 4000.0f; ++h) {
    const std::size_t bin = harmonicPeakBin(magnitude, h * kF0);
    EXPECT_GT(toDb(envelope[bin]), toDb(magnitude[bin]) - 3.0f)
        << "envelope sags more than 3 dB below harmonic " << h << " ("
        << bin * kBinHz << " Hz)";
  }
}

TEST(EnvelopeExtractor, TrueEnvelopeImprovesOnPlainSmoothing) {
  constexpr float kF0 = 200.0f;
  const std::vector<float> magnitude = makeVoicedMagnitudeSpectrum(kF0);

  auto peakError = [&](int iterations) {
    EnvelopeExtractor extractor(makeConfig(iterations));
    std::vector<float> envelope(kBins);
    extractor.extract(magnitude.data(), envelope.data());
    double error = 0.0;
    int count = 0;
    for (int h = 2; h * kF0 < 4000.0f; ++h) {
      const std::size_t bin = harmonicPeakBin(magnitude, h * kF0);
      error += std::abs(toDb(envelope[bin]) - toDb(magnitude[bin]));
      ++count;
    }
    return error / count;
  };

  EXPECT_LT(peakError(8), peakError(0));
}

TEST(EnvelopeExtractor, FollowsFormantShape) {
  EnvelopeExtractor extractor(makeConfig(8));
  const std::vector<float> magnitude = makeVoicedMagnitudeSpectrum(150.0f);
  std::vector<float> envelope(kBins);
  extractor.extract(magnitude.data(), envelope.data());

  // The extracted envelope must rank formant peaks and the inter-formant
  // valley the same way the true envelope does.
  const auto binOf = [&](float hz) { return static_cast<std::size_t>(hz / kBinHz + 0.5f); };
  const float atF1 = envelope[binOf(700.0f)];
  const float atValley = envelope[binOf(1250.0f)];
  const float atF2 = envelope[binOf(1800.0f)];

  EXPECT_GT(atF1, atValley);
  EXPECT_GT(atF2, atValley);
  EXPECT_GT(atF1, atF2);  // F1 is stronger than F2 in the synthetic envelope
}

}  // namespace
}  // namespace vocalexp
