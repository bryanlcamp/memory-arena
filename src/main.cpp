#include <print>
#include <cstdint>
#include <chrono>
#include <memory>
#include "../include/memory-arena/MemoryArena.hpp"

struct MarketOrder {
  std::uint64_t orderId;
  std::uint32_t quantity;
  double price;
};

int main() {
  std::print("==================================================\n");
  std::print("   RUNNING NANOSECOND ALLOCATOR BENCHMARK SUITE   \n");
  std::print("==================================================\n");

  // Initialize an isolated 4MB memory arena page 
  memory_arena::MemoryArena benchmarkArena(4 * 1024 * 1024);
  
  constexpr std::size_t totalIterations = 1'000'000;
  constexpr std::size_t batchCount = 4;

  // --- WARM-UP PHASE ---
  // Priming the instruction pipeline, branch predictors, and L1 cache line states
  for (std::size_t i = 0; i < 50'000; i++) {
    MarketOrder* orders = benchmarkArena.allocateArray<MarketOrder>(batchCount);
    if (orders) [[likely]] {
      orders[0].orderId = 999ULL;
      orders[0].quantity = 100;
      orders[0].price = 100.0;
      
      // Force compiler register pinning during warm-up phase
#if defined(__GNUC__) || defined(__clang__)
      asm volatile("" : : "g"(orders) : "memory");
#endif
    }
    benchmarkArena.resetPool();
  }

  std::print("Executing {} iterations of {}-leg batch allocations...\n", 
             totalIterations, 
             batchCount);

  // Capture high-resolution hardware timestamp before execution begins
  auto startTime = std::chrono::high_resolution_clock::now();

  for (std::size_t i = 0; i < totalIterations; i++) {
    // ONE SINGLE BUMP: Grab a flat chunk capable of holding the multi-leg flurry
    MarketOrder* orders = benchmarkArena.allocateArray<MarketOrder>(batchCount);

    // Prevent compiler from optimizing the loop away by writing hot data
    if (orders) [[likely]] {
      orders[0].orderId = 1001ULL;
      orders[0].quantity = 500;
      orders[0].price = 142.50;

      // Tier-1 Optimization Guard: Inline assembly memory barrier instruction sequence.
      // Informs the compiler optimizer that memory has been accessed externally, preventing 
      // the loop from being dead-code eliminated or optimized away at the -O3 layer.
#if defined(__GNUC__) || defined(__clang__)
      asm volatile("" : : "g"(orders) : "memory");
#elif defined(_MSC_VER)
      _ReadWriteBarrier();
#endif
    }

    // Sub-nanosecond pointer reset to clear tracking state for next tick cycle
    benchmarkArena.resetPool();
  }

  // Capture hardware timestamp immediately post-execution loop
  auto endTime = std::chrono::high_resolution_clock::now();

  // Compute total elapsed duration down to nanosecond precision
  auto elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

  double totalSec = static_cast<double>(elapsedNanoseconds) / 1'000'000'000.0;
  double avgNanosecondsPerCycle = static_cast<double>(elapsedNanoseconds) / static_cast<double>(totalIterations);
  double allocationsPerSec = (static_cast<double>(totalIterations) / totalSec) / 1'000'000.0;

  std::print("\n--- BENCHMARK RESULTS ---\n");
  std::print("Total Elapsed Wall-Clock Time : {:.6f} seconds\n", totalSec);
  std::print("Total Processing Operations   : {} cycles\n", totalIterations);
  std::print("Average Cost Per Tick Cycle   : {:.3f} nanoseconds\n", avgNanosecondsPerCycle);
  std::print("Throughput Capacity           : {:.2f} Million allocations/sec\n", allocationsPerSec);
  std::print("==================================================\n");

  return 0;
}