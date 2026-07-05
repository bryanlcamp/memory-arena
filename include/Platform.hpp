#pragma once

#include <cstddef>
#include <new>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <malloc.h>
#elif defined(__APPLE__) || defined(__linux__)
    #include <unistd.h>
    #include <stdlib.h>
#endif

namespace MemoryArena::Platform {

/**
 * @brief Safely determines the standard hardware cache line size.
 * Accounts for standard x86 architectures and Apple Silicon overrides.
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
        return 256; // Explicit boundary targeting Apple Silicon cores
    #else
        return 64;  // Standard fallback baseline metric
    #endif
#endif
}

/**
 * @brief Allocates an aligned block of raw system memory.
 */
inline void* allocateAligned(std::size_t size, std::size_t alignment) noexcept 
{
#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__linux__)
    void* ptr = nullptr;
    
    if (posix_memalign(&ptr, alignment, size) != 0) 
    {
        return nullptr;
    }
    
    return ptr;
#else
    return ::operator new(size, std::align_val_t{alignment}, std::nothrow);
#endif
}

/**
 * @brief Releases memory requested via allocateAligned.
 */
inline void freeAligned(void* ptr) noexcept 
{
    if (!ptr) 
    {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(ptr);
#elif defined(__APPLE__) || defined(__linux__)
    free(ptr);
#else
    ::operator delete(ptr, std::nothrow);
#endif
}

} // namespace MemoryArena::Platform