/**
 * @file PsramAlloc.h
 * @brief Allocators that place large/long-lived data in the 8 MB OPI PSRAM
 *
 * Internal SRAM (~320 KB) is reserved for Wi-Fi/TLS, task stacks and DMA.
 * Memory records, episode buffers and parsed JSON go to PSRAM instead.
 * On the host (unit tests) everything falls back to malloc.
 */

#pragma once

#include <stddef.h>
#include <stdlib.h>

#include <string>
#include <vector>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

inline void* psramMalloc(size_t n) {
#if defined(ESP_PLATFORM)
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return malloc(n);
}

inline void* psramRealloc(void* p, size_t n) {
#if defined(ESP_PLATFORM)
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (q) return q;
#endif
    return realloc(p, n);
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
