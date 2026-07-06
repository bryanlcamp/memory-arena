#include <print>
#include <cstdint>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>

#include "../include/memory-arena/MemoryArena.hpp"
#include "../include/spsc-ring-buffer/SpScRingBuffer.hpp"
#include "../include/spsc-ring-buffer/SpScRingBufferConsumer.hpp"

namespace baseline_no_work {

struct EmptySignal {};

class PassiveConsumer {
public:
  explicit PassiveConsumer(std::atomic<std::size_t>& counterRef) : totalCount(counterRef) {}

  void operator()(const EmptySignal* signal) noexcept {
    if (!signal) [[unlikely]] {
      return;
    }
    totalCount.fetch_add(1, std::memory_order_relaxed);
  }

private:
  std::atomic<std::size_t>& totalCount;
};

} // namespace baseline_no_work

int main() {
  std::print("==================================================\n");
  std::print("   RUNNING RAW LATENCY OVERHEAD BASELINE TEST     \n");
  std::print("==================================================\n");

  constexpr std::size_t testVolume = 1'000'000;
  std::atomic<std::size_t> receivedCount{0};

  memory_arena::MemoryArena baselineArena(4 * 1024 * 1024);
  spsc_ring_buffer::SpScRingBuffer<const baseline_no_work::EmptySignal*, 4096> transportRing;
  baseline_no_work::PassiveConsumer passiveStrategy(receivedCount);
  spsc_ring_buffer::SpScRingBufferConsumer consumer(transportRing, std::ref(passiveStrategy));

  std::print("Instantiating isolated baseline execution thread...\n");
  consumer.start(-1);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto startTime = std::chrono::high_resolution_clock::now();

  for (std::size_t i = 0; i < testVolume; i++) {
    auto* signal = baselineArena.construct<baseline_no_work::EmptySignal>();

    if (signal) [[likely]] {
      while (!transportRing.tryPush(signal)) {
        spsc_ring_buffer::platform::cpuPause();
      }
    }
    if ((i & 15) == 0) {
      baselineArena.resetPool();
    }
  }

  while (receivedCount.load(std::memory_order_relaxed) < testVolume) {
    spsc_ring_buffer::platform::cpuPause();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  consumer.stop();

  auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
  double totalSec = static_cast<double>(elapsedNs) / 1'000'000'000.0;
  double baselineLatency = static_cast<double>(elapsedNs) / static_cast<double>(testVolume);

  std::print("\n--- BASELINE METRICS (PURE HARDWARE OVERHEAD) ---\n");
  std::print("Total Running Time           : {:.6f} seconds\n", totalSec);
  std::print("Absolute Transit Floor       : {:.3f} nanoseconds/cycle\n", baselineLatency);
  std::print("==================================================\n");

  return 0;
}
