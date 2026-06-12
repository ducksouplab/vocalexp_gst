#include "dsp/vocal_expressivity_processor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace vocalexp {

namespace {

PitchTracker::Config makeLegacyTrackerConfig(const VocalExpressivityProcessor::Config& c) {
  PitchTracker::Config tc;
  tc.sampleRate = c.sampleRate;
  tc.windowSize = c.frameSize;
  tc.minFrequency = c.minFrequency;
  tc.maxFrequency = c.maxFrequency;
  return tc;
}

PhaseVocoder::Config makeLegacyVocoderConfig(const VocalExpressivityProcessor::Config& c) {
  PhaseVocoder::Config vc;
  vc.sampleRate = c.sampleRate;
  vc.frameSize = c.frameSize;
  vc.overlapFactor = c.overlapFactor;
  vc.envelopePreservation = c.envelopePreservation;
  vc.envelopeOrder = static_cast<int>(c.envelopeOrder);
  return vc;
}

}  // namespace

VocalExpressivityProcessor::VocalExpressivityProcessor(const Config& config)
    : config_(config) {
  mapper_.setExpressivity(config.expressivity);

  if (config_.engine == Engine::LEGACY) {
    legacyTracker_ = std::make_unique<PitchTracker>(makeLegacyTrackerConfig(config_));
    legacyVocoder_ = std::make_unique<PhaseVocoder>(makeLegacyVocoderConfig(config_));
    samplesUntilUpdate_ = 0; // Trigger update on first sample
  } else {
#ifdef HAVE_ONNXRUNTIME
    SwiftPitchTracker::Config sc;
    modernTracker_ = std::make_unique<SwiftPitchTracker>(sc);
    samplesUntilSwiftUpdate_ = 0; // Trigger update on first sample
#endif
#ifdef HAVE_RUBBERBAND
    RubberBandStretcher::Config rc;
    rc.sampleRate = config_.sampleRate;
    rc.preserveFormants = config_.envelopePreservation;
    modernStretcher_ = std::make_unique<RubberBandStretcher>(rc);
#endif
  }
}

VocalExpressivityProcessor::~VocalExpressivityProcessor() = default;

void VocalExpressivityProcessor::setExpressivity(float e) {
  mapper_.setExpressivity(e);
}

void VocalExpressivityProcessor::setEnvelopePreservation(bool enabled) {
  config_.envelopePreservation = enabled;
  if (legacyVocoder_) {
    legacyVocoder_->setEnvelopePreservation(enabled);
  }
#ifdef HAVE_RUBBERBAND
  if (modernStretcher_) {
    // Note: RubberBand options are fixed at construction in current wrapper.
  }
#endif
}

bool VocalExpressivityProcessor::envelopePreservation() const {
  return config_.envelopePreservation;
}

std::size_t VocalExpressivityProcessor::latencySamples() const {
  if (config_.engine == Engine::LEGACY) {
    return legacyVocoder_ ? legacyVocoder_->latencySamples() : 0;
  }
#ifdef HAVE_RUBBERBAND
  return modernStretcher_ ? modernStretcher_->latency() : 0;
#else
  return 0;
#endif
}

std::size_t VocalExpressivityProcessor::hopSize() const {
  if (config_.engine == Engine::LEGACY) {
    return legacyVocoder_ ? legacyVocoder_->hopSize() : 0;
  }
  return static_cast<std::size_t>(config_.sampleRate * 0.016f); // 16ms
}

float VocalExpressivityProcessor::currentPitchRatio() const {
  if (config_.engine == Engine::LEGACY) {
    return legacyVocoder_ ? legacyVocoder_->pitchRatio() : 1.0f;
  }
  return 1.0f; 
}

void VocalExpressivityProcessor::reset() {
  mapper_.reset();
  currentF0_ = 0.0f;
  currentRatio_ = 1.0f;
  sampleCounter_ = 0;
  if (debugLog_) {
    debugLog_ = std::make_unique<std::ofstream>("vocalexp_debug.csv");
    *debugLog_ << "sample_index,input,f0,ratio" << std::endl;
  }

  if (legacyTracker_) legacyTracker_->reset();
  if (legacyVocoder_) legacyVocoder_->reset();
  samplesUntilUpdate_ = 0;

#ifdef HAVE_ONNXRUNTIME
  if (modernTracker_) modernTracker_->reset();
  samplesUntilSwiftUpdate_ = 0;
#endif
#ifdef HAVE_RUBBERBAND
  if (modernStretcher_) modernStretcher_->reset();
#endif
}

void VocalExpressivityProcessor::updatePitchRatio() {
  float f0 = 0.0f;
  float ratio = 1.0f;

  if (config_.engine == Engine::LEGACY) {
    const PitchEstimate estimate = legacyTracker_->estimate();
    f0 = estimate.f0;
    ratio = mapper_.process(f0);
    legacyVocoder_->setPitchRatio(ratio);
  } else {
#ifdef HAVE_ONNXRUNTIME
    const PitchEstimate estimate = modernTracker_->estimate();
    f0 = estimate.f0;
    ratio = mapper_.process(f0);
#ifdef HAVE_RUBBERBAND
    if (modernStretcher_) {
      modernStretcher_->setPitchRatio(ratio);
    }
#endif
#endif
  }
  
  currentF0_ = f0;
  currentRatio_ = ratio;

  if (config_.verbose) {
    if (!debugLog_) {
      debugLog_ = std::make_unique<std::ofstream>("vocalexp_debug.csv");
      *debugLog_ << "sample_index,input,f0,ratio" << std::endl;
    }
  }
}

void VocalExpressivityProcessor::process(const float* input, float* output, std::size_t n) {
  if (config_.engine == Engine::LEGACY) {
    processLegacy(input, output, n);
  } else {
    processModern(input, output, n);
  }

  if (config_.verbose && debugLog_) {
    for (std::size_t i = 0; i < n; ++i) {
      *debugLog_ << sampleCounter_++ << "," << input[i] << "," << currentF0_ << "," << currentRatio_ << "\n";
    }
  }
}

void VocalExpressivityProcessor::processLegacy(const float* input, float* output, std::size_t n) {
  std::size_t processed = 0;
  while (processed < n) {
    if (samplesUntilUpdate_ == 0) {
      updatePitchRatio();
      samplesUntilUpdate_ = legacyVocoder_->hopSize();
    }

    const std::size_t chunk = std::min(n - processed, samplesUntilUpdate_);
    legacyTracker_->push(input + processed, chunk);
    legacyVocoder_->process(input + processed, output + processed, chunk);

    processed += chunk;
    samplesUntilUpdate_ -= chunk;
  }
}

void VocalExpressivityProcessor::processModern(const float* input, float* output, std::size_t n) {
  std::size_t processed = 0;
  while (processed < n) {
    if (samplesUntilSwiftUpdate_ == 0) {
      updatePitchRatio();
      samplesUntilSwiftUpdate_ = static_cast<std::size_t>(config_.sampleRate * 0.016f);
    }

    const std::size_t chunk = std::min(n - processed, samplesUntilSwiftUpdate_);
    
    // 1. Resample chunk to 16kHz for SWIFT using an averaging filter
    float sr_ratio = 16000.0f / config_.sampleRate;
    std::size_t chunk16k = static_cast<std::size_t>(chunk * sr_ratio);
    if (chunk16k > 0) {
      resampled16k_.resize(chunk16k);
      float inv_ratio = 1.0f / sr_ratio;
      for (std::size_t i = 0; i < chunk16k; ++i) {
        float start = i * inv_ratio;
        float end = (i + 1) * inv_ratio;
        float sum = 0.0f;
        int count = 0;
        for (int j = static_cast<int>(start); j < static_cast<int>(end) && (processed + j) < n; ++j) {
          sum += input[processed + j];
          count++;
        }
        resampled16k_[i] = (count > 0) ? sum / count : 0.0f;
      }
#ifdef HAVE_ONNXRUNTIME
      if (modernTracker_) {
        modernTracker_->push(resampled16k_.data(), chunk16k);
      }
#endif
    }

#ifdef HAVE_RUBBERBAND
    // 2. Process through RubberBand
    if (modernStretcher_) {
      modernStretcher_->process(input + processed, output + processed, chunk);
    } else {
      std::copy(input + processed, input + processed + chunk, output + processed);
    }
#else
    std::copy(input + processed, input + processed + chunk, output + processed);
#endif

    processed += chunk;
    samplesUntilSwiftUpdate_ -= chunk;
  }
}

}  // namespace vocalexp
