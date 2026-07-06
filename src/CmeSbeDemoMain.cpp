#include <print>
#include <cstdint>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <array>
#include <cstring>

#include "../include/memory-arena/MemoryArena.hpp"
#include "../include/spsc-ring-buffer/SpScRingBuffer.hpp"
#include "../include/spsc-ring-buffer/SpScRingBufferConsumer.hpp"

namespace cme_sbe_demo {

struct alignas(4) SbeMessageHeader {
  std::uint16_t blockLength;
  std::uint16_t templateId;
  std::uint16_t schemaId;
  std::uint16_t version;
};

struct SbeBookEntry {
  std::int64_t mdEntryPx;          
  std::int32_t mdEntrySize;        
  std::uint32_t securityId;        
  std::uint32_t rptSeq;            
  std::uint8_t numberOfOrders;     
  std::uint8_t mdUpdateAction;     
  std::uint8_t mdEntryType;        
  std::uint8_t padding;            
};

struct alignas(64) SbeIncrementalRefreshBook46 {
  std::uint64_t sendingTimeNs;     
  std::uint16_t msgSeqNum;         
  std::uint8_t mdSecurityTradingStatus; 
  std::uint8_t numEntries;         
  std::array<SbeBookEntry, 4> entries; 
};

class CmeIsolatedStrategy {
public:
  explicit CmeIsolatedStrategy(memory_arena::MemoryArena& arenaRef) : arena(arenaRef), processedCount(0) {}

  void operator()(const SbeIncrementalRefreshBook46* sbePacket) noexcept {
    if (!sbePacket) [[unlikely]] {
      return;
    }

    const std::uint64_t ingressTime = sbePacket->sendingTimeNs;
    const std::int64_t rawPrice = sbePacket->entries.mdEntryPx;
    const double convertedPrice = static_cast<double>(rawPrice) * 0.0000001;
    const std::int32_t topVolume = sbePacket->entries.mdEntrySize;

#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r"(ingressTime), "r"(convertedPrice), "r"(topVolume) : "memory");
#endif

    processedCount++;

    if ((processedCount & 15) == 0) {
      arena.resetPool();
    }
  }

  [[nodiscard]] std::size_t getProcessedCount() const noexcept {
    return processedCount;
  }

private:
  memory_arena::MemoryArena& arena;
  std::size_t processedCount;
};

inline void runIsolatedCmeWireFeed(
  memory_arena::MemoryArena& arena, 
  spsc_ring_buffer::SpScRingBuffer<const SbeIncrementalRefreshBook46*, 4096>& queue,
  std::size_t totalPackets) noexcept {
  
  alignas(64) std::array<std::byte, sizeof(SbeMessageHeader) + sizeof(SbeIncrementalRefreshBook46)> networkWireBuffer;
  
  auto* sbeHeader = reinterpret_cast<SbeMessageHeader*>(networkWireBuffer.data());
  sbeHeader->blockLength = sizeof(SbeIncrementalRefreshBook46) - 12;
  sbeHeader->templateId = 46;
  sbeHeader->schemaId = 1;
  sbeHeader->version = 9;

  auto* sbePayload = reinterpret_cast<SbeIncrementalRefreshBook46*>(networkWireBuffer.data() + sizeof(SbeMessageHeader));
  sbePayload->sendingTimeNs = 1680000000000000000ULL;
  sbePayload->msgSeqNum = 50025;
  sbePayload->mdSecurityTradingStatus = 2;
  sbePayload->numEntries = 1;

  sbePayload->entries.mdEntryPx = 51502500000; 
  sbePayload->entries.mdEntrySize = 150;      
  sbePayload->entries.securityId = 124356;    
  sbePayload->entries.rptSeq = 8439;
  sbePayload->entries.numberOfOrders = 12;
  sbePayload->entries.mdUpdateAction = 0;     
  sbePayload->entries.mdEntryType = 0;        

  for (std::size_t i = 0; i < totalPackets; i++) {
    SbeIncrementalRefreshBook46* decodedPacket = arena.construct<SbeIncrementalRefreshBook46>();

    if (decodedPacket) [[likely]] {
      std::memcpy(decodedPacket, sbePayload, sizeof(SbeIncrementalRefreshBook46));
      decodedPacket->msgSeqNum = static_cast<std::uint16_t>(50025 + i);

      while (!queue.tryPush(decodedPacket)) {
        spsc_ring_buffer::platform::cpuPause();
      }
    }
  }
}

} // namespace cme_sbe_demo

int main() {
  std::print("==================================================\n");
  std::print("   LAUNCHING ISOLATED CME SBE DEMO SANDBOX APP    \n");
  std::print("==================================================\n");

  constexpr std::size_t targetCmeVolume = 1'000'000;

  memory_arena::MemoryArena sandboxArena(32 * 1024 * 1024);
  spsc_ring_buffer::SpScRingBuffer<const cme_sbe_demo::SbeIncrementalRefreshBook46*, 4096> transportRing;
  cme_sbe_demo::CmeIsolatedStrategy strategyEngine(sandboxArena);
  spsc_ring_buffer::SpScRingBufferConsumer consumer(transportRing, std::ref(strategyEngine));

  std::print("Spawning sandbox consumer pipeline execution threads...\n");
  consumer.start(-1); 
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::print("Streaming 1M CME SBE frames through sandboxed decoder pipes...\n");
  auto startTime = std::chrono::high_resolution_clock::now();

  cme_sbe_demo::runIsolatedCmeWireFeed(sandboxArena, transportRing, targetCmeVolume);

  while (strategyEngine.getProcessedCount() < targetCmeVolume) {
    spsc_ring_buffer::platform::cpuPause();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  consumer.stop();

  auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
  double totalSec = static_cast<double>(elapsedNs) / 1'000'000'000.0;
  double latencyPerPacket = static_cast<double>(elapsedNs) / static_cast<double>(targetCmeVolume);
  double throughputMps = (static_cast<double>(targetCmeVolume) / totalSec) / 1'000'000.0;

  std::print("\n--- SANDBOX PACKET TELEMETRY PROFILE ---\n");
  std::print("Total Elapsed Processing Time: {:.6f} seconds\n", totalSec);
  std::print("Total Sandbox Decodes Executed: {} messages\n", strategyEngine.getProcessedCount());
  std::print("Mean End-to-End Latency      : {:.3f} nanoseconds\n", latencyPerPacket);
  std::print("Pipeline Throughput Capacity : {:.2f} Million packets/sec\n", throughputMps);
  std::print("Ring Buffer Packets Dropped  : {}\n", transportRing.getDropped());
  std::print("==================================================\n");

  return 0;
}
