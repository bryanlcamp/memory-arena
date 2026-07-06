#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <concepts>
#include <cstdlib>
#include <type_traits>
#include <limits>
#include <memory>

#include "Platform.hpp"

namespace memory_arena {

/**
 * @brief High-performance, single-threaded, zero-allocation contiguous memory arena.
 *        Designed strictly for ultra-low latency Tier-1 trading loops.
 * 
 * @note This allocator operates on an O(1) sequential bump model. It contains no internal 
 *       synchronization locks or atomic fences. It must remain strictly confined to a single thread.
 */
class MemoryArena {
public:
  /**
   * @brief Reserves and pre-faults a block of physical memory from the operating system.
   * @param bytes The total allocation capacity requested for the backing store.
   */
  explicit MemoryArena(std::size_t bytes) noexcept : totalSize(bytes), offset(0) {
    constexpr std::size_t alignment = platform::getCacheLineSize();
    
    // Request page-aligned, pre-faulted, hardware-isolated virtual address space
    buffer = static_cast<std::uint8_t*>(platform::allocateAligned(totalSize, alignment));
    
    if (!buffer) {
      // Unrecoverable startup initialization failure. Hard abort to prevent undefined state.
      std::abort(); 
    }
  }

  /**
   * @brief Cleans up the arena backing store and releases system page frame assignments.
   */
  ~MemoryArena() noexcept {
    constexpr std::size_t alignment = platform::getCacheLineSize();
    platform::freeAligned(buffer, totalSize, alignment);
  }

  // Explicitly disallow copying, moving, or assigning the underlying arena architecture.
  MemoryArena(const MemoryArena&) = delete;
  MemoryArena& operator=(const MemoryArena&) = delete;
  MemoryArena(MemoryArena&&) noexcept = delete;
  MemoryArena& operator=(MemoryArena&&) noexcept = delete;

  /**
   * @brief Blazing-fast O(1) bump allocation instruction sequences.
   * @param size Total requested block footprint in bytes.
   * @param alignment Strict binary structural alignment parameter (e.g., 64 bytes).
   * @returns Aligned void pointer on success, or nullptr if capacity limits are breached.
   */
  [[nodiscard]] inline void* allocate(std::size_t size, std::size_t alignment) noexcept {
    const std::uintptr_t currentAddress = getCurrentAddress();
    const std::uintptr_t alignedAddress = alignAddress(currentAddress, alignment);
    const std::size_t padding = alignedAddress - currentAddress;

    if (hasOverflowed(padding, size)) [[unlikely]] {
      return nullptr;
    }
    
    commitAllocation(padding, size);
    return reinterpret_cast<void*>(alignedAddress);
  }

  /**
   * @brief In-place allocation constructor dispatcher using perfect forwarding.
   *        Utilizes C++23 lifetime semantics to maximize optimization limits.
   * @tparam T Core object structure payload.
   */
  template <typename T, typename... Args>
  requires std::is_trivially_destructible_v<T>
  [[nodiscard]] inline T* construct(Args&&... args) noexcept {
    void* storage = allocate(sizeof(T), alignof(T));  
    if (!storage) [[unlikely]] {
      return nullptr;
    }

    // C++23 Optimization: If the type is implicit-lifetime/trivially copyable, explicitly start its lifetime 
    // without calling constructors, allowing the compiler to bypass formal object creation overhead.
    if constexpr (std::is_trivially_copyable_v<T> && sizeof...(Args) == 0) {
      return std::start_lifetime_as<T>(storage);
    } else {
      return std::construct_at(static_cast<T*>(storage), std::forward<Args>(args)...);
    }
  }

  /**
   * @brief Continuous array allocation segment allocator using C++23 array lifetime features.
   * @param count Target multiplier size for array structure blocks.
   */
  template <typename T>
  requires std::is_trivially_destructible_v<T>
  [[nodiscard]] inline T* allocateArray(std::size_t count) noexcept {
    if (count == 0) [[unlikely]] {
      return nullptr;
    }

    if (isMultiplicationUnsafe(count, sizeof(T))) [[unlikely]] {
      return nullptr;
    }

    std::size_t totalBytes = sizeof(T) * count;
    void* storage = allocate(totalBytes, alignof(T));
    if (!storage) [[unlikely]] {
      return nullptr;
    }

    // C++23 Optimization: std::start_lifetime_as_array perfectly signals to the compiler that an array 
    // footprint has begun over raw bytes. This flattens vector paths and prevents any element looping.
    if constexpr (std::is_trivially_copyable_v<T>) {
      return std::start_lifetime_as_array<T>(storage, count);
    } else {
      T* arrayItems = static_cast<T*>(storage);  
      initializeArrayElements<T>(arrayItems, count);
      return arrayItems;
    }
  }

  /**
   * @brief Software prefetch instruction hint targeting the next speculative memory block.
   *        Brings the cache lines ahead of the current offset into L1/L2 cache concurrently.
   */
  inline void prefetchNextSegment() const noexcept {
#if defined(__GNUC__) || defined(__clang__)
    // Prefetch with read/write intent (1) and high temporal locality (3)
    __builtin_prefetch(buffer + offset, 1, 3);
#endif
  }

  /**
   * @brief Issues a hardware-instantaneous tracking drop to reset the active cycle space.
   *        Completely avoids iterating through indices or invalidating hot data tracks.
   */
  inline void resetPool() noexcept {
    offset = 0;
  }

  /**
   * @brief Simple diagnostic accessor to evaluate operational metrics.
   */
  [[nodiscard]] inline std::size_t getUsedBytes() const noexcept {
    return offset;
  }

private:
  std::size_t totalSize;
  std::size_t offset;
  std::uint8_t* buffer;

  /**
   * @brief Computes raw integer pointer address from active arena state.
   */
  [[nodiscard]] inline std::uintptr_t getCurrentAddress() const noexcept {
    return reinterpret_cast<std::uintptr_t>(buffer + offset);
  }

  /**
   * @brief Aligns an address upward to a specified power-of-two alignment boundary.
   */
  [[nodiscard]] inline std::uintptr_t alignAddress(std::uintptr_t address, std::size_t alignment) const noexcept {
    return (address + (alignment - 1)) & ~(alignment - 1);
  }

  /**
   * @brief Boundary safety validation to prevent physical memory overflow errors.
   */
  [[nodiscard]] inline bool hasOverflowed(std::size_t padding, std::size_t size) const noexcept {
    return (offset + padding + size > totalSize);
  }

  /**
   * @brief Finalizes registration indices after completing safety checks.
   */
  inline void commitAllocation(std::size_t padding, std::size_t size) noexcept {
    offset += (padding + size);
  }

  /**
   * @brief Mathematical overflow detection for size multiplications.
   */
  [[nodiscard]] inline bool isMultiplicationUnsafe(std::size_t count, std::size_t elementSize) const noexcept {
    return (count > (std::numeric_limits<std::size_t>::max() / elementSize));
  }

  /**
   * @brief Conditionally initializes array blocks based on type properties at compile time.
   */
  template <typename T>
  inline void initializeArrayElements(T* arrayItems, std::size_t count) noexcept {
    if constexpr (!std::is_trivially_default_constructible_v<T>) {
      for (std::size_t i = 0; i < count; i++) {
        std::construct_at(&arrayItems[i]);
      }
    }
  }
};

} // namespace memory_arena