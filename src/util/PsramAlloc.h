/**
 * @file PsramAlloc.h
 * @brief Allocators that place large/long-lived data in the 8 MB OPI PSRAM
 *
 * Internal SRAM (~320 KB) is reserved for Wi-Fi/TLS, task stacks and DMA.
 * Memory records, episode buffers and parsed JSON go to PSRAM instead.
 * On the host (unit tests) everything falls back to malloc.
 *
 * T119: when PSRAM cannot take a block, a big one no longer falls back to internal RAM if that would leave
 * less than kInternalReserve there - one 16 KB buffer used to be enough to starve TLS / Wi-Fi / esp-aes (a
 * failed allocation for that one caller beats a board where nothing can connect).
 */

#pragma once

#include <stddef.h>
#include <stdlib.h>

#include <string>
#include <vector>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace psram_detail {
constexpr size_t kSmallBlock = 512;             // always allowed to fall back (a few of these cannot hurt)
constexpr size_t kInternalReserve = 24 * 1024;  // internal RAM a fallback must leave free

#if defined(ESP_PLATFORM)
inline bool internalFallbackOk(size_t n) {
    return n <= kSmallBlock || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + kInternalReserve;
}
#endif
}  // namespace psram_detail

inline void* psramMalloc(size_t n) {
#if defined(ESP_PLATFORM)
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p || !psram_detail::internalFallbackOk(n)) return p;
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

inline void* psramRealloc(void* p, size_t n) {
#if defined(ESP_PLATFORM)
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (q || !psram_detail::internalFallbackOk(n)) return q;  // on failure p stays valid (realloc rules)
    return heap_caps_realloc(p, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return realloc(p, n);
#endif
}

inline void psramFree(void* p) { free(p); }  // heap_caps_free() == free() in ESP-IDF

template <class T>
struct PsramAllocator {
    using value_type = T;

    PsramAllocator() noexcept = default;
    template <class U>
    PsramAllocator(const PsramAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        void* p = psramMalloc(n * sizeof(T));
        if (!p) abort();  // out of memory: no sane recovery
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t) noexcept { psramFree(p); }

    template <class U>
    bool operator==(const PsramAllocator<U>&) const noexcept { return true; }
    template <class U>
    bool operator!=(const PsramAllocator<U>&) const noexcept { return false; }
};

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

template <class T>
using PsVector = std::vector<T, PsramAllocator<T>>;
