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
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)

# GStreamer
pkg_check_modules(GST IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-base-1.0
  gstreamer-audio-1.0
)

# RubberBand
pkg_check_modules(RUBBERBAND IMPORTED_TARGET rubberband)
if(RUBBERBAND_FOUND)
  add_definitions(-DHAVE_RUBBERBAND)
  message(STATUS "Found RubberBand: ${RUBBERBAND_VERSION}")
endif()

# ONNX Runtime (for swift-f0)
find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_c_api.h)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
  set(ONNXRUNTIME_FOUND TRUE)
  add_definitions(-DHAVE_ONNXRUNTIME)
  message(STATUS "Found ONNX Runtime")
endif()

# ---------------------------------------------------------------------------
# Core DSP library
# ---------------------------------------------------------------------------
set(DSP_SOURCES
  src/dsp/fft.cpp
  src/dsp/phase_vocoder.cpp
  src/dsp/envelope_extractor.cpp
  src/dsp/pitch_tracker.cpp
  src/dsp/expressivity_mapper.cpp
  src/dsp/vocal_expressivity_processor.cpp
)

if(ONNXRUNTIME_FOUND)
  list(APPEND DSP_SOURCES src/dsp/swift_pitch_tracker.cpp)
endif()

if(RUBBERBAND_FOUND)
  list(APPEND DSP_SOURCES src/dsp/rubberband_stretcher.cpp)
endif()

add_library(vocalexp_dsp STATIC ${DSP_SOURCES})
target_include_directories(vocalexp_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(ONNXRUNTIME_FOUND)
  target_include_directories(vocalexp_dsp PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()
set_target_properties(vocalexp_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(RUBBERBAND_FOUND)
  target_link_libraries(vocalexp_dsp PRIVATE PkgConfig::RUBBERBAND)
endif()
if(ONNXRUNTIME_FOUND)
  target_link_libraries(vocalexp_dsp PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()

# ---------------------------------------------------------------------------
# GStreamer plugin
# ---------------------------------------------------------------------------
option(VOCALEXP_BUILD_PLUGIN "Build the GStreamer plugin" ON)

if(VOCALEXP_BUILD_PLUGIN)
  if(GST_FOUND)
    add_library(gstvocalexp MODULE src/gst/gstvocalexp.cpp)
    target_link_libraries(gstvocalexp PRIVATE vocalexp_dsp PkgConfig::GST)
    target_include_directories(gstvocalexp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(gstvocalexp PRIVATE
      PACKAGE="vocalexp"
      VERSION="${PROJECT_VERSION}"
    )

    # Install into the GStreamer plugin directory of the active prefix.
    pkg_get_variable(GST_PLUGIN_DIR gstreamer-1.0 pluginsdir)
    install(TARGETS gstvocalexp LIBRARY DESTINATION ${GST_PLUGIN_DIR})

    # Interactive live demo (mic → vocalexp → speakers with key bindings).
    add_executable(vocalexp-demo tools/vocalexp-demo.cpp)
    target_link_libraries(vocalexp-demo PRIVATE PkgConfig::GST)
  else()
    message(WARNING "GStreamer development files not found - building DSP library and tests only")
  endif()
endif()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
option(VOCALEXP_BUILD_TESTS "Build unit tests" ON)

if(VOCALEXP_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  include(GoogleTest)

  add_executable(dsp_tests
    tests/test_fft.cpp
    tests/test_phase_vocoder.cpp
    tests/test_envelope_extractor.cpp
    tests/test_pitch_tracker.cpp
    tests/test_expressivity_mapper.cpp
    tests/test_processor.cpp
  )
  target_link_libraries(dsp_tests PRIVATE vocalexp_dsp GTest::gtest_main)
  gtest_discover_tests(dsp_tests DISCOVERY_TIMEOUT 60)

  if(VOCALEXP_BUILD_PLUGIN AND GST_FOUND)
    pkg_check_modules(GST_CHECK IMPORTED_TARGET gstreamer-check-1.0)
    if(GST_CHECK_FOUND)
      # The element is compiled into the test binary and registered
      # statically, so the tests need no installed plugin.
      add_executable(gst_element_tests
        tests/test_gst_element.cpp
        src/gst/gstvocalexp.cpp
      )
      target_link_libraries(gst_element_tests PRIVATE
        vocalexp_dsp PkgConfig::GST PkgConfig::GST_CHECK GTest::gtest_main)
      target_include_directories(gst_element_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
      gtest_discover_tests(gst_element_tests DISCOVERY_TIMEOUT 60)
    else()
      message(WARNING "gstreamer-check-1.0 not found - skipping element integration tests")
    endif()
  endif()
endif()
