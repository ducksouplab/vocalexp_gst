#include "dsp/pitch_tracker.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "test_helpers.hpp"

namespace vocalexp {
namespace {

using testing::makeNoise;
using testing::makeSawtooth;
using testing::makeSine;
using testing::makeVibratoSine;

PitchTracker::Config makeConfig(float sampleRate = 48000.0f) {
  PitchTracker::Config config;
  config.sampleRate = sampleRate;
  config.windowSize = 1024;
  config.minFrequency = 60.0f;
  config.maxFrequency = 1000.0f;
  return config;
}

TEST(PitchTracker, RejectsInvalidFrequencyRange) {
  PitchTracker::Config config = makeConfig();
  config.minFrequency = 500.0f;
  config.maxFrequency = 100.0f;
  EXPECT_THROW(PitchTracker tracker(config), std::invalid_argument);
}

TEST(PitchTracker, ReturnsUnvoicedBeforeBufferFills) {
  PitchTracker tracker(makeConfig());
  const std::vector<float> signal = makeSine(220.0f, 48000.0f, 256);
  tracker.push(signal.data(), signal.size());
  EXPECT_FALSE(tracker.estimate().voiced());
}

TEST(PitchTracker, TracksSineAccurately) {
  for (float frequency : {110.0f, 220.0f, 440.0f, 880.0f}) {
    PitchTracker tracker(makeConfig());
    const std::vector<float> signal = makeSine(frequency, 48000.0f, 4096);
    tracker.push(signal.data(), signal.size());

    const PitchEstimate estimate = tracker.estimate();
    ASSERT_TRUE(estimate.voiced()) << frequency << " Hz";
    EXPECT_NEAR(estimate.f0, frequency, 0.01f * frequency) << frequency << " Hz";
    EXPECT_LT(estimate.aperiodicity, 0.05f);
  }
}

TEST(PitchTracker, TracksHarmonicRichSignalWithoutOctaveError) {
  // A sawtooth has strong harmonics; naive autocorrelation pitch trackers
  // often lock onto 2·f0 or f0/2. YIN's CMNDF must find the true f0.
  PitchTracker tracker(makeConfig());
  const std::vector<float> signal = makeSawtooth(110.0f, 48000.0f, 4096);
  tracker.push(signal.data(), signal.size());

  const PitchEstimate estimate = tracker.estimate();
  ASSERT_TRUE(estimate.voiced());
  EXPECT_NEAR(estimate.f0, 110.0f, 2.0f);
}

TEST(PitchTracker, WorksAt44100) {
  PitchTracker tracker(makeConfig(44100.0f));
  const std::vector<float> signal = makeSine(330.0f, 44100.0f, 4096);
  tracker.push(signal.data(), signal.size());

  const PitchEstimate estimate = tracker.estimate();
  ASSERT_TRUE(estimate.voiced());
  EXPECT_NEAR(estimate.f0, 330.0f, 3.0f);
}

TEST(PitchTracker, ReportsNoiseAsUnvoiced) {
  PitchTracker tracker(makeConfig());
  const std::vector<float> noise = makeNoise(8192);
  tracker.push(noise.data(), noise.size());

  const PitchEstimate estimate = tracker.estimate();
  EXPECT_FALSE(estimate.voiced());
  EXPECT_GT(estimate.aperiodicity, 0.3f);
}

TEST(PitchTracker, ReportsSilenceAsUnvoiced) {
  PitchTracker tracker(makeConfig());
  const std::vector<float> silence(8192, 0.0f);
  tracker.push(silence.data(), silence.size());
  EXPECT_FALSE(tracker.estimate().voiced());
}

TEST(PitchTracker, FollowsVibratoOverTime) {
  constexpr float kSampleRate = 48000.0f;
  constexpr float kCentre = 220.0f;
  constexpr float kDepth = 15.0f;
  constexpr std::size_t kHop = 256;

  PitchTracker tracker(makeConfig());
  const std::vector<float> signal =
      makeVibratoSine(kCentre, kDepth, 4.0f, kSampleRate, 48000);

  std::vector<float> trajectory;
  for (std::size_t offset = 0; offset + kHop <= signal.size(); offset += kHop) {
    tracker.push(signal.data() + offset, kHop);
    const PitchEstimate estimate = tracker.estimate();
    if (estimate.voiced()) trajectory.push_back(estimate.f0);
  }

  ASSERT_GT(trajectory.size(), 100u);
  float minF0 = trajectory[0], maxF0 = trajectory[0];
  for (float f : trajectory) {
    minF0 = std::min(minF0, f);
    maxF0 = std::max(maxF0, f);
  }
  // The tracker's window smooths the extremes a little; require it to see
  // most of the ±15 Hz excursion around 220 Hz.
  EXPECT_LT(minF0, kCentre - 0.6f * kDepth);
  EXPECT_GT(maxF0, kCentre + 0.6f * kDepth);
  EXPECT_NEAR(testing::mean(trajectory), kCentre, 3.0f);
}

TEST(PitchTracker, ResetClearsHistory) {
  PitchTracker tracker(makeConfig());
  const std::vector<float> signal = makeSine(220.0f, 48000.0f, 4096);
  tracker.push(signal.data(), signal.size());
  ASSERT_TRUE(tracker.estimate().voiced());

  tracker.reset();
  EXPECT_FALSE(tracker.estimate().voiced());
}

}  // namespace
}  // namespace vocalexp
