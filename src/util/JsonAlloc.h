/**
 * @file JsonAlloc.h
 * @brief ArduinoJson allocator backed by PSRAM
 *
 * Gemini server messages carry Base64 audio (tens of KB). Parsing them with
 * the default allocator would fragment internal SRAM, which TLS needs.
 */

#pragma once

#include <ArduinoJson.h>

#include "PsramAlloc.h"

class JsonPsramAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override { return psramMalloc(size); }
    void deallocate(void* ptr) override { psramFree(ptr); }
    void* reallocate(void* ptr, size_t newSize) override { return psramRealloc(ptr, newSize); }

    static JsonPsramAllocator* instance() {
        static JsonPsramAllocator allocator;
        return &allocator;
    }
};
