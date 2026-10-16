/**
 * @file PsramTask.h
 * @brief Start a FreeRTOS task with its stack in PSRAM (falls back to internal RAM)
 * @see wiki/14-AEC-WakeWord-Plan.md (phase 1: internal RAM budget on ESP-IDF 5)
 *
 * On IDF 5 the system takes ~35 KB more internal RAM than on IDF 4, and ESP-SR needs headroom too.
 * Network/decoder tasks (Gemini I/O, web tools, text model, radio) have big stacks that do not need
 * to be fast, so they live in PSRAM. Rule: a task with a PSRAM stack must never write the SPI flash
 * itself (NVS, LittleFS, OTA) - the cache is off during a flash write and IDF requires the writing
 * task to have an internal stack. Such tasks never end (vTaskDelete would need vTaskDeleteWithCaps).
 */

#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

inline bool startPsramTask(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                           TaskHandle_t* handle, BaseType_t core) {
    if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, handle, core,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return true;
    }
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core) == pdPASS;
}
