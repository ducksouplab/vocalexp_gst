// Integration tests for the vocalexp GstAudioFilter element, run through
// GstHarness — no plugin installation required (the element type is
// registered statically).

#include <gst/audio/audio.h>
#include <gst/check/gstharness.h>
#include <gst/gst.h>
#include <gtest/gtest.h>

#include <vector>

#include "dsp/pitch_tracker.hpp"
#include "gst/gstvocalexp.hpp"
#include "test_helpers.hpp"

namespace vocalexp {
namespace {

using testing::dominantFrequency;
using testing::makeSine;
using testing::makeVibratoSine;
using testing::rms;
using testing::standardDeviation;

constexpr int kRate = 48000;
constexpr const char* kCaps = "audio/x-raw,format=F32LE,rate=48000,channels=1,layout=interleaved";

class VocalexpElement : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    gst_init(nullptr, nullptr);
    ASSERT_TRUE(gst_element_register(nullptr, "vocalexp", GST_RANK_NONE, GST_TYPE_VOCALEXP));
  }
};

/// Pushes `input` through a vocalexp harness in `blockSize`-sample buffers
/// and returns the concatenated output samples.
std::vector<float> pushThrough(GstHarness* harness, const std::vector<float>& input,
                               std::size_t blockSize = 480) {
  std::vector<float> output;
  output.reserve(input.size());

  for (std::size_t offset = 0; offset < input.size(); offset += blockSize) {
    const std::size_t n = std::min(blockSize, input.size() - offset);
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, n * sizeof(float), nullptr);
    gst_buffer_fill(buffer, 0, input.data() + offset, n * sizeof(float));
    GST_BUFFER_PTS(buffer) = gst_util_uint64_scale_int(offset, GST_SECOND, kRate);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(n, GST_SECOND, kRate);

    EXPECT_EQ(gst_harness_push(harness, buffer), GST_FLOW_OK);

    while (GstBuffer* out = gst_harness_try_pull(harness)) {
      GstMapInfo map;
      EXPECT_TRUE(gst_buffer_map(out, &map, GST_MAP_READ));
      const float* samples = reinterpret_cast<const float*>(map.data);
      output.insert(output.end(), samples, samples + map.size / sizeof(float));
      gst_buffer_unmap(out, &map);
      gst_buffer_unref(out);
    }
  }
  return output;
}

TEST_F(VocalexpElement, FactoryCreatesElementWithDefaults) {
  GstElement* element = gst_element_factory_make("vocalexp", nullptr);
  ASSERT_NE(element, nullptr);

  gfloat expressivity = -1.0f;
  gboolean envelope = FALSE;
  guint frameSize = 0, overlap = 0;
  g_object_get(element, "expressivity", &expressivity, "envelope-preservation", &envelope,
               "frame-size", &frameSize, "overlap-factor", &overlap, nullptr);
  EXPECT_FLOAT_EQ(expressivity, 1.0f);
  EXPECT_EQ(envelope, TRUE);
  EXPECT_EQ(frameSize, 1024u);
  EXPECT_EQ(overlap, 4u);

  gst_object_unref(element);
}

TEST_F(VocalexpElement, PropertiesAreReadWrite) {
  GstElement* element = gst_element_factory_make("vocalexp", nullptr);
  ASSERT_NE(element, nullptr);

  g_object_set(element, "expressivity", 2.5f, "envelope-preservation", FALSE,
               "frame-size", 2048u, "overlap-factor", 8u, "min-frequency", 80.0f,
               "max-frequency", 500.0f, nullptr);

  gfloat expressivity = 0.0f, minF = 0.0f, maxF = 0.0f;
  gboolean envelope = TRUE;
  guint frameSize = 0, overlap = 0;
  g_object_get(element, "expressivity", &expressivity, "envelope-preservation", &envelope,
               "frame-size", &frameSize, "overlap-factor", &overlap, "min-frequency",
               &minF, "max-frequency", &maxF, nullptr);
  EXPECT_FLOAT_EQ(expressivity, 2.5f);
  EXPECT_EQ(envelope, FALSE);
  EXPECT_EQ(frameSize, 2048u);
  EXPECT_EQ(overlap, 8u);
  EXPECT_FLOAT_EQ(minF, 80.0f);
  EXPECT_FLOAT_EQ(maxF, 500.0f);

  gst_object_unref(element);
}

TEST_F(VocalexpElement, ProcessesAudioSampleForSample) {
  GstHarness* harness = gst_harness_new("vocalexp");
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  const std::vector<float> input = makeSine(440.0f, kRate, 48000);
  const std::vector<float> output = pushThrough(harness, input);

  // In-place transform: every input buffer yields an output buffer of the
  // same size (the 16 ms algorithmic latency lives inside the samples).
  EXPECT_EQ(output.size(), input.size());

  // Past the latency + warm-up, the output must carry the (unchanged, E=1)
  // tone at comparable level.
  const std::size_t skip = 8192;
  EXPECT_NEAR(dominantFrequency(output.data() + skip, output.size() - skip, kRate),
              440.0f, 2.0f);
  EXPECT_NEAR(rms(output.data() + skip, output.size() - skip),
              rms(input.data() + skip, input.size() - skip), 0.2f * rms(input.data(), input.size()));

  gst_harness_teardown(harness);
}

TEST_F(VocalexpElement, ZeroExpressivityFlattensVibratoEndToEnd) {
  // envelope-preservation off: on a pure tone the "envelope" is the single
  // spectral peak itself, which fights the shift; it is meant for voiced
  // spectra with many harmonics under a broad envelope. The narrowed pitch
  // range shortens the YIN buffer, reducing correction lag against the 4 Hz
  // vibrato (same configuration as the DSP-level flatten test).
  GstHarness* harness = gst_harness_new_parse(
      "vocalexp expressivity=0.0 envelope-preservation=false "
      "min-frequency=100 max-frequency=600");
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  const std::vector<float> input = makeVibratoSine(220.0f, 15.0f, 4.0f, kRate, 2 * kRate);
  const std::vector<float> output = pushThrough(harness, input);
  ASSERT_EQ(output.size(), input.size());

  // Measure both pitch contours offline with the DSP tracker.
  auto trajectory = [&](const std::vector<float>& signal) {
    PitchTracker::Config config;
    config.sampleRate = kRate;
    config.windowSize = 1024;
    config.minFrequency = 100.0f;
    config.maxFrequency = 600.0f;
    PitchTracker tracker(config);
    std::vector<float> f0;
    for (std::size_t offset = 0; offset + 256 <= signal.size(); offset += 256) {
      tracker.push(signal.data() + offset, 256);
      const PitchEstimate estimate = tracker.estimate();
      if (offset >= 24000 && estimate.voiced()) f0.push_back(estimate.f0);
    }
    return f0;
  };

  const std::vector<float> inputPitch = trajectory(input);
  const std::vector<float> outputPitch = trajectory(output);
  ASSERT_GT(outputPitch.size(), 100u);
  EXPECT_LT(standardDeviation(outputPitch), 0.35 * standardDeviation(inputPitch));

  gst_harness_teardown(harness);
}

TEST_F(VocalexpElement, ExpressivityIsChangeableMidStream) {
  GstHarness* harness = gst_harness_new("vocalexp");
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  const std::vector<float> input = makeSine(330.0f, kRate, 24000);
  std::vector<float> output = pushThrough(harness, input);

  g_object_set(harness->element, "expressivity", 2.0f, "envelope-preservation", FALSE,
               nullptr);

  const std::vector<float> more = pushThrough(harness, input);
  EXPECT_EQ(more.size(), input.size());
  // Constant pitch has zero derivative: E=2 must not alter a steady tone.
  EXPECT_NEAR(dominantFrequency(more.data() + 8192, more.size() - 8192, kRate), 330.0f,
              2.0f);

  gst_harness_teardown(harness);
}

TEST_F(VocalexpElement, ReportsStftLatency) {
  GstHarness* harness = gst_harness_new("vocalexp");
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  // Prime the element so caps are negotiated and the processor exists.
  const std::vector<float> input = makeSine(220.0f, kRate, 4800);
  pushThrough(harness, input);

  GstQuery* query = gst_query_new_latency();
  ASSERT_TRUE(gst_pad_query(GST_PAD_PEER(harness->sinkpad), query));
  gboolean live = TRUE;
  GstClockTime min = GST_CLOCK_TIME_NONE, max = 0;
  gst_query_parse_latency(query, &live, &min, &max);
  // 1024 - 256 = 768 samples @ 48 kHz = 16 ms.
  EXPECT_EQ(min, 16 * GST_MSECOND);
  gst_query_unref(query);

  gst_harness_teardown(harness);
}

TEST_F(VocalexpElement, SurvivesDiscontBuffers) {
  GstHarness* harness = gst_harness_new("vocalexp");
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  const std::vector<float> input = makeSine(220.0f, kRate, 4800);
  pushThrough(harness, input);

  GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 4800 * sizeof(float), nullptr);
  gst_buffer_fill(buffer, 0, input.data(), 4800 * sizeof(float));
  GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
  EXPECT_EQ(gst_harness_push(harness, buffer), GST_FLOW_OK);

  while (GstBuffer* out = gst_harness_try_pull(harness)) gst_buffer_unref(out);
  gst_harness_teardown(harness);
}

TEST_F(VocalexpElement, RejectsInvalidFrameConfiguration) {
  GstHarness* harness = gst_harness_new_parse("vocalexp frame-size=1000");  // not 2^n
  // Caps negotiation triggers setup(), which must fail cleanly.
  gst_harness_set_caps_str(harness, kCaps, kCaps);

  GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 480 * sizeof(float), nullptr);
  EXPECT_NE(gst_harness_push(harness, buffer), GST_FLOW_OK);
  gst_harness_teardown(harness);
}

}  // namespace
}  // namespace vocalexp
