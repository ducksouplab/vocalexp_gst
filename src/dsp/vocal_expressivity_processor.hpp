#pragma once

#include <cstddef>

#include "dsp/expressivity_mapper.hpp"
#include "dsp/phase_vocoder.hpp"
#include "dsp/pitch_tracker.hpp"

namespace vocalexp {

/// Full mono processing chain: YIN pitch tracking → expressivity mapping →
/// phase-vocoder pitch shifting (with optional spectral-envelope
/// preservation).
///
/// This is the object the GStreamer element wraps in Phase 2: process() is
/// real-time safe (no allocation), produces one output sample per input
/// sample, and all parameters can be changed concurrently-safely between
/// process() calls. Algorithmic latency is latencySamples().
class VocalExpressivityProcessor {
 public:
  struct Config {
    float sampleRate = 48000.0f;
    std::size_t frameSize = 1024;    ///< STFT window (power of two).
    std::size_t overlapFactor = 4;   ///< hop = frameSize / overlapFactor.
    float expressivity = 1.0f;
    bool envelopePreservation = true;
    std::size_t envelopeOrder = 40;
    float minFrequency = 60.0f;      ///< Pitch search range.
    float maxFrequency = 1000.0f;
  };

  explicit VocalExpressivityProcessor(const Config& config);

  void setExpressivity(float e) { mapper_.setExpressivity(e); }
  float expressivity() const { return mapper_.expressivity(); }

  void setEnvelopePreservation(bool enabled) { vocoder_.setEnvelopePreservation(enabled); }
  bool envelopePreservation() const { return vocoder_.envelopePreservation(); }

  std::size_t latencySamples() const { return vocoder_.latencySamples(); }
  std::size_t hopSize() const { return vocoder_.hopSize(); }

  /// Pitch ratio applied to the most recent analysis frame (diagnostic).
  float currentPitchRatio() const { return vocoder_.pitchRatio(); }
  /// Most recent tracked f0 in Hz, 0 if unvoiced (diagnostic).
  float currentF0() const { return currentF0_; }

  /// Processes n mono samples; input and output may alias.
  void process(const float* input, float* output, std::size_t n);

  void reset();

 private:
  void updatePitchRatio();

  PitchTracker tracker_;
  ExpressivityMapper mapper_;
  PhaseVocoder vocoder_;

  std::size_t samplesUntilUpdate_;
  float currentF0_ = 0.0f;
};

}  // namespace vocalexp
