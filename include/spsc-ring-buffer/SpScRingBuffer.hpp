#pragma once

#include <utility>
#include <memory>
#include <type_traits>
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>

#include "../memory-arena/Platform.hpp" 

namespace spsc_ring_buffer {

inline constexpr size_t DefaultCapacity = 1024;
inline constexpr uint64_t SpinCheckInterval = 1024;
inline constexpr uint64_t SpinCheckMask = SpinCheckInterval - 1;
inline constexpr uint32_t DefaultPushTimeoutMs = 5000;

/**
 * @brief Lock free, zero allocation, single producer single consumer ring buffer.
 */
template <typename T, size_t Capacity = DefaultCapacity>
class SpScRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");

public:
  SpScRingBuffer() : _head(0), _tail(0), _dropped(0), _peakOccupancy(0) {}

  ~SpScRingBuffer() noexcept {
    size_t tail = _tail.load(std::memory_order_relaxed);
    const size_t head = _head.load(std::memory_order_relaxed);
    while (tail != head) {
      std::destroy_at(getPtr(tail));
      tail = increment(tail);
    }
  }

  SpScRingBuffer(const SpScRingBuffer&) = delete;
  SpScRingBuffer& operator=(const SpScRingBuffer&) = delete;
  SpScRingBuffer(SpScRingBuffer&&) = delete;
  SpScRingBuffer& operator=(SpScRingBuffer&&) = delete;

  [[nodiscard]] bool tryPush(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return emplaceImpl(item);
  }

  [[nodiscard]] bool tryPush(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return emplaceImpl(std::move(item));
  }

  template <typename U>
  bool push(U&& item, uint32_t timeoutMs = DefaultPushTimeoutMs) {
    const auto startTime = std::chrono::steady_clock::now();
    const auto timeoutDuration = std::chrono::milliseconds(timeoutMs);
    uint64_t spinCounter = 0;

    while (true) {
      if (emplaceImpl(std::forward<U>(item))) {
        return true;
      }

      if ((spinCounter++ & SpinCheckMask) == 0) [[unlikely]] {
        if (std::chrono::steady_clock::now() - startTime >= timeoutDuration) {
          _dropped.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
      }

      // Fix: Redirect core pause hints directly to the shared platform namespace
      memory_arena::platform::cpuPause(); 
    }
  }

  [[nodiscard]] bool tryPop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
    const size_t tail = _tail.load(std::memory_order_relaxed);

    if (tail == _cachedHead) [[unlikely]] {
      _cachedHead = _head.load(std::memory_order_acquire); 
      if (tail == _cachedHead) [[unlikely]] {
        return false; 
      }
    }

#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(getPtr(increment(tail)), 0, 1);
#endif

    T* objectPtr = getPtr(tail);
    item = std::move(*objectPtr);
    std::destroy_at(objectPtr);

    _tail.store(increment(tail), std::memory_order_release);
    return true;
  }

  template <typename Callback>
  size_t popBatch(Callback&& callback, size_t maxBatchSize) noexcept {
    const size_t tail = _tail.load(std::memory_order_relaxed);
    
    if (tail == _cachedHead) {
      _cachedHead = _head.load(std::memory_order_acquire);
      if (tail == _cachedHead) {
        return 0; 
      }
    }

    size_t localTail = tail;
    size_t processed = 0;

    while (localTail != _cachedHead && processed < maxBatchSize) [[likely]] {
#if defined(__GNUC__) || defined(__clang__)
      __builtin_prefetch(getPtr(increment(localTail)), 0, 1);
#endif

      T* objectPtr = getPtr(localTail);
      callback(*objectPtr);
      std::destroy_at(objectPtr); 

      localTail = increment(localTail);
      ++processed;
    }

    _tail.store(localTail, std::memory_order_release);
    return processed;
  }

  template <typename Self>
  [[nodiscard]] auto* peek(this Self&& self) noexcept {
    const size_t tail = self._tail.load(std::memory_order_relaxed);
    
    if (tail == self._cachedHead) [[unlikely]] {
      self._cachedHead = self._head.load(std::memory_order_acquire);
      if (tail == self._cachedHead) [[unlikely]] {
        return static_cast<std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>>(nullptr);
      }
    }
    return self.getPtr(tail);
  }

  [[nodiscard]] constexpr size_t getCapacity() const noexcept { return Capacity; }
  [[nodiscard]] size_t getDropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
  [[nodiscard]] size_t getPeakCount() const noexcept { return _peakOccupancy.load(std::memory_order_relaxed); }

private:
  // Fix: Redirect cache line evaluation to the clean core platform namespace
  static constexpr size_t CacheLine = memory_arena::platform::getCacheLineSize();
  static constexpr size_t IndexMask = Capacity - 1;

  struct alignas(alignof(T)) StorageSlot {
    std::byte data[sizeof(T)];
  };

  alignas(CacheLine) std::array<StorageSlot, Capacity> _buffer;
  alignas(CacheLine) std::atomic<size_t> _head;
  size_t _cachedTail{0};

  alignas(CacheLine) std::atomic<size_t> _tail;
  size_t _cachedHead{0};

  alignas(CacheLine) std::atomic<size_t> _dropped;
  alignas(CacheLine) std::atomic<size_t> _peakOccupancy;

  [[nodiscard]] inline size_t increment(size_t index) const noexcept {
    return (index + 1) & IndexMask;
  }

  [[nodiscard]] inline T* getPtr(size_t index) noexcept {
    return reinterpret_cast<T*>(&_buffer[index & IndexMask].data);
  }

  [[nodiscard]] inline const T* getPtr(size_t index) const noexcept {
    return reinterpret_cast<const T*>(&_buffer[index & IndexMask].data);
  }

  template <typename... Args>
  inline bool emplaceImpl(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    const size_t head = _head.load(std::memory_order_relaxed);
    const size_t nextHead = increment(head);

    if (nextHead == _cachedTail) [[unlikely]] {
      _cachedTail = _tail.load(std::memory_order_acquire);
      if (nextHead == _cachedTail) [[unlikely]] {
        return false; 
      }
    }

    ::new (static_cast<void*>(getPtr(head))) T(std::forward<Args>(args)...);

    const size_t currentTail = _cachedTail;
    const size_t occupancy = (head >= currentTail) ? (head - currentTail) : (Capacity - (currentTail - head));
    size_t currentPeak = _peakOccupancy.load(std::memory_order_relaxed);
    if (occupancy > currentPeak) {
      _peakOccupancy.store(occupancy, std::memory_order_relaxed);
    }

    _head.store(nextHead, std::memory_order_release);
    return true;
  }
};

} // namespace spsc_ring_buffer