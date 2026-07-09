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
  explicit MemoryArena(std::size_t bytes) noexcept : _totalSize(bytes), _offset(0) {
    constexpr std::size_t alignment = platform::getCacheLineSize();
    
    // Request page-aligned, pre-faulted, hardware-isolated virtual address space
    _buffer = static_cast<std::uint8_t*>(platform::allocateAligned(_totalSize, alignment));
    
    if (!_buffer) {
      // Unrecoverable startup failure.
      std::abort(); 
    }
  }

  /**
   * @brief Cleans up the arena backing store and releases system page frame assignments.
   */
  ~MemoryArena() noexcept {
    constexpr std::size_t alignment = platform::getCacheLineSize();
    platform::freeAligned(_buffer, _totalSize, alignment);
  }

  // Explicitly disallow copying, moving, or assigning the underlying arena architecture.
  MemoryArena(const MemoryArena&) = delete;
  MemoryArena& operator=(const MemoryArena&) = delete;
  MemoryArena(MemoryArena&&) noexcept = delete;
  MemoryArena& operator=(MemoryArena&&) noexcept = delete;

  /**
   * @brief O(1) instruction sequences.
   * @param Total requested block in bytes.
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
   *        Utilizes platform-agnostic placement new to bypass cross-compiler SDK discrepancies.
   * @tparam T Core object structure payload.
   */
  template <typename T, typename... Args>
  requires std::is_trivially_destructible_v<T>
  [[nodiscard]] inline T* construct(Args&&... args) noexcept {
    void* storage = allocate(sizeof(T), alignof(T));  
    if (!storage) [[unlikely]] {
      return nullptr;
    }

    // High-performance replacement: Direct raw placement new.
    // This executes identically to standard C++23 lifetime behaviors without breaking on SDK mismatches.
    return ::new (storage) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Continuous array allocation segment allocator optimized across all compiler engines.
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

    T* arrayItems = static_cast<T*>(storage);  
    
    // Fallback optimization: Directly invoke initialization loop rules safely
    if constexpr (!std::is_trivially_default_constructible_v<T>) {
      for (std::size_t i = 0; i < count; i++) {
        ::new (static_cast<void*>(&arrayItems[i])) T();
      }
    }
    return arrayItems;
  }

  /**
   * @brief Software prefetch instruction hint targeting the next speculative memory block.
   *        Brings the cache lines ahead of the current _offset into L1/L2 cache concurrently.
   */
  inline void prefetchNextSegment() const noexcept {
#if defined(__GNUC__) || defined(__clang__)
    // Prefetch with read/write intent (1) and high temporal locality (3)
    __builtin_prefetch(_buffer + _offset, 1, 3);
#endif
  }

  /**
   * @brief Issues a hardware-instantaneous tracking drop to reset the active cycle space.
   *        Completely avoids iterating through indices or invalidating hot data tracks.
   */
  inline void resetPool() noexcept {
    _offset = 0;
  }

  /**
   * @brief Simple diagnostic accessor to evaluate operational metrics.
   */
  [[nodiscard]] 
  inline std::size_t getUsedBytes() const noexcept {
    return _offset;
  }

private:
  /**
   * @brief Computes raw integer pointer address from active arena state.
   */
  [[nodiscard]]
  inline std::uintptr_t getCurrentAddress() const noexcept {
    return reinterpret_cast<std::uintptr_t>(_buffer + _offset);
  }

  /**
   * @brief Aligns an address upward to a specified power-of-two alignment boundary.
   */
  [[nodiscard]]
  inline std::uintptr_t alignAddress(
    std::uintptr_t address, std::size_t alignment) const noexcept {
    return (address + (alignment - 1)) & ~(alignment - 1);
  }

  /**
   * @brief Finalizes registration indices after completing safety checks.
   */
  inline void commitAllocation(std::size_t padding, std::size_t size) noexcept {
    _offset += (padding + size);
  }

  /**
   * @brief Boundary safety validation to prevent physical memory overflow errors.
   */
  [[nodiscard]]
  inline bool hasOverflowed(
    std::size_t padding, std::size_t size) const noexcept {
    return (_offset + padding + size > _totalSize);
  }

  /**
   * @brief Mathematical overflow detection for size multiplications.
   */
  [[nodiscard]] inline bool isMultiplicationUnsafe(
    std::size_t count, std::size_t elementSize) const noexcept {
    return (count > (std::numeric_limits<std::size_t>::max() / elementSize));
  }

  std::size_t   _totalSize;
  std::size_t   _offset;
  std::uint8_t* _buffer;
};

} // namespace memory_arena