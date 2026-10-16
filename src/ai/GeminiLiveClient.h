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
    /** Free slots in the send queue (mic audio is never waited for: a burst must fit what is free). */
    size_t outboxSpace() const { return _outbox ? (size_t)uxQueueSpacesAvailable(_outbox) : 0; }
    /** The assistant is answering: mic audio still queued is stale - skip it so the reply gets the
     *  whole link (a phone hotspot uplink could not keep up and delayed the reply's audio). */
    void discardQueuedMic() { _micGen = (uint8_t)((_micGen + 1) % 0xFF); }
    /** Mic chunks skipped by discardQueuedMic since the last call. */
    uint32_t takeMicDiscarded() { const uint32_t v = _micDiscarded; _micDiscarded = 0; return v; }
    /** Longest frame read since the last call - kept apart from the diag value (per reply). */
    uint32_t takeTurnReadMs() { const uint32_t v = _turnReadMs; _turnReadMs = 0; return v; }
    /** Longest single frame read / write on the I/O task since the last call (diag). */
    uint32_t takeSlowReadMs() { const uint32_t v = _slowReadMs; _slowReadMs = 0; return v; }
    uint32_t takeSlowWriteMs() { const uint32_t v = _slowWriteMs; _slowWriteMs = 0; return v; }
    uint32_t audioBytesReceived() const { return _audioBytes; }
    /** Bytes written to the WebSocket so far (diag: uplink throughput). */
    uint32_t txBytes() const { return _txBytes; }

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
        uint8_t micGen;  // mic audio: generation stamp (stale ones are skipped), 0xFF = anything else
    };

    GeminiListener* _listener = nullptr;
    QueueHandle_t _inbox = nullptr;
    QueueHandle_t _outbox = nullptr;
    SemaphoreHandle_t _cfgLock = nullptr;
    SemaphoreHandle_t _closed = nullptr;  // given by the I/O task once a stop() request is done
    TaskHandle_t _io = nullptr;
    esp_transport_handle_t _ssl = nullptr;
    esp_transport_handle_t _wst = nullptr;
    // The SSL transport keeps only a POINTER to this (esp_tls_cfg_t::keep_alive_cfg) and reads it on
    // every connect: it must live as long as the transport (a local in begin() = garbage socket options
    // later, seen as TLS handshakes failing with -0x004E / -0x004C - T047).
    esp_transport_keep_alive_t _keepAlive = {};

    String _path;
    String _headers;
    bool _keyInHeader = true;  // x-goog-api-key like the SDKs: the key stays out of URLs / proxy logs
    bool _configured = false;
    bool _started = false;
    volatile uint32_t _cfgGen = 0;
    volatile bool _want = false;        // keep a connection open
    volatile bool _restartReq = false;  // close cleanly, then reconnect at once
    volatile bool _linkUp = false;      // WebSocket open (written by the I/O task)
    volatile GeminiState _state = GeminiState::Idle;
    volatile uint32_t _dropped = 0;
    volatile uint32_t _slowReadMs = 0, _slowWriteMs = 0, _turnReadMs = 0, _micDiscarded = 0;
    volatile uint8_t _micGen = 0;
    volatile bool _streamEndPending = false;  // audioStreamEnd that did not fit the queue: sent after it
    uint32_t _audioBytes = 0;
    volatile uint32_t _txBytes = 0;
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
    // Own frame reader on the TLS transport: IDF 4.4's ws transport reads the header with single
    // reads, so a header split across TLS records desynchronises the stream (audio read as a close)
    int readExact(void* dst, size_t n, int timeoutMs);  // n, or -1 (error / timed out mid-frame)
    int readOneFrame(char* buf, size_t cap, bool& fin, char* ctrl, int& ctrlLen, bool keepData);
    bool writeFrame(uint8_t opcode, char* data, int len, int timeoutMs);
    void pushMessage(MsgKind kind, const char* data, size_t len, int code);
    bool appendRx(const char* data, size_t len);

    bool enqueue(const char* data, size_t len, uint32_t waitMs, bool mic = false);
    void sendSetup();
    void handleMessage(const char* json, size_t len);
    bool ensure(void*& buf, size_t& cap, size_t need);
};
