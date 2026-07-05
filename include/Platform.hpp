#pragma once

#include <cstddef>
#include <new>

#if defined(__linux__)
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace MemoryArena::Platform {

/**
 * @brief Determines the standard hardware cache line size.
 */
inline constexpr std::size_t getCacheLineSize() noexcept 
{
#if defined(__cpp_lib_hardware_interference_size) && !defined(__apple_build_version__)
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Winterference-size"
    #endif

    constexpr std::size_t size = std::hardware_destructive_interference_size;

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif

    return size;
#else
    #if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
        return 256; 
    #else
        return 64;  
    #endif
#endif
}

/**
 * @brief Cross-platform high-performance allocations.
 * Linux maps kernel frames directly; other paths fall back to standard non-throwing allocations.
 */
inline void* allocateAligned(std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept 
{
#if defined(__linux__)
    void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) 
    {
        return nullptr;
    }
    ::madvise(ptr, size, MADV_SEQUENTIAL);
    return ptr;
#else
    // Fully robust fallback bypassing system header conflicts completely
    return ::operator new[](size, std::align_val_t{alignment}, std::nothrow);
#endif
}

/**
 * @brief Releases blocks requested via allocateAligned.
 */
inline void freeAligned(void* ptr, [[maybe_unused]] std::size_t size, [[maybe_unused]] std::size_t alignment) noexcept 
{
    if (!ptr) 
    {
        return;
    }

#if defined(__linux__)
    ::munmap(ptr, size);
#else
    // Perfectly pairs with operator new[] to prevent memory leaks and tracking errors
    ::operator delete[](ptr, std::align_val_t{alignment}, std::nothrow);
#endif
}

} // namespace MemoryArena::Platform