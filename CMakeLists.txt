cmake_minimum_required(VERSION 3.20)
project(jpegopt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(JPEGOPT_BUILD_TESTS "Build jpegopt tests" ON)

find_package(Threads REQUIRED)
include(ExternalProject)

set(TURBO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-turbo)
set(GUETZLI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/guetzli)

# ---------------------------------------------------------------------------
# libjpeg-turbo (lossless transcode + independent verification decoder)
# Built out-of-tree via ExternalProject; linked as an imported static target.
# turbojpeg-static bundles ALL of libjpeg's sources plus transupp.c, so it
# provides both the TurboJPEG API and the jpeglib API we need.
# ---------------------------------------------------------------------------
set(TURBO_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/turbo)
set(TURBO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-turbo)

ExternalProject_Add(libjpeg_turbo
  SOURCE_DIR ${TURBO_SRC}
  PREFIX ${TURBO_PREFIX}
  INSTALL_DIR ${TURBO_PREFIX}
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${TURBO_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=ON
    -DWITH_JAVA=OFF
    -DWITH_TESTS=OFF
    -DWITH_FUZZ=OFF
    -DWITH_12BIT=OFF
    -DWITH_SIMD=ON
    -DREQUIRE_SIMD=OFF
  BUILD_BYPRODUCTS
    ${TURBO_PREFIX}/lib/libturbojpeg.a)

file(MAKE_DIRECTORY ${TURBO_PREFIX}/include ${TURBO_PREFIX}/lib)
add_library(turbojpeg_static STATIC IMPORTED GLOBAL)
set_target_properties(turbojpeg_static PROPERTIES
  IMPORTED_LOCATION ${TURBO_PREFIX}/lib/libturbojpeg.a
  INTERFACE_INCLUDE_DIRECTORIES ${TURBO_PREFIX}/include)
add_dependencies(turbojpeg_static libjpeg_turbo)

# ---------------------------------------------------------------------------
# guetzli (google/guetzli) minimal subset: independent JPEG reader + writer
# that emits sequential JPEGs with cost-clustered Huffman tables. Only the
# self-contained entropy-coding files are compiled (no butteraugli/libpng).
# Vendored in-tree (not a submodule) because it carries a local patch:
# jpeg_data_reader.cc no-ops fprintf() so stdout/stderr stay clean under
# multi-threaded runs (see third_party/guetzli/JPEGOPT_PATCH.md).
# ---------------------------------------------------------------------------
add_library(guetzli_enc STATIC
  ${GUETZLI_DIR}/guetzli/entropy_encode.cc
  ${GUETZLI_DIR}/guetzli/jpeg_data.cc
  ${GUETZLI_DIR}/guetzli/jpeg_data_reader.cc
  ${GUETZLI_DIR}/guetzli/jpeg_data_writer.cc
  ${GUETZLI_DIR}/guetzli/jpeg_huffman_decode.cc)
target_include_directories(guetzli_enc PUBLIC ${GUETZLI_DIR})
target_compile_definitions(guetzli_enc PRIVATE NDEBUG)

# ---------------------------------------------------------------------------
# Core library (shared between the CLI tool and tests)
# ---------------------------------------------------------------------------
set(JPEGOPT_CORE_SOURCES
  src/cli.cpp
  src/guetzli_encode.cpp
  src/jpeg_reader.cpp
  src/pipeline.cpp
  src/report.cpp
  src/transcoder.cpp
  src/util.cpp
  src/verify.cpp)

add_library(jpegopt_core STATIC ${JPEGOPT_CORE_SOURCES})
target_include_directories(jpegopt_core PUBLIC src ${TURBO_DIR})
target_link_libraries(jpegopt_core PUBLIC
  guetzli_enc turbojpeg_static Threads::Threads m)

# ---------------------------------------------------------------------------
# The tool
# ---------------------------------------------------------------------------
add_executable(jpegopt src/main.cpp)
target_link_libraries(jpegopt PRIVATE jpegopt_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(JPEGOPT_BUILD_TESTS)
  enable_testing()
  add_executable(roundtrip_test tests/roundtrip_test.cpp)
  target_link_libraries(roundtrip_test PRIVATE jpegopt_core)
  add_test(NAME roundtrip COMMAND roundtrip_test)
endif()
