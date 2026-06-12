#include "dsp/vocal_expressivity_processor.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "dsp/pitch_tracker.hpp"
#include "test_helpers.hpp"

namespace vocalexp {
namespace {

using testing::makeVibratoSine;
using testing::mean;
using testing::standardDeviation;

constexpr float kSampleRate = 48000.0f;
constexpr float kCentre = 220.0f;
constexpr float kVibratoDepth = 15.0f;
constexpr float kVibratoRate = 4.0f;
constexpr std::size_t kLength = 2 * 48000;

VocalExpressivityProcessor::Config makeConfig(float expressivity) {
  VocalExpressivityProcessor::Config config;
  config.sampleRate = kSampleRate;
  config.frameSize = 1024;
  config.overlapFactor = 4;
  config.expressivity = expressivity;
  config.envelopePreservation = false;  // pure tone: not needed here
  config.minFrequency = 100.0f;
  config.maxFrequency = 600.0f;
  return config;
}

/// Offline pitch trajectory of a rendered signal, skipping edge transients.
std::vector<float> pitchTrajectory(const std::vector<float>& signal) {
  PitchTracker::Config config;
  config.sampleRate = kSampleRate;
  config.windowSize = 1024;
  config.minFrequency = 100.0f;
  config.maxFrequency = 600.0f;
  PitchTracker tracker(config);

  constexpr std::size_t kHop = 256;
  const std::size_t skipStart = 24000;  // 0.5 s: processor + tracker warm-up
  const std::size_t skipEnd = 4800;

  std::vector<float> trajectory;
  for (std::size_t offset = 0; offset + kHop <= signal.size() - skipEnd; offset += kHop) {
    tracker.push(signal.data() + offset, kHop);
    const PitchEstimate estimate = tracker.estimate();
    if (offset >= skipStart && estimate.voiced()) trajectory.push_back(estimate.f0);
  }
  return trajectory;
}

std::vector<float> processVibrato(float expressivity) {
  VocalExpressivityProcessor processor(makeConfig(expressivity));
  const std::vector<float> input =
      makeVibratoSine(kCentre, kVibratoDepth, kVibratoRate, kSampleRate, kLength);
  std::vector<float> output(kLength);

  // Feed in uneven chunk sizes to exercise the streaming path.
  const std::size_t chunks[] = {480, 113, 1024, 7, 256};
  std::size_t position = 0, c = 0;
  while (position < kLength) {
    const std::size_t n = std::min(chunks[c++ % 5], kLength - position);
    processor.process(input.data() + position, output.data() + position, n);
    position += n;
  }
  return output;
}

TEST(VocalExpressivityProcessor, ReportsLatency) {
  VocalExpressivityProcessor processor(makeConfig(1.0f));
  EXPECT_EQ(processor.latencySamples(), 768u);  // 16 ms @ 48 kHz
  EXPECT_EQ(processor.hopSize(), 256u);
}

TEST(VocalExpressivityProcessor, UnityExpressivityPreservesThePitchContour) {
  const std::vector<float> input =
      makeVibratoSine(kCentre, kVibratoDepth, kVibratoRate, kSampleRate, kLength);
  const std::vector<float> output = processVibrato(1.0f);

  const std::vector<float> inputPitch = pitchTrajectory(input);
  const std::vector<float> outputPitch = pitchTrajectory(output);
  ASSERT_GT(outputPitch.size(), 100u);

  EXPECT_NEAR(mean(outputPitch), mean(inputPitch), 0.02 * mean(inputPitch));
  EXPECT_NEAR(standardDeviation(outputPitch), standardDeviation(inputPitch),
              0.25 * standardDeviation(inputPitch));
}

TEST(VocalExpressivityProcessor, HighExpressivityWidensThePitchContour) {
  const std::vector<float> input =
      makeVibratoSine(kCentre, kVibratoDepth, kVibratoRate, kSampleRate, kLength);
  const std::vector<float> output = processVibrato(2.0f);

  const std::vector<float> inputPitch = pitchTrajectory(input);
  const std::vector<float> outputPitch = pitchTrajectory(output);
  ASSERT_GT(outputPitch.size(), 100u);

  const double widening = standardDeviation(outputPitch) / standardDeviation(inputPitch);
  // Target is 2.0; tracking latency and vocoder smearing erode the extremes,
  // so accept a generous band that still clearly separates E=2 from E=1.
  EXPECT_GT(widening, 1.5);
  EXPECT_LT(widening, 2.6);
  // The mean pitch must not drift.
  EXPECT_NEAR(mean(outputPitch), kCentre, 0.05 * kCentre);
}

TEST(VocalExpressivityProcessor, ZeroExpressivityFlattensThePitchContour) {
  const std::vector<float> input =
      makeVibratoSine(kCentre, kVibratoDepth, kVibratoRate, kSampleRate, kLength);
  const std::vector<float> output = processVibrato(0.0f);

  const std::vector<float> inputPitch = pitchTrajectory(input);
  const std::vector<float> outputPitch = pitchTrajectory(output);
  ASSERT_GT(outputPitch.size(), 100u);

  EXPECT_LT(standardDeviation(outputPitch), 0.35 * standardDeviation(inputPitch));
}

TEST(VocalExpressivityProcessor, SilencePassesThrough) {
  VocalExpressivityProcessor processor(makeConfig(2.0f));
  std::vector<float> silence(16384, 0.0f);
  std::vector<float> output(silence.size(), 1.0f);
  processor.process(silence.data(), output.data(), silence.size());

  for (float v : output) EXPECT_EQ(v, 0.0f);
  EXPECT_FLOAT_EQ(processor.currentF0(), 0.0f);
  EXPECT_FLOAT_EQ(processor.currentPitchRatio(), 1.0f);
}

TEST(VocalExpressivityProcessor, InPlaceProcessingMatchesOutOfPlace) {
  const std::vector<float> input =
      makeVibratoSine(kCentre, kVibratoDepth, kVibratoRate, kSampleRate, 16384);

  VocalExpressivityProcessor outOfPlace(makeConfig(1.5f));
  std::vector<float> reference(input.size());
  outOfPlace.process(input.data(), reference.data(), input.size());

  VocalExpressivityProcessor inPlace(makeConfig(1.5f));
  std::vector<float> buffer = input;
  inPlace.process(buffer.data(), buffer.data(), buffer.size());

  for (std::size_t i = 0; i < input.size(); ++i) {
    ASSERT_FLOAT_EQ(buffer[i], reference[i]) << "sample " << i;
  }
}

}  // namespace
}  // namespace vocalexp
