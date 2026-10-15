/**
 * @file TextModelClient.h
 * @brief Minimal HTTPS client for generateContent (Gemini Flash-Lite text models)
 *
 * Runs in its own low-priority task: requests are queued, the HTTPS call
 * (TLS verified against the Google roots) happens off the agent task, and the
 * answer is handed back through a result queue.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "../util/PsramAlloc.h"

class TextModelClient {
public:
    struct Request {
        uint32_t tag;          // caller-defined id
        PsString* body;        // full generateContent JSON (owned, freed by the worker)
        char model[40];
    };
    struct Result {
        uint32_t tag;
        int httpStatus;        // <0 = transport error
        PsString* text;        // candidate text (owned by the receiver), may be nullptr
    };

    /** Result tag of an API-key self-test (see probe()). */
    static constexpr uint32_t kProbeTag = 0xFFFFFFFFu;

    bool begin();
    void setApiKey(const char* key);

    /** Queue a request; takes ownership of `body`. */
    bool submit(uint32_t tag, const char* model, PsString* body);
    /** Queue an API-key self-test; the JSON report arrives as a Result with tag kProbeTag.
     *  `models` = model ids to look up (GET models/{id}), `textModels` = ids to call with a
     *  1-token generateContent (shows quota details: quotaId, limit, retry delay). */
    bool submitProbe(const char* const* models, size_t modelCount, const char* const* textModels, size_t textCount);
    /** Non-blocking: true if a result was returned (caller deletes result.text). */
    bool poll(Result& out);
    bool busy() const { return _busy; }

private:
    QueueHandle_t _requests = nullptr;
    QueueHandle_t _results = nullptr;
    char _apiKey[128] = "";
    volatile bool _busy = false;

    static constexpr size_t kProbeMax = 6;
    const char* _probeModels[kProbeMax] = {};
    const char* _probeText[kProbeMax] = {};
    size_t _probeModelCount = 0, _probeTextCount = 0;

    static void taskEntry(void* self);
    void loop();
    int call(const char* model, const PsString& body, PsString& text);
    void probe(PsString& report);
};
