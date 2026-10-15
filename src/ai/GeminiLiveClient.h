/**
 * @file GeminiLiveClient.h
 * @brief Gemini Live API (BidiGenerateContent) client over ESP-IDF esp_transport (WSS)
 * @see wiki/05-Gemini-Live-API-Protocol.md
 *
 * v3.1: one I/O task owns the connection. esp_websocket_client (used until v3.1) aborts the
 * connection from whichever task saw a write error, while its own task may be inside
 * esp_transport_poll_read() on the same TLS context; on a weak Wi-Fi link (audio write
 * timeouts) that race freed the TLS session twice -> "CORRUPT HEAP ... multi_heap_free" reboot.
 * Now:
 *   - the I/O task alone connects (TLS verified against the Google roots), writes queued
 *     messages, reads/reassembles frames, answers pings and closes with a handshake
 *   - other tasks only enqueue messages (copied to PSRAM) and read events through poll()
 *   - frames of any size are reassembled; binary frames are treated like text (both carry JSON)
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef GEMINI_MODEL
#define GEMINI_MODEL    "models/gemini-3.8-live"
#endif
#ifndef GEMINI_HOST
#define GEMINI_HOST     "generativelanguage.googleapis.com"
#endif
#ifndef GEMINI_PORT
#define GEMINI_PORT     443
#endif
#ifndef GEMINI_WS_PATH
#define GEMINI_WS_PATH  "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
#endif

/** Receives protocol events (all calls happen inside GeminiLiveClient::poll()). */
class GeminiListener {
public:
    virtual ~GeminiListener() = default;
    /** Fill the BidiGenerateContentSetup object (first message of every connection). */
    virtual void buildSetup(JsonObject setup) = 0;
    virtual void onSetupComplete() {}
    virtual void onDisconnected(bool wasReady) { (void)wasReady; }
    virtual void onServerClose(int code, const char* reason) { (void)code; (void)reason; }
    virtual void onAudio(const int16_t* pcm, size_t samples) { (void)pcm; (void)samples; }
    virtual void onInputTranscript(const char* text) { (void)text; }
    virtual void onOutputTranscript(const char* text) { (void)text; }
    virtual void onInterrupted() {}
    virtual void onGenerationComplete() {}
    virtual void onTurnComplete() {}
    virtual void onToolCall(JsonArrayConst calls) { (void)calls; }
    virtual void onToolCallCancellation(JsonArrayConst ids) { (void)ids; }
    virtual void onSessionHandle(const char* handle, bool resumable) { (void)handle; (void)resumable; }
    virtual void onGoAway(uint32_t msLeft) { (void)msLeft; }
    /** usageMetadata.totalTokenCount of a server message (billing / TPM tracking). */
    virtual void onUsage(uint32_t totalTokens) { (void)totalTokens; }
};

enum class GeminiState : uint8_t { Idle, Connecting, SetupSent, Ready };

class GeminiLiveClient {
public:
    /** Create the transports and the I/O task. */
    bool begin(GeminiListener* listener);

    /** Set the API key (stops a running session; start() connects with the new key). */
    bool configure(const char* apiKey);
    /** Send the key as an x-goog-api-key header instead of ?key= (takes effect on configure()). */
    void setKeyInHeader(bool on) { _keyInHeader = on; }
    bool keyInHeader() const { return _keyInHeader; }

    /** Open the connection (reconnects by itself afterwards). Call once Wi-Fi is up. */
    bool start();

    /** Close the session with the WebSocket handshake (sleep mode). start() opens it again. */
    void stop();

    /** Close the current connection cleanly and reconnect now (new or resumed session). */
    void restart();

    bool started() const { return _started; }

    /** Dispatch queued server events in the calling task. */
    void poll(uint32_t budgetMs = 30);

    /** Stream mic PCM (16-bit mono). Returns false if not ready or the send queue is full. */
    bool sendAudio(const int16_t* pcm, size_t samples, uint32_t sampleRate);

    /** Tell the server the mic stream paused (flushes its voice activity detection). */
    bool sendAudioStreamEnd();

    /** Send any client message (e.g. toolResponse). */
    bool sendJson(const JsonDocument& doc);

    /** clientContent with one user text turn (e.g. "announce this reminder"). */
    bool sendClientText(const char* text, bool turnComplete = true);

    bool isReady() const { return _state == GeminiState::Ready; }
    GeminiState state() const { return _state; }
    uint32_t droppedMessages() const { return _dropped; }
    uint32_t audioBytesReceived() const { return _audioBytes; }

private:
    enum class MsgKind : uint8_t { Connected, Disconnected, Data, Closed };
    struct InboundMsg {
        MsgKind kind;
        int code;
        char* data;  // PSRAM, NUL-terminated, freed by poll()
        size_t len;
    };
    struct OutboundMsg {
        char* data;  // PSRAM copy (the WS transport masks it in place), freed by the I/O task
        uint32_t len;
        uint8_t opcode;
    };

    GeminiListener* _listener = nullptr;
    QueueHandle_t _inbox = nullptr;
    QueueHandle_t _outbox = nullptr;
    SemaphoreHandle_t _cfgLock = nullptr;
    SemaphoreHandle_t _closed = nullptr;  // given by the I/O task once a stop() request is done
    TaskHandle_t _io = nullptr;
    esp_transport_handle_t _ssl = nullptr;
    esp_transport_handle_t _wst = nullptr;

    String _path;
    String _headers;
    bool _keyInHeader = false;
    bool _configured = false;
    bool _started = false;
    volatile uint32_t _cfgGen = 0;
    volatile bool _want = false;        // keep a connection open
    volatile bool _restartReq = false;  // close cleanly, then reconnect at once
    volatile bool _linkUp = false;      // WebSocket open (written by the I/O task)
    volatile GeminiState _state = GeminiState::Idle;
    volatile uint32_t _dropped = 0;
    uint32_t _audioBytes = 0;
    uint32_t _setupAt = 0;
    static constexpr uint32_t kSetupTimeoutMs = 15000;

    // ---- I/O task only ----
    char* _rx = nullptr;  // message reassembly (PSRAM)
    size_t _rxLen = 0;
    size_t _rxCap = 0;
    bool _setupOnLink = false;  // the first frame of a link must be the setup
    uint32_t _backoffMs = 1000;
    uint32_t _nextAttempt = 0;
    uint32_t _lastRx = 0, _lastPing = 0;
    uint32_t _appliedGen = 0;

    // ---- caller task scratch buffers ----
    char* _tx = nullptr;
    size_t _txCap = 0;
    uint8_t* _pcm = nullptr;
    size_t _pcmCap = 0;

    static void ioEntry(void* self);
    void ioLoop();
    void applyTransportConfig();
    bool connectLink();
    void closeLink(bool handshake, bool notify);
    void dropLink(const char* why);
    void dropOutbox();
    bool flushOutbox();
    int readFrame(char* buf, size_t cap);  // 1 ok, 0 closed by the server, -1 error
    bool writeFrame(uint8_t opcode, char* data, int len, int timeoutMs);
    void pushMessage(MsgKind kind, const char* data, size_t len, int code);
    bool appendRx(const char* data, size_t len);

    bool enqueue(const char* data, size_t len, uint32_t waitMs);
    void sendSetup();
    void handleMessage(const char* json, size_t len);
    bool ensure(void*& buf, size_t& cap, size_t need);
};
