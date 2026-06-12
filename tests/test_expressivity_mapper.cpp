#include "dsp/expressivity_mapper.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace vocalexp {
namespace {

TEST(ExpressivityMapper, UnityExpressivityIsIdentity) {
  ExpressivityMapper mapper;
  mapper.setExpressivity(1.0f);

  for (float f0 : {200.0f, 215.0f, 190.0f, 240.0f, 200.0f}) {
    EXPECT_FLOAT_EQ(mapper.process(f0), 1.0f) << "f0 = " << f0;
  }
}

TEST(ExpressivityMapper, ZeroExpressivityFlattensThePitchContour) {
  ExpressivityMapper mapper;
  mapper.setExpressivity(0.0f);

  // Onset anchors at 200 Hz; every later frame must be shifted back to it.
  EXPECT_FLOAT_EQ(mapper.process(200.0f), 1.0f);
  EXPECT_NEAR(mapper.process(210.0f), 200.0f / 210.0f, 1e-6f);
  EXPECT_NEAR(mapper.process(190.0f), 200.0f / 190.0f, 1e-6f);
  EXPECT_NEAR(mapper.process(250.0f), 200.0f / 250.0f, 1e-6f);
}

TEST(ExpressivityMapper, DoubleExpressivityDoublesDeviations) {
  ExpressivityMapper mapper;
  mapper.setExpressivity(2.0f);

  // Onset at 200 Hz.
  EXPECT_FLOAT_EQ(mapper.process(200.0f), 1.0f);
  // f0: 200 → 210 (Δ +10) ⇒ f0_mod: 200 + 2·10 = 220 ⇒ ratio 220/210.
  EXPECT_NEAR(mapper.process(210.0f), 220.0f / 210.0f, 1e-6f);
  // f0: 210 → 190 (Δ −20) ⇒ f0_mod: 220 − 40 = 180 ⇒ ratio 180/190.
  EXPECT_NEAR(mapper.process(190.0f), 180.0f / 190.0f, 1e-6f);
  // Back to the onset value ⇒ deviation 0 ⇒ ratio exactly 1 again.
  // f0: 190 → 200 (Δ +10) ⇒ f0_mod: 180 + 20 = 200 ⇒ ratio 1.
  EXPECT_NEAR(mapper.process(200.0f), 1.0f, 1e-6f);
}

TEST(ExpressivityMapper, DeviationFromOnsetScalesExactlyByE) {
  // For any E, f0_mod(t) − f0(0) must equal E · (f0(t) − f0(0)).
  const std::vector<float> contour = {180.0f, 195.0f, 230.0f, 210.0f, 170.0f, 185.0f};
  for (float e : {0.0f, 0.5f, 1.0f, 1.5f, 3.0f}) {
    ExpressivityMapper mapper;
    mapper.setExpressivity(e);
    for (float f0 : contour) {
      const float ratio = mapper.process(f0);
      const float modified = ratio * f0;
      const float expected = contour.front() + e * (f0 - contour.front());
      EXPECT_NEAR(modified, expected, 1e-3f) << "E = " << e << ", f0 = " << f0;
    }
  }
}

TEST(ExpressivityMapper, UnvoicedFramesPassThroughAndResetTheContour) {
  ExpressivityMapper mapper;
  mapper.setExpressivity(2.0f);

  EXPECT_FLOAT_EQ(mapper.process(200.0f), 1.0f);
  EXPECT_NEAR(mapper.process(220.0f), 240.0f / 220.0f, 1e-6f);

  // Unvoiced gap.
  EXPECT_FLOAT_EQ(mapper.process(0.0f), 1.0f);
  EXPECT_FLOAT_EQ(mapper.process(0.0f), 1.0f);

  // New voiced segment re-anchors: first frame unmodified even though the
  // pitch jumped during the gap.
  EXPECT_FLOAT_EQ(mapper.process(300.0f), 1.0f);
  EXPECT_NEAR(mapper.process(310.0f), 320.0f / 310.0f, 1e-6f);
}

TEST(ExpressivityMapper, ClampsAndRecoversWithoutWindup) {
  ExpressivityMapper::Config config;
  config.minRatio = 0.5f;
  config.maxRatio = 2.0f;
  ExpressivityMapper mapper(config);
  mapper.setExpressivity(4.0f);

  EXPECT_FLOAT_EQ(mapper.process(100.0f), 1.0f);
  // Δ +100 ⇒ f0_mod = 100 + 400 = 500 ⇒ raw ratio 2.5, clamped to 2.0.
  EXPECT_FLOAT_EQ(mapper.process(200.0f), 2.0f);
  // After clamping, the state is re-anchored at 2.0 · 200 = 400 Hz, so a
  // stable input recovers immediately instead of staying pinned:
  // Δ 0 ⇒ f0_mod stays 400 ⇒ ratio 2.0 (not above), then a falling input
  // pulls it back inside the range.
  EXPECT_FLOAT_EQ(mapper.process(200.0f), 2.0f);
  // Δ −50 ⇒ f0_mod = 400 − 200 = 200 ⇒ ratio 200/150.
  EXPECT_NEAR(mapper.process(150.0f), 200.0f / 150.0f, 1e-6f);
}

TEST(ExpressivityMapper, ResetForgetsTheAnchor) {
  ExpressivityMapper mapper;
  mapper.setExpressivity(0.0f);

  EXPECT_FLOAT_EQ(mapper.process(200.0f), 1.0f);
  EXPECT_NEAR(mapper.process(250.0f), 200.0f / 250.0f, 1e-6f);

  mapper.reset();
  EXPECT_FLOAT_EQ(mapper.process(250.0f), 1.0f);  // new anchor at 250 Hz
}

}  // namespace
}  // namespace vocalexp
