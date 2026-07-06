#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "../include/memory-arena/MemoryArena.hpp"
#include "../include/memory-arena/Platform.hpp"

namespace memory_arena::testing {

// Enforce strict cache line alignment constraints to mimic live trading structs
struct alignas(64) OrderBookDelta {
  std::uint64_t sequenceNumber;
  double topBidPx;
  double topAskPx;
  std::uint32_t totalVolume;
};

struct alignas(128) AppleSiliconAlignedStruct {
  std::uint64_t internalId;
  std::byte rawPayload[112];
};

struct SmallPackedMessage {
  std::uint8_t templateId;
  std::uint8_t schemaId;
};

/**
 * @brief Assures that memory layouts adhere strictly to type alignment parameters.
 */
TEST(MemoryArenaTest, verifyPointerAlignmentCalculations) {
  // Allocate a 1MB isolated testing memory page
  MemoryArena arena(1024 * 1024);

  // Allocate a single byte layer to intentionally throw subsequent allocations off boundary
  void* singleByte = arena.allocate(1, 1);
  ASSERT_NE(singleByte, nullptr);

  // Construct a 64-byte cache line aligned structure immediately following the raw byte allocation
  OrderBookDelta* deltaFrame = arena.construct<OrderBookDelta>();
  ASSERT_NE(deltaFrame, nullptr);

  // Verify that the pointer address resolves perfectly to a 64-byte boundary layer
  std::uintptr_t deltaAddress = reinterpret_cast<std::uintptr_t>(deltaFrame);
  EXPECT_EQ(deltaAddress % 64, 0U);

  // Construct a 128-byte cache line aligned structure to verify Apple Silicon maximum layouts
  AppleSiliconAlignedStruct* appleFrame = arena.construct<AppleSiliconAlignedStruct>();
  ASSERT_NE(appleFrame, nullptr);

  std::uintptr_t appleAddress = reinterpret_cast<std::uintptr_t>(appleFrame);
  EXPECT_EQ(appleAddress % 128, 0U);
}

/**
 * @brief Assures that C++23 array lifetime features instantiate array elements instantly.
 */
TEST(MemoryArenaTest, verifyCpp23ArrayLifetimeSemantics) {
  MemoryArena arena(4096);

  constexpr std::size_t targetLegCount = 4;
  OrderBookDelta* legArray = arena.allocateArray<OrderBookDelta>(targetLegCount);
  ASSERT_NE(legArray, nullptr);

  // Verify that elements are laid out in a perfectly contiguous block of bytes
  std::uintptr_t baseAddress = reinterpret_cast<std::uintptr_t>(&legArray[0]);
  std::uintptr_t nextAddress = reinterpret_cast<std::uintptr_t>(&legArray[1]);
  EXPECT_EQ(nextAddress - baseAddress, sizeof(OrderBookDelta));
}

/**
 * @brief Assures that memory boundaries return a deterministic nullptr on exhaustion.
 */
TEST(MemoryArenaTest, verifyOutOfMemoryBoundaryHandling) {
  // Setup a tiny arena pool capable of holding only a tight block cluster
  MemoryArena arena(32);

  // Attempt to allocate an array configuration that violates the 32-byte limit
  void* oversizedBlock = arena.allocate(64, 1);
  
  // The hot-path branch must return a safe nullptr instead of triggering an undefined state
  EXPECT_EQ(oversizedBlock, nullptr);

  // Verify that the arena's internal tracking offset remains unharmed and un-corrupted
  EXPECT_EQ(arena.getUsedBytes(), 0U);
}

/**
 * @brief Assures that executing resetPool clears memory tracking states instantaneously.
 */
TEST(MemoryArenaTest, verifyPoolResetAndMemoryReplay) {
  MemoryArena arena(2048);

  // Force allocation steps to advance tracking states upward
  SmallPackedMessage* initialMsg = arena.construct<SmallPackedMessage>();
  ASSERT_NE(initialMsg, nullptr);
  EXPECT_GT(arena.getUsedBytes(), 0U);

  // Clear memory tracking back to zero config frames instantaneously
  arena.resetPool();
  EXPECT_EQ(arena.getUsedBytes(), 0U);

  // Allocate an identical block and assure it claims the exact same memory coordinate pointer
  SmallPackedMessage* replayedMsg = arena.construct<SmallPackedMessage>();
  ASSERT_NE(replayedMsg, nullptr);
  EXPECT_EQ(initialMsg, replayedMsg);
}

} // namespace memory_arena::testing