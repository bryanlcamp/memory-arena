#pragma once

#include <cstddef>
#include <new>

#if defined(__linux__) || defined(__APPLE__)
#  include <sys/mman.h>
#  include <unistd.h>
#endif

#if defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach/thread_act.h>
#elif defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <intrin.h>
#endif

namespace memory_arena::platform {

/**
 * @brief Determines the standard hardware cache line size.
 */
inline constexpr std::size_t getCacheLineSize() noexcept {
#if defined(__cpp_lib_hardware_interference_size) && !defined(__apple_build_version__)
#  if defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Winterference-size"
#  endif

  constexpr std::size_t size = std::hardware_destructive_interference_size;

#  if defined(__GNUC__)
#    pragma GCC diagnostic pop
#  endif

  return size;
#else
#  if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
  return 128; // Apple Silicon M-series uses 128-byte cache lines
#  else
  return 64;  
#endif
#endif
}

/**
 * @brief Allocation backend for Linux environments using pre-faulted pages.
 */
inline void* allocateLinux(std::size_t size) noexcept {
#if defined(__linux__)
  void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (ptr == MAP_FAILED) {
    return nullptr;
  }
  ::madvise(ptr, size, MADV_HUGEPAGE);
  return ptr;
#else
  return nullptr;
#endif
}

/**
 * @brief Allocation backend for macOS environments using sequential page faults.
 */
inline void* allocateMac(std::size_t size) noexcept {
#if defined(__APPLE__)
  void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
    return nullptr;
  }
  
  volatile std::byte* bytePtr = static_cast<volatile std::byte*>(ptr);
  for (std::size_t i = 0; i < size; i += 4096) {
    bytePtr[i] = std::byte{0};
  }
  return ptr;
#else
  return nullptr;
#endif
}

/**
 * @brief Allocation backend for Windows environments using direct commit.
 */
inline void* allocateWindows(std::size_t size) noexcept {
#if defined(_WIN32) || defined(_WIN64)
  return ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  return nullptr;
#endif
}

/**
 * @brief Cross-platform high-performance allocations with pre-faulting.
 */
inline void* allocateAligned(std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept {
#if defined(__linux__)
  return allocateLinux(size);
#elif defined(__APPLE__)
  return allocateMac(size);
#elif defined(_WIN32) || defined(_WIN64)
  return allocateWindows(size);
#else
  return ::operator new[](size, std::align_val_t{alignment}, std::nothrow);
#endif
}

/**
 * @brief Releases blocks requested via allocateAligned.
 */
inline void freeAligned(void* ptr, [[maybe_unused]] std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept {
  if (!ptr) {
    return;
  }

#if defined(__linux__) || defined(__APPLE__)
  ::munmap(ptr, size);
#elif defined(_WIN32) || defined(_WIN64)
  ::VirtualFree(ptr, 0, MEM_RELEASE);
#else
  ::operator delete[](ptr, std::align_val_t{alignment}, std::nothrow);
#endif
}

/**
 * @brief Low-overhead intrinsic pause to yield execution slots inside tight spin locks.
 */
inline void cpuPause() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
#  if defined(_MSC_VER)
  _mm_pause();
#  else
  __builtin_ia32_pause();
#  endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#  if defined(_MSC_VER)
  __isb(_ARM64_BARRIER_SY);
#  else
  __asm__ __volatile__("isb" : : : "memory");
#  endif
#endif
}

/**
 * @brief Isolates the current thread onto a specific physical or logical execution core.
 */
inline void pinCurrentThread(int coreId) noexcept {
  if (coreId < 0) {
    return;
  }
#if defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(coreId, &cpuset);
  ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
  thread_affinity_policy_data_t policy = { coreId };
  ::thread_policy_set(::mach_thread_self(), THREAD_AFFINITY_POLICY, reinterpret_cast<thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
#elif defined(_WIN32) || defined(_WIN64)
  DWORD_PTR mask = static_cast<DWORD_PTR>(1) << coreId;
  ::SetThreadAffinityMask(::GetCurrentThread(), mask);
#endif
}

} // namespace memory_arena::platform