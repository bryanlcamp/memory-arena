#!/usr/bin/env bash

# Exit immediately if any command fails
set -e

std_print() {
  echo ">>> $1"
}

std_print "Starting final source folder restructuring..."

# 1. Clean out the old temporary apps folder tree
if [ -d apps ]; then
  std_print "Removing legacy apps directory..."
  rm -rf apps
fi

# 2. Ensure your pristine src/ directory exists
mkdir -p src

# 3. Overwrite master CMakeLists.txt to handle the flat src/ targets
std_print "Configuring master CMakeLists.txt with production configurations..."
cat << 'EOF' > CMakeLists.txt
cmake_minimum_required(VERSION 3.25)
project(MemoryArena CXX)

# Enforce strict compliance with C++23 standards
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find and pull native threading components for modern jthread facilities
find_package(Threads REQUIRED)

# Create an interface library target for your header-only arena component
add_library(memory_arena INTERFACE)

# Expose your include directory path cleanly to external dependencies
target_include_directories(memory_arena INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Global compiler configuration block optimized for ultra-low latency execution
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang") {
  set(GLOBAL_HFT_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -O3               # Maximum standard compiler code optimizations
    -ffast-math       # Enables aggressive mathematical pipelining and hardware vectorization
    -fno-exceptions   # Completely strips stack-unwinding code layers from hot paths
    -fno-rtti         # Eliminates runtime type lookup overhead for class structures
    -g                # Generates standalone debug tracking symbols (.dSYM)
  )

  # Check host system processor architecture to apply proper vector extensions
  if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64") {
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang") {
      list(APPEND GLOBAL_HFT_FLAGS "-mcpu=native")
    }
    else {
      list(APPEND GLOBAL_HFT_FLAGS "-march=native")
    }
  }
  else {
    list(APPEND GLOBAL_HFT_FLAGS "-march=native")
  }
}

# Windows MSVC Specific Performance Configurations
if(MSVC) {
  set(GLOBAL_HFT_FLAGS
    /W4
    /O2               # Maximize speed on Windows environments
    /Oi               # Enable intrinsic functions directly inside the assembly line
    /Ot               # Favor fast code tracks over executable size bounds
    /EHs-c-           # Completely disable exception handling code gen
    /GR-              # Disable runtime type information lookup structures
  )
}

# Target 1: Micro-Benchmark Driver Suite
add_executable(build_driver src/main.cpp)
target_link_libraries(build_driver PRIVATE memory_arena Threads::Threads)
target_compile_options(build_driver PRIVATE ${GLOBAL_HFT_FLAGS})
set_target_properties(build_driver PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Target 2: True CME MDP 3.0 SBE Multi-Threaded End-to-End Sandbox App
add_executable(cme_sbe_demo src/CmeSbeDemoMain.cpp)
target_link_libraries(cme_sbe_demo PRIVATE memory_arena Threads::Threads)
target_compile_options(cme_sbe_demo PRIVATE ${GLOBAL_HFT_FLAGS})
set_target_properties(cme_sbe_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Target 3: Minimum Baseline No-Work App (Pure Hardware Overhead Floor)
add_executable(baseline_no_work src/BaselineNoWorkMain.cpp)
target_link_libraries(baseline_no_work PRIVATE memory_arena Threads::Threads)
target_compile_options(baseline_no_work PRIVATE ${GLOBAL_HFT_FLAGS})
set_target_properties(baseline_no_work PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
EOF

std_print "Project structured successfully! Run ./build.sh to build your target matrix."