#include "dsp/vocal_expressivity_processor.hpp"

#include <algorithm>

namespace vocalexp {

namespace {

PitchTracker::Config makeTrackerConfig(const VocalExpressivityProcessor::Config& c) {
  PitchTracker::Config tc;
  tc.sampleRate = c.sampleRate;
  tc.windowSize = c.frameSize;
  tc.minFrequency = c.minFrequency;
  tc.maxFrequency = c.maxFrequency;
  return tc;
}

PhaseVocoder::Config makeVocoderConfig(const VocalExpressivityProcessor::Config& c) {
  PhaseVocoder::Config vc;
  vc.sampleRate = c.sampleRate;
  vc.frameSize = c.frameSize;
  vc.overlapFactor = c.overlapFactor;
  vc.envelopePreservation = c.envelopePreservation;
  vc.envelopeOrder = c.envelopeOrder;
  return vc;
}

}  // namespace

VocalExpressivityProcessor::VocalExpressivityProcessor(const Config& config)
    : tracker_(makeTrackerConfig(config)),
      vocoder_(makeVocoderConfig(config)),
      samplesUntilUpdate_(vocoder_.hopSize()) {
  mapper_.setExpressivity(config.expressivity);
}

void VocalExpressivityProcessor::reset() {
  tracker_.reset();
  mapper_.reset();
  vocoder_.reset();
  samplesUntilUpdate_ = vocoder_.hopSize();
  currentF0_ = 0.0f;
}

void VocalExpressivityProcessor::updatePitchRatio() {
  const PitchEstimate estimate = tracker_.estimate();
  currentF0_ = estimate.f0;
  vocoder_.setPitchRatio(mapper_.process(estimate.f0));
}

void VocalExpressivityProcessor::process(const float* input, float* output, std::size_t n) {
  // The pitch ratio is re-evaluated once per hop, synchronised with the
  // vocoder's analysis frames; the vocoder itself streams sample by sample.
  std::size_t processed = 0;
  while (processed < n) {
    const std::size_t chunk = std::min(n - processed, samplesUntilUpdate_);
    tracker_.push(input + processed, chunk);
    vocoder_.process(input + processed, output + processed, chunk);

    processed += chunk;
    samplesUntilUpdate_ -= chunk;
    if (samplesUntilUpdate_ == 0) {
      updatePitchRatio();
      samplesUntilUpdate_ = vocoder_.hopSize();
    }
  }
}

}  // namespace vocalexp
