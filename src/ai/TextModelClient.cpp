/**
 * @file TextModelClient.cpp
 * @brief generateContent over HTTPS for background text tasks
 */

#include "TextModelClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../system/PsramTask.h"
#include "../util/JsonAlloc.h"
#include "GoogleRootCA.h"

bool TextModelClient::begin() {
    _requests = xQueueCreate(2, sizeof(Request));
    _results = xQueueCreate(2, sizeof(Result));
    if (!_requests || !_results) return false;
    return startPsramTask(taskEntry, "textModel", 8192, this, 1, nullptr, 1);  // core 1: see WebTools
}

void TextModelClient::setApiKey(const char* key) {
    strncpy(_apiKey, key ? key : "", sizeof(_apiKey) - 1);
    _apiKey[sizeof(_apiKey) - 1] = '\0';
}

bool TextModelClient::submit(uint32_t tag, const char* model, PsString* body) {
    Request r{tag, body, {}};
    strncpy(r.model, model, sizeof(r.model) - 1);
    if (!_requests || xQueueSend(_requests, &r, 0) != pdTRUE) {
        delete body;
        return false;
    }
    _busy = true;
    return true;
}

bool TextModelClient::submitProbe(const char* const* models, size_t modelCount, const char* const* textModels,
                                  size_t textCount) {
    if (_busy) return false;
    _probeModelCount = modelCount < kProbeMax ? modelCount : kProbeMax;
    _probeTextCount = textCount < kProbeMax ? textCount : kProbeMax;
    for (size_t i = 0; i < _probeModelCount; i++) _probeModels[i] = models[i];  // static strings
    for (size_t i = 0; i < _probeTextCount; i++) _probeText[i] = textModels[i];
    return submit(kProbeTag, "", new PsString());
}

bool TextModelClient::poll(Result& out) { return _results && xQueueReceive(_results, &out, 0) == pdTRUE; }

void TextModelClient::taskEntry(void* self) { static_cast<TextModelClient*>(self)->loop(); }

void TextModelClient::loop() {
    Request req;
    for (;;) {
        if (xQueueReceive(_requests, &req, portMAX_DELAY) != pdTRUE) continue;
        Result res{req.tag, -1, new PsString()};
        if (WiFi.status() == WL_CONNECTED && _apiKey[0]) {
            if (req.tag == kProbeTag) {
                probe(*res.text);
                res.httpStatus = 200;
            } else {
                res.httpStatus = call(req.model, *req.body, *res.text);
            }
        }
        delete req.body;
        if (xQueueSend(_results, &res, pdMS_TO_TICKS(1000)) != pdTRUE) delete res.text;
        _busy = false;
    }
}

int TextModelClient::call(const char* model, const PsString& body, PsString& text) {
    WiFiClientSecure tls;
    tls.setCACert(GOOGLE_ROOT_CA_PEM);
    tls.setTimeout(25);
    HTTPClient http;
    String url = String("https://generativelanguage.googleapis.com/v1beta/models/") + model + ":generateContent";
    if (!http.begin(tls, url)) return -2;
    http.setTimeout(25000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", _apiKey);

    const int code = http.POST((uint8_t*)body.data(), body.size());
    if (code > 0) {
        String resp = http.getString();
        if (code == 200) {
            JsonDocument doc(JsonPsramAllocator::instance());
            if (!deserializeJson(doc, resp)) {
                for (JsonObjectConst part : doc["candidates"][0]["content"]["parts"].as<JsonArrayConst>()) {
                    if (part["thought"] | false) continue;  // skip thought summaries
                    const char* t = part["text"];
                    if (t) text += t;
                }
            }
        } else {
            Serial.printf("[TextModel] %s HTTP %d: %.200s\n", model, code, resp.c_str());
        }
    } else {
        Serial.printf("[TextModel] transport error %d\n", code);
    }
    http.end();
    return code;
}

namespace {

// Pull the useful parts out of a google.rpc error body (message, status, QuotaFailure, RetryInfo)
void describeError(const String& body, JsonObject out) {
    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, body)) {
        out["raw"] = body.substring(0, 200);
        return;
    }
    JsonObjectConst err = doc["error"];
    out["status"] = err["status"] | "";
    String msg = err["message"] | "";
    out["message"] = msg.substring(0, 400);
    JsonArray quota = out["quota"].to<JsonArray>();
    for (JsonObjectConst d : err["details"].as<JsonArrayConst>()) {
        const char* type = d["@type"] | "";
        if (strstr(type, "QuotaFailure")) {
            for (JsonObjectConst v : d["violations"].as<JsonArrayConst>()) {
                JsonObject q = quota.add<JsonObject>();
                q["id"] = v["quotaId"] | "";
                q["metric"] = v["quotaMetric"] | "";
                q["limit"] = v["quotaValue"] | "";
                q["model"] = v["quotaDimensions"]["model"] | "";
            }
        } else if (strstr(type, "RetryInfo")) {
            out["retryDelay"] = d["retryDelay"] | "";
        } else if (strstr(type, "ErrorInfo")) {
            out["reason"] = d["reason"] | "";
        }
    }
}

}  // namespace

// API-key self-test: which models this key can see, and whether a 1-token call goes through.
void TextModelClient::probe(PsString& report) {
    JsonDocument out(JsonPsramAllocator::instance());
    out["keyLen"] = (uint32_t)strlen(_apiKey);
    WiFiClientSecure tls;
    tls.setCACert(GOOGLE_ROOT_CA_PEM);
    tls.setTimeout(20);
    HTTPClient http;
    http.setReuse(true);
    http.setTimeout(20000);
    const String base = "https://generativelanguage.googleapis.com/v1beta/models/";

    JsonArray models = out["models"].to<JsonArray>();
    for (size_t i = 0; i < _probeModelCount; i++) {
        JsonObject m = models.add<JsonObject>();
        m["id"] = _probeModels[i];
        if (!http.begin(tls, base + _probeModels[i])) {
            m["http"] = -2;
            continue;
        }
        http.addHeader("x-goog-api-key", _apiKey);
        const int code = http.GET();
        m["http"] = code;
        const String body = code > 0 ? http.getString() : String();
        if (code != 200 && code > 0) describeError(body, m["error"].to<JsonObject>());
        http.end();
    }

    static const char kPing[] = "{\"contents\":[{\"parts\":[{\"text\":\"ping\"}]}],"
                                "\"generationConfig\":{\"maxOutputTokens\":1}}";
    JsonArray calls = out["generate"].to<JsonArray>();
    for (size_t i = 0; i < _probeTextCount; i++) {
        JsonObject g = calls.add<JsonObject>();
        g["id"] = _probeText[i];
        if (!http.begin(tls, base + _probeText[i] + ":generateContent")) {
            g["http"] = -2;
            continue;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-goog-api-key", _apiKey);
        const int code = http.POST((uint8_t*)kPing, sizeof(kPing) - 1);
        g["http"] = code;
        const String body = code > 0 ? http.getString() : String();
        if (code != 200 && code > 0) describeError(body, g["error"].to<JsonObject>());
        http.end();
    }

    // Grounding with Google Search has its own quota. The Live API only says "exceeded your current
    // quota"; a REST call with the googleSearch tool returns the QuotaFailure details (quotaId, limit).
    if (_probeTextCount) {
        static const char kSearch[] = "{\"contents\":[{\"parts\":[{\"text\":\"Search the web: today's date\"}]}],"
                                      "\"tools\":[{\"googleSearch\":{}}],\"generationConfig\":{\"maxOutputTokens\":16}}";
        JsonObject s = out["search"].to<JsonObject>();
        s["id"] = _probeText[0];
        if (http.begin(tls, base + _probeText[0] + ":generateContent")) {
            http.addHeader("Content-Type", "application/json");
            http.addHeader("x-goog-api-key", _apiKey);
            const int code = http.POST((uint8_t*)kSearch, sizeof(kSearch) - 1);
            s["http"] = code;
            const String body = code > 0 ? http.getString() : String();
            if (code == 200) s["grounded"] = body.indexOf("groundingMetadata") >= 0;
            else if (code > 0) describeError(body, s["error"].to<JsonObject>());
            http.end();
        } else {
            s["http"] = -2;
        }
    }
    report.clear();
    serializeJson(out, report);
}
