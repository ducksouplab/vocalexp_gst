#include "dsp/expressivity_mapper.hpp"

#include <algorithm>

namespace vocalexp {

void ExpressivityMapper::reset() {
  voiced_ = false;
  previousF0_ = 0.0f;
  modifiedF0_ = 0.0f;
  unvoicedCounter_ = 0;
}

float ExpressivityMapper::process(float f0) {
  if (f0 <= 0.0f) {
    if (voiced_ && unvoicedCounter_ < kUnvoicedHoldFrames) {
      unvoicedCounter_++;
      // Hold the last known ratio during short dropouts
      return std::clamp(modifiedF0_ / previousF0_, config_.minRatio, config_.maxRatio);
    }
    // Truly unvoiced or long silence: pass through and reset.
    reset();
    return 1.0f;
  }

  if (!voiced_) {
    // Voiced onset anchors both contours; the first frame is unmodified.
    voiced_ = true;
    previousF0_ = f0;
    modifiedF0_ = f0;
    unvoicedCounter_ = 0;
    return 1.0f;
  }

  unvoicedCounter_ = 0;
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
