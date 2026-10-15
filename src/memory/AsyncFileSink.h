/**
 * @file AsyncFileSink.h
 * @brief AppendSink that writes memory/episode lines from a low-priority task
 *
 * SD card writes can take tens of milliseconds; doing them in the agent task
 * would stall audio streaming. Lines are queued (PSRAM copies) and appended
 * in order by a dedicated writer task.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <string.h>

#include "../util/PsramAlloc.h"
#include "AppendSink.h"

class AsyncFileSink : public AppendSink {
public:
    bool begin(UBaseType_t depth = 64, BaseType_t core = 1) {
        _queue = xQueueCreate(depth, sizeof(Item));
        if (!_queue) return false;
        return xTaskCreatePinnedToCore(taskEntry, "memWriter", 4096, this, 2, nullptr, core) == pdPASS;
    }

    bool appendLine(const char* path, const char* line, size_t len) override {
        if (!_queue) return DirectFileSink::instance()->appendLine(path, line, len);
        const size_t plen = strlen(path);
        Item it;
        it.buf = (char*)psramMalloc(plen + 1 + len + 1);
        if (!it.buf) return false;
        memcpy(it.buf, path, plen + 1);
        memcpy(it.buf + plen + 1, line, len);
        it.buf[plen + 1 + len] = '\0';
        it.pathLen = plen;
        it.lineLen = len;
        if (xQueueSend(_queue, &it, pdMS_TO_TICKS(200)) != pdTRUE) {
            psramFree(it.buf);
            _dropped++;
            return false;
        }
        return true;
    }

    uint32_t written() const { return _written; }
    uint32_t failed() const { return _failed + _dropped; }
    bool idle() const { return !_queue || uxQueueMessagesWaiting(_queue) == 0; }

private:
    struct Item {
        char* buf;  // "path\0line\0"
        size_t pathLen;
        size_t lineLen;
    };
    QueueHandle_t _queue = nullptr;
    volatile uint32_t _written = 0;
    volatile uint32_t _failed = 0;
    volatile uint32_t _dropped = 0;

    static void taskEntry(void* self) { static_cast<AsyncFileSink*>(self)->loop(); }

    void loop() {
        Item it;
        for (;;) {
            if (xQueueReceive(_queue, &it, portMAX_DELAY) != pdTRUE) continue;
            const char* path = it.buf;
            const char* line = it.buf + it.pathLen + 1;
            if (DirectFileSink::instance()->appendLine(path, line, it.lineLen)) {
                _written++;
            } else {
                _failed++;
                Serial.printf("[Memory] write failed: %s\n", path);
            }
            psramFree(it.buf);
        }
    }
};
