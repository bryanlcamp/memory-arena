#!/usr/bin/env bash

# Exit immediately if any command fails
set -e

# Friendly usage guide helper function
print_usage() {
  echo "================================================================="
  echo "              COMPILATION & BENCHMARK SUITE MANAGEMENT           "
  echo "================================================================="
  echo "Usage: ./build.sh [OPTION]"
  echo ""
  echo "Options:"
  echo "  --simple    Compile and execute raw allocator micro-benchmarks"
  echo "  --advanced  Compile and execute full CME MDP 3.0 SBE simulator"
  echo "  --tests     Compile and run GoogleTest alignment verification"
  echo "  --help      Display this friendly usage matrix information"
  echo ""
  echo "Note: Running without an option will safely compile all targets"
  echo "      without executing them."
  echo "================================================================="
}

# Check for help requests before triggering heavy compiler configurations
if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
  print_usage
  exit 0
fi

# Tell your compiler dynamically exactly where active Apple system libraries live
export SDKROOT="$(xcrun --show-sdk-path)"

# Purge previous artifact spaces to prevent dirty cache compilation overlaps
rm -rf build
mkdir build
cd build

# Determine optimal multi-core compilation thread boundaries
COMPILATION_CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Explicitly choose the fastest generator available on your system
if command -v ninja &>/dev/null; then
  cmake -G "Ninja" -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15 ..
  ninja
else
  cmake -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15 ..
  make -j"${COMPILATION_CORES}"
fi

# Navigate back to workspace root to evaluate execution arguments cleanly
cd ..

# Parse execution options to route runtime commands down separate paths
if [ "$1" == "--simple" ]; then
  echo ">>> Executing Raw Allocator Micro-Benchmark App..."
  ./build/bin/build_driver
  exit 0
fi

if [ "$1" == "--advanced" ]; then
  echo ">>> Executing True CME MDP 3.0 SBE Multi-Threaded Sandbox App..."
  ./build/bin/cme_sbe_demo
  exit 0
fi

if [ "$1" == "--tests" ]; then
  echo ">>> Executing GoogleTest Alignment & Boundary Verification Suite..."
  ./build/bin/memory_arena_tests
  exit 0
fi

# Handle unknown arguments gracefully to avoid silent command execution failures
if [ -n "$1" ]; then
  echo "Error: Unknown option '$1'"
  print_usage
  exit 1
fi

echo ">>> Target matrix successfully compiled in parallel mode."
