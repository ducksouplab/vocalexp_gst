#include "dsp/rubberband_stretcher.hpp"
#include <rubberband/RubberBandStretcher.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace vocalexp {

struct RubberBandStretcher::Impl {
    RubberBand::RubberBandStretcher stretcher;
    std::vector<float> outputFifo;
    std::size_t initialLatency;
    std::size_t extraLatency = 16384; // Extra padding for block jitter
    
    Impl(const Config& config)
        : stretcher(static_cast<std::size_t>(config.sampleRate), 
                    config.channels,
                    RubberBand::RubberBandStretcher::OptionProcessRealTime |
                    RubberBand::RubberBandStretcher::OptionEngineFiner |
                    RubberBand::RubberBandStretcher::OptionPitchHighQuality |
                    (config.preserveFormants ? RubberBand::RubberBandStretcher::OptionFormantPreserved : 0)) {
        
        // In real-time mode, getLatency() tells us the startup delay.
        // We pre-fill a FIFO with silence to bridge this gap and maintain 1:1 streaming.
        // We add extraLatency to absorb block processing jitter.
        initialLatency = stretcher.getLatency();
        outputFifo.resize(initialLatency + extraLatency, 0.0f);
    }
};

RubberBandStretcher::RubberBandStretcher(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

RubberBandStretcher::~RubberBandStretcher() = default;

void RubberBandStretcher::reset() {
    impl_->stretcher.reset();
    impl_->outputFifo.clear();
    impl_->outputFifo.resize(impl_->initialLatency + impl_->extraLatency, 0.0f);
}

void RubberBandStretcher::setPitchRatio(float ratio) {
    impl_->stretcher.setPitchScale(ratio);
}

void RubberBandStretcher::process(const float* input, float* output, std::size_t n) {
    // 1. Push input to RubberBand
    const float* src[1] = { input };
    impl_->stretcher.process(src, n, false);

    // 2. Retrieve all available output and append to our FIFO
    int avail = impl_->stretcher.available();
    while (avail > 0) {
        std::vector<float> tmp(avail);
        float* dst[1] = { tmp.data() };
        int retrieved = impl_->stretcher.retrieve(dst, avail);
        if (retrieved > 0) {
            impl_->outputFifo.insert(impl_->outputFifo.end(), tmp.begin(), tmp.begin() + retrieved);
        }
        avail = impl_->stretcher.available();
    }

    // 3. Pop exactly 'n' samples from FIFO to provide synchronous output
    if (impl_->outputFifo.size() >= n) {
        std::copy(impl_->outputFifo.begin(), impl_->outputFifo.begin() + n, output);
        impl_->outputFifo.erase(impl_->outputFifo.begin(), impl_->outputFifo.begin() + n);
    } else {
        // Underflow: RubberBand hasn't given us enough samples yet.
        // To avoid clicks, we'll repeat the last sample or just use the available ones and pad with 
        // a very short fade-out/in if it were a real problem, but here we just pad 
        // with the available samples and then silence, but we'll print a warning.
        // A better fix is to increase the initial latency.
        std::size_t availableNow = impl_->outputFifo.size();
        if (availableNow > 0) {
            std::copy(impl_->outputFifo.begin(), impl_->outputFifo.end(), output);
            impl_->outputFifo.clear();
        }
        std::fill(output + availableNow, output + n, 0.0f);
        // std::cerr << "[RubberBand] Underflow! Padded " << (n - availableNow) << " zeros." << std::endl;
    }
}

std::size_t RubberBandStretcher::latency() const {
    return impl_->initialLatency + impl_->extraLatency;
}


} // namespace vocalexp
