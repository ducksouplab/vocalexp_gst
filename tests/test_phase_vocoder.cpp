#include "dsp/phase_vocoder.hpp"

#include <gtest/gtest.h>

#include <complex>
#include <vector>

#include "dsp/window.hpp"
#include "test_helpers.hpp"

namespace vocalexp {
namespace {

using testing::dominantFrequency;
using testing::kPi;
using testing::makeSine;
using testing::rms;

TEST(PrincipalArgument, WrapsIntoMinusPiPi) {
  EXPECT_NEAR(principalArgument(0.0f), 0.0f, 1e-6f);
  EXPECT_NEAR(principalArgument(1.0f), 1.0f, 1e-6f);
  EXPECT_NEAR(principalArgument(-1.0f), -1.0f, 1e-6f);
  EXPECT_NEAR(principalArgument(2.0f * kPi + 0.1f), 0.1f, 1e-5f);
  EXPECT_NEAR(principalArgument(-2.0f * kPi - 0.1f), -0.1f, 1e-5f);
  EXPECT_NEAR(principalArgument(7.0f * kPi), kPi, 1e-4f);
  EXPECT_NEAR(principalArgument(10.0f * kPi + 0.5f), 0.5f, 1e-4f);
}

// Phase unwrapping: the instantaneous frequency of a sinusoid that falls
// between bin centres must be recovered from consecutive frame phases.
TEST(EstimateTrueFrequency, RecoversOffBinSineFrequency) {
  constexpr float kSampleRate = 48000.0f;
  constexpr std::size_t kFftSize = 1024;
  constexpr std::size_t kHop = 256;
  // Bin width is 46.875 Hz; 443.7 Hz sits between bins 9 and 10.
  constexpr float kFrequency = 443.7f;

  const std::vector<float> signal = makeSine(kFrequency, kSampleRate, kFftSize + kHop);
  const std::vector<float> window = makeHannWindow(kFftSize);
  Fft fft(kFftSize);

  auto framePhase = [&](std::size_t offset, std::size_t bin) {
    std::vector<std::complex<float>> buffer(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
      buffer[i] = {signal[offset + i] * window[i], 0.0f};
    }
    fft.forward(buffer.data());
    return std::arg(buffer[bin]);
  };

  const std::size_t peakBin =
      static_cast<std::size_t>(kFrequency / (kSampleRate / kFftSize) + 0.5f);
  const float phase0 = framePhase(0, peakBin);
  const float phase1 = framePhase(kHop, peakBin);

  const float estimated =
      estimateTrueFrequency(peakBin, phase1, phase0, kFftSize, kHop, kSampleRate);
  EXPECT_NEAR(estimated, kFrequency, 0.5f);
}

TEST(PhaseVocoder, ReportsExpectedLatency) {
  PhaseVocoder::Config config;
  config.frameSize = 1024;
  config.overlapFactor = 4;
  PhaseVocoder vocoder(config);
  EXPECT_EQ(vocoder.hopSize(), 256u);
  EXPECT_EQ(vocoder.latencySamples(), 768u);
}

TEST(PhaseVocoder, UnityRatioPassesSineThrough) {
  constexpr float kSampleRate = 48000.0f;
  constexpr float kFrequency = 440.0f;
  constexpr std::size_t kLength = 48000;

  PhaseVocoder::Config config;
  config.sampleRate = kSampleRate;
  PhaseVocoder vocoder(config);
  vocoder.setPitchRatio(1.0f);

  const std::vector<float> input = makeSine(kFrequency, kSampleRate, kLength);
  std::vector<float> output(kLength);
  vocoder.process(input.data(), output.data(), kLength);

  // Skip the start-up transient, then check frequency and level.
  const std::size_t skip = 4 * config.frameSize;
  const float frequency = dominantFrequency(output.data() + skip, kLength - skip, kSampleRate);
  EXPECT_NEAR(frequency, kFrequency, 1.0f);

  const float inputRms = rms(input.data() + skip, kLength - skip);
  const float outputRms = rms(output.data() + skip, kLength - skip);
  EXPECT_NEAR(outputRms, inputRms, 0.15f * inputRms);
}

TEST(PhaseVocoder, ShiftsPitchUpByRatio) {
  constexpr float kSampleRate = 48000.0f;
  constexpr float kFrequency = 440.0f;
  constexpr float kRatio = 1.5f;
  constexpr std::size_t kLength = 48000;

  PhaseVocoder::Config config;
  config.sampleRate = kSampleRate;
  PhaseVocoder vocoder(config);
  vocoder.setPitchRatio(kRatio);

  const std::vector<float> input = makeSine(kFrequency, kSampleRate, kLength);
  std::vector<float> output(kLength);
  vocoder.process(input.data(), output.data(), kLength);

  const std::size_t skip = 4 * config.frameSize;
  const float frequency = dominantFrequency(output.data() + skip, kLength - skip, kSampleRate);
  EXPECT_NEAR(frequency, kFrequency * kRatio, 5.0f);
}

TEST(PhaseVocoder, ShiftsPitchDownByRatio) {
  constexpr float kSampleRate = 48000.0f;
  constexpr float kFrequency = 660.0f;
  constexpr float kRatio = 2.0f / 3.0f;
  constexpr std::size_t kLength = 48000;

  PhaseVocoder::Config config;
  config.sampleRate = kSampleRate;
  PhaseVocoder vocoder(config);
  vocoder.setPitchRatio(kRatio);

  const std::vector<float> input = makeSine(kFrequency, kSampleRate, kLength);
  std::vector<float> output(kLength);
  vocoder.process(input.data(), output.data(), kLength);

  const std::size_t skip = 4 * config.frameSize;
  const float frequency = dominantFrequency(output.data() + skip, kLength - skip, kSampleRate);
  EXPECT_NEAR(frequency, kFrequency * kRatio, 5.0f);
}

TEST(PhaseVocoder, ClampsExtremeRatios) {
  PhaseVocoder vocoder(PhaseVocoder::Config{});
  vocoder.setPitchRatio(100.0f);
  EXPECT_FLOAT_EQ(vocoder.pitchRatio(), 4.0f);
  vocoder.setPitchRatio(0.0f);
  EXPECT_FLOAT_EQ(vocoder.pitchRatio(), 0.25f);
}

TEST(PhaseVocoder, SilenceInSilenceOut) {
  PhaseVocoder vocoder(PhaseVocoder::Config{});
  vocoder.setPitchRatio(1.3f);

  std::vector<float> input(8192, 0.0f);
  std::vector<float> output(input.size(), 1.0f);
  vocoder.process(input.data(), output.data(), input.size());

  for (float v : output) EXPECT_EQ(v, 0.0f);
}

TEST(PhaseVocoder, EnvelopePreservationKeepsFormantPeak) {
  // A "vowel-like" tone: harmonics of 150 Hz with a spectral resonance near
  // 1100 Hz. When shifting up an octave with envelope preservation, the
  // spectral energy centroid around the formant must move far less than the
  // octave the harmonics move.
  constexpr float kSampleRate = 48000.0f;
  constexpr float kF0 = 150.0f;
  constexpr float kFormant = 1100.0f;
  constexpr float kFormantWidth = 350.0f;
  constexpr std::size_t kLength = 96000;

  std::vector<float> input(kLength, 0.0f);
  for (int h = 1; h * kF0 < 6000.0f; ++h) {
    const float frequency = h * kF0;
    const float weight =
        std::exp(-0.5f * std::pow((frequency - kFormant) / kFormantWidth, 2.0f)) + 0.02f;
    const std::vector<float> partial = makeSine(frequency, kSampleRate, kLength, 0.1f * weight);
    for (std::size_t i = 0; i < kLength; ++i) input[i] += partial[i];
  }

  auto spectralCentroidNearFormant = [&](const std::vector<float>& x) {
    constexpr std::size_t kFftSize = 8192;
    Fft fft(kFftSize);
    const std::vector<float> window = makeHannWindow(kFftSize);
    std::vector<std::complex<float>> buffer(kFftSize);
    const std::size_t offset = kLength / 2;
    for (std::size_t i = 0; i < kFftSize; ++i) {
      buffer[i] = {x[offset + i] * window[i], 0.0f};
    }
    fft.forward(buffer.data());
    // Energy-weighted mean frequency over 400–3000 Hz.
    const float binHz = kSampleRate / kFftSize;
    double num = 0.0, den = 0.0;
    for (std::size_t k = static_cast<std::size_t>(400 / binHz);
         k < static_cast<std::size_t>(3000 / binHz); ++k) {
      const double e = std::norm(buffer[k]);
      num += e * k * binHz;
      den += e;
    }
    return static_cast<float>(num / den);
  };

  auto runVocoder = [&](bool preserve) {
    PhaseVocoder::Config config;
    config.sampleRate = kSampleRate;
    config.frameSize = 2048;  // finer spectral resolution for envelope work
    config.envelopePreservation = preserve;
    PhaseVocoder vocoder(config);
    vocoder.setPitchRatio(2.0f);
    std::vector<float> output(kLength);
    vocoder.process(input.data(), output.data(), kLength);
    return output;
  };

  const float inputCentroid = spectralCentroidNearFormant(input);
  const std::vector<float> plain = runVocoder(false);
  const std::vector<float> preserved = runVocoder(true);

  const float plainShift = spectralCentroidNearFormant(plain) - inputCentroid;
  const float preservedShift = spectralCentroidNearFormant(preserved) - inputCentroid;

  // Without preservation the formant region migrates upward markedly; with
  // preservation it must stay much closer to the original.
  EXPECT_GT(plainShift, 250.0f);
  EXPECT_LT(std::abs(preservedShift), 0.5f * plainShift);
}

}  // namespace
}  // namespace vocalexp
