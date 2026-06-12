#include "dsp/expressivity_mapper.hpp"

#include <algorithm>

namespace vocalexp {

void ExpressivityMapper::reset() {
  voiced_ = false;
  previousF0_ = 0.0f;
  modifiedF0_ = 0.0f;
}

float ExpressivityMapper::process(float f0) {
  if (f0 <= 0.0f) {
    // Unvoiced or silent: pass through and restart the contour at the next
    // voiced onset.
    reset();
    return 1.0f;
  }

  if (!voiced_) {
    // Voiced onset anchors both contours; the first frame is unmodified.
    voiced_ = true;
    previousF0_ = f0;
    modifiedF0_ = f0;
    return 1.0f;
  }

  const float derivative = f0 - previousF0_;          // f0'(t)
  modifiedF0_ += expressivity_ * derivative;          // f0_mod(t)
  previousF0_ = f0;

  const float ratio = std::clamp(modifiedF0_ / f0, config_.minRatio, config_.maxRatio);
  // Re-anchor after clamping so a saturated ratio cannot wind up and keep the
  // output pitch pinned at the limit long after the input recovers.
  modifiedF0_ = ratio * f0;
  return ratio;
}

}  // namespace vocalexp
