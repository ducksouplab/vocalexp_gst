#include "dsp/phase_vocoder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "dsp/window.hpp"

namespace vocalexp {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kMinPitchRatio = 0.25f;
constexpr float kMaxPitchRatio = 4.0f;
constexpr float kEnvelopeFloor = 1e-9f;
}  // namespace

float estimateTrueFrequency(std::size_t bin, float phase, float previousPhase,
                            std::size_t fftSize, std::size_t hop, float sampleRate) {
  const float binFrequency = sampleRate / static_cast<float>(fftSize);
  const float expectedAdvance =
      kTwoPi * static_cast<float>(bin) * static_cast<float>(hop) / static_cast<float>(fftSize);
  const float deviation = principalArgument(phase - previousPhase - expectedAdvance);
  // deviation radians over `hop` samples → frequency offset from bin centre.
  const float frequencyOffset =
      deviation * static_cast<float>(fftSize) / (kTwoPi * static_cast<float>(hop)) * binFrequency;
  return static_cast<float>(bin) * binFrequency + frequencyOffset;
}

PhaseVocoder::PhaseVocoder(const Config& config)
    : config_(config),
      fft_(config.frameSize),
      hopSize_(config.frameSize / config.overlapFactor),
      numBins_(config.frameSize / 2 + 1),
      envelopePreservation_(config.envelopePreservation) {
  if (!isPowerOfTwo(config_.frameSize)) {
    throw std::invalid_argument("frameSize must be a power of two");
  }
  if (config_.overlapFactor < 2 || config_.frameSize % config_.overlapFactor != 0) {
    throw std::invalid_argument("overlapFactor must be >= 2 and divide frameSize");
  }

  window_ = makeHannWindow(config_.frameSize);
  overlapGain_ = windowOverlapGain(window_, hopSize_);

  inputFifo_.assign(config_.frameSize, 0.0f);
  outputFifo_.assign(hopSize_, 0.0f);
  outputAccumulator_.assign(config_.frameSize, 0.0f);
  fifoPosition_ = latencySamples();

  spectrum_.assign(config_.frameSize, {0.0f, 0.0f});
  lastPhase_.assign(numBins_, 0.0f);
  accumulatedPhase_.assign(numBins_, 0.0f);
  analysisMagnitude_.assign(numBins_, 0.0f);
  analysisFrequency_.assign(numBins_, 0.0f);
  synthesisMagnitude_.assign(numBins_, 0.0f);
  synthesisFrequency_.assign(numBins_, 0.0f);
  envelope_.assign(numBins_, 0.0f);

  EnvelopeExtractor::Config envConfig;
  envConfig.fftSize = config_.frameSize;
  envConfig.cepstralOrder = config_.envelopeOrder;
  envConfig.trueEnvelopeIterations = config_.trueEnvelopeIterations;
  envelopeExtractor_ = std::make_unique<EnvelopeExtractor>(envConfig);
}

void PhaseVocoder::setPitchRatio(float ratio) {
  pitchRatio_ = std::clamp(ratio, kMinPitchRatio, kMaxPitchRatio);
}

void PhaseVocoder::reset() {
  std::fill(inputFifo_.begin(), inputFifo_.end(), 0.0f);
  std::fill(outputFifo_.begin(), outputFifo_.end(), 0.0f);
  std::fill(outputAccumulator_.begin(), outputAccumulator_.end(), 0.0f);
  std::fill(lastPhase_.begin(), lastPhase_.end(), 0.0f);
  std::fill(accumulatedPhase_.begin(), accumulatedPhase_.end(), 0.0f);
  fifoPosition_ = latencySamples();
}

float PhaseVocoder::processSample(float input) {
  inputFifo_[fifoPosition_] = input;
  const float output = outputFifo_[fifoPosition_ - latencySamples()];
  ++fifoPosition_;

  if (fifoPosition_ >= config_.frameSize) {
    fifoPosition_ = latencySamples();
    processFrame();
  }
  return output;
}

void PhaseVocoder::process(const float* input, float* output, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) output[i] = processSample(input[i]);
}

void PhaseVocoder::processFrame() {
  const std::size_t n = config_.frameSize;
  const float binFrequency = config_.sampleRate / static_cast<float>(n);
  const float ratio = pitchRatio_;

  // --- Analysis: window + FFT ---
  for (std::size_t k = 0; k < n; ++k) {
    spectrum_[k] = {inputFifo_[k] * window_[k], 0.0f};
  }
  fft_.forward(spectrum_.data());

  // --- Phase unwrapping: magnitude + instantaneous frequency per bin ---
  for (std::size_t k = 0; k < numBins_; ++k) {
    analysisMagnitude_[k] = std::abs(spectrum_[k]);
    const float phase = std::arg(spectrum_[k]);
    analysisFrequency_[k] =
        estimateTrueFrequency(k, phase, lastPhase_[k], n, hopSize_, config_.sampleRate);
    lastPhase_[k] = phase;
  }

  // --- Optional envelope preservation: whiten with the current envelope ---
  const bool preserveEnvelope = envelopePreservation_;
  if (preserveEnvelope) {
    envelopeExtractor_->extract(analysisMagnitude_.data(), envelope_.data());
    for (std::size_t k = 0; k < numBins_; ++k) {
      analysisMagnitude_[k] /= std::max(envelope_[k], kEnvelopeFloor);
    }
  }

  // --- Pitch shift: remap bins and scale instantaneous frequencies ---
  std::fill(synthesisMagnitude_.begin(), synthesisMagnitude_.end(), 0.0f);
  std::fill(synthesisFrequency_.begin(), synthesisFrequency_.end(), 0.0f);
  for (std::size_t k = 0; k < numBins_; ++k) {
    const std::size_t target = static_cast<std::size_t>(static_cast<float>(k) * ratio + 0.5f);
    if (target >= numBins_) break;
    synthesisMagnitude_[target] += analysisMagnitude_[k];
    synthesisFrequency_[target] = analysisFrequency_[k] * ratio;
  }

  // --- Re-apply the original envelope at the original frequencies ---
  if (preserveEnvelope) {
    for (std::size_t k = 0; k < numBins_; ++k) {
      synthesisMagnitude_[k] *= envelope_[k];
    }
  }

  // --- Synthesis: accumulate phase consistent with the target frequencies ---
  for (std::size_t k = 0; k < numBins_; ++k) {
    const float frequencyOffset = synthesisFrequency_[k] - static_cast<float>(k) * binFrequency;
    const float phaseAdvance =
        kTwoPi * static_cast<float>(hopSize_) *
        (static_cast<float>(k) / static_cast<float>(n) + frequencyOffset / config_.sampleRate);
    accumulatedPhase_[k] = principalArgument(accumulatedPhase_[k] + phaseAdvance);
    spectrum_[k] = std::polar(synthesisMagnitude_[k], accumulatedPhase_[k]);
  }
  // Restore conjugate symmetry for a real output signal.
  for (std::size_t k = numBins_; k < n; ++k) {
    spectrum_[k] = std::conj(spectrum_[n - k]);
  }

  fft_.inverse(spectrum_.data());

  // --- Windowed overlap-add ---
  const float gain = 1.0f / overlapGain_;
  for (std::size_t k = 0; k < n; ++k) {
    outputAccumulator_[k] += spectrum_[k].real() * window_[k] * gain;
  }

  // The first hop of the accumulator is complete: emit it and slide.
  std::copy(outputAccumulator_.begin(), outputAccumulator_.begin() + hopSize_,
            outputFifo_.begin());
  std::memmove(outputAccumulator_.data(), outputAccumulator_.data() + hopSize_,
               (n - hopSize_) * sizeof(float));
  std::fill(outputAccumulator_.end() - hopSize_, outputAccumulator_.end(), 0.0f);

  // Slide the input FIFO by one hop.
  std::memmove(inputFifo_.data(), inputFifo_.data() + hopSize_,
               (n - hopSize_) * sizeof(float));
}

}  // namespace vocalexp
