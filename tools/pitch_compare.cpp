#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include "dsp/pitch_tracker.hpp"
#include "dsp/swift_pitch_tracker.hpp"
#include <wave.h> // I need a way to read WAVs in C++

// Since I don't have a simple WAV reader here, I'll use a hacky one or 
// use GStreamer to feed samples to a tool. 
// Actually, I can use the existing test_helpers.hpp patterns if available.

#include "tests/test_helpers.hpp"

using namespace vocalexp;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.wav>" << std::endl;
        return 1;
    }

    // This is getting complicated because of WAV reading.
    // Let's instead modify the GStreamer element to dump F0 to a CSV if a 
    // specific environment variable is set. That's much easier to run.
    return 0;
}
