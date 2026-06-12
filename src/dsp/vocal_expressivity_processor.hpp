#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#include "dsp/expressivity_mapper.hpp"
#include "dsp/phase_vocoder.hpp"
#include "dsp/pitch_tracker.hpp"

#ifdef HAVE_ONNXRUNTIME
#include "dsp/swift_pitch_tracker.hpp"
#endif

#ifdef HAVE_RUBBERBAND
#include "dsp/rubberband_stretcher.hpp"
#endif

namespace vocalexp {

/**
 * @brief Full mono processing chain.
 * 
 * Supports two processing engines:
 * - LEGACY: Custom YIN + Phase Vocoder (default)
 * - MODERN: SWIFT-F0 (ONNX) + Rubber Band Stretcher
 */
class VocalExpressivityProcessor {
 public:
  enum class Engine {
    LEGACY,
    MODERN
  };

  struct Config {
    float sampleRate = 48000.0f;
    Engine engine = Engine::LEGACY;
    
    float expressivity = 1.0f;
    bool envelopePreservation = true;

    // Legacy (YIN/Vocoder) parameters
    std::size_t frameSize = 1024;
    std::size_t overlapFactor = 4;
    float minFrequency = 60.0f;
    float maxFrequency = 1000.0f;
    std::size_t envelopeOrder = 40;
    bool verbose = false;
  };

  explicit VocalExpressivityProcessor(const Config& config);
  ~VocalExpressivityProcessor();

  void setExpressivity(float e);
  float expressivity() const { return mapper_.expressivity(); }

  void setEnvelopePreservation(bool enabled);
  bool envelopePreservation() const;

  void setVerbose(bool enabled) { config_.verbose = enabled; }
  bool verbose() const { return config_.verbose; }

  std::size_t latencySamples() const;
  std::size_t hopSize() const;

  float currentPitchRatio() const;
  float currentF0() const { return currentF0_; }

  void process(const float* input, float* output, std::size_t n);
  void reset();

 private:
  void updatePitchRatio();
  void processLegacy(const float* input, float* output, std::size_t n);
  void processModern(const float* input, float* output, std::size_t n);

  Config config_;
  ExpressivityMapper mapper_;
  float currentF0_ = 0.0f;
  float currentRatio_ = 1.0f;
  std::unique_ptr<std::ofstream> debugLog_;
  std::size_t sampleCounter_ = 0;

  // Legacy components
  std::unique_ptr<PitchTracker> legacyTracker_;
  std::unique_ptr<PhaseVocoder> legacyVocoder_;
  std::size_t samplesUntilUpdate_ = 0;

  // Modern components
#ifdef HAVE_ONNXRUNTIME
  std::unique_ptr<SwiftPitchTracker> modernTracker_;
  std::vector<float> resampled16k_;
  std::size_t samplesUntilSwiftUpdate_ = 0;
#endif

#ifdef HAVE_RUBBERBAND
  std::unique_ptr<RubberBandStretcher> modernStretcher_;
#endif
};

}  // namespace vocalexp
