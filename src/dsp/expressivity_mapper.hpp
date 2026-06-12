#pragma once

namespace vocalexp {

/// Maps a tracked pitch contour to a per-frame pitch-shift ratio that scales
/// the contour's variability by the `expressivity` factor E.
///
/// Per voiced frame, with f0'(t) = f0(t) - f0(t-1):
///
///   f0_mod(t) = f0_mod(t-1) + E · f0'(t)        (f0_mod(onset) = f0(onset))
///   ratio(t)  = f0_mod(t) / f0(t)
///
/// Integrating the scaled derivative makes the modified contour's deviation
/// from its onset value exactly E times the original's, so:
///   E = 1 → identity (ratio ≡ 1),
///   E = 0 → flat, monotone pitch,
///   E > 1 → exaggerated pitch excursions (hyper-expressive),
///   0 < E < 1 → flattened excursions (hypo-expressive).
///
/// Note: the one-frame-memory form "f0(t-1) + E·f0'(t)" (with the *original*
/// previous pitch as the base) would collapse to a one-frame-delayed copy of
/// the original contour at E = 0 instead of a monotone pitch; the integrated
/// form above is what actually scales the contour's standard deviation.
///
/// Unvoiced frames (f0 = 0) reset the state and map to ratio 1, so noise and
/// silence pass through unmodified. The ratio is clamped and the internal
/// state re-anchored after clamping to prevent wind-up drift.
class ExpressivityMapper {
 public:
  struct Config {
    float minRatio = 0.25f;
    float maxRatio = 4.0f;
  };

  ExpressivityMapper() = default;
  explicit ExpressivityMapper(const Config& config) : config_(config) {}

  /// E ≥ 0; 1.0 is neutral.
  void setExpressivity(float expressivity) { expressivity_ = expressivity; }
  float expressivity() const { return expressivity_; }

  /// Consumes one pitch frame (f0 in Hz, 0 = unvoiced) and returns the pitch
  /// ratio to apply to that frame.
  float process(float f0);

  /// Forgets the contour state (e.g. on stream discontinuity).
  void reset();

 private:
  Config config_;
  float expressivity_ = 1.0f;
  bool voiced_ = false;
  float previousF0_ = 0.0f;  // f0(t-1), original contour
  float modifiedF0_ = 0.0f;  // f0_mod(t-1), transformed contour
};

}  // namespace vocalexp
