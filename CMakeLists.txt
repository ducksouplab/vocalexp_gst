cmake_minimum_required(VERSION 3.16)

project(vocalexp_gst
        VERSION 0.1.0
        DESCRIPTION "Real-time vocal expressivity GStreamer plugin"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Core DSP library (no GStreamer dependency — wrapped by the plugin in Phase 2)
# ---------------------------------------------------------------------------
add_library(vocalexp_dsp STATIC
  src/dsp/fft.cpp
  src/dsp/phase_vocoder.cpp
  src/dsp/envelope_extractor.cpp
  src/dsp/pitch_tracker.cpp
  src/dsp/expressivity_mapper.cpp
  src/dsp/vocal_expressivity_processor.cpp
)
target_include_directories(vocalexp_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(vocalexp_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
option(VOCALEXP_BUILD_TESTS "Build unit tests" ON)

if(VOCALEXP_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)

  add_executable(dsp_tests
    tests/test_fft.cpp
    tests/test_phase_vocoder.cpp
    tests/test_envelope_extractor.cpp
    tests/test_pitch_tracker.cpp
    tests/test_expressivity_mapper.cpp
    tests/test_processor.cpp
  )
  target_link_libraries(dsp_tests PRIVATE vocalexp_dsp GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(dsp_tests DISCOVERY_TIMEOUT 60)
endif()
