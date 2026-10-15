/**
 * @file GeminiLiveClient.cpp
 * @brief Gemini Live API client: single-owner WSS transport, message reassembly, protocol parsing
 * @see wiki/05-Gemini-Live-API-Protocol.md
 *
 * Protocol: WSS -> BidiGenerateContent
 *   client: setup -> realtimeInput.audio (16 kHz PCM, Base64) / toolResponse
 *   server: setupComplete, serverContent (audio 24 kHz, transcripts, turn flags),
 *           toolCall, sessionResumptionUpdate, goAway
 */

#include "GeminiLiveClient.h"

#include <stdlib.h>
#include <string.h>

#include "../util/Base64.h"
#include "../util/JsonAlloc.h"
#include "../util/JsonFrame.h"
#include "../util/PsramAlloc.h"
#include "GoogleRootCA.h"

namespace {
constexpr size_t kMaxMessageBytes = 1024 * 1024;  // sanity limit for one server message
constexpr UBaseType_t kInboxDepth = 48;
constexpr UBaseType_t kOutboxDepth = 64;          // ~2.5 s of 40-ms audio chunks
constexpr int kConnectTimeoutMs = 10000;
constexpr int kWriteTimeoutMs = 5000;             // a slow link may take a while; a stalled one is dead
constexpr int kReadTimeoutMs = 5000;              // rest of a frame once its header arrived
constexpr uint32_t kPingIntervalMs = 15000;
constexpr uint32_t kDeadLinkMs = 45000;           // nothing received (not even a pong)
constexpr uint32_t kCloseWaitMs = 1500;           // for the server's close reply
constexpr uint32_t kMaxBackoffMs = 15000;
constexpr uint32_t kIoStack = 10240;              // TLS records need ~6 KB

// "12.5s" -> 12500 ms (google.protobuf.Duration JSON form)
uint32_t parseDurationMs(const char* s) {
    if (!s || !*s) return 0;
    return (uint32_t)(strtof(s, nullptr) * 1000.0f);
}
}  // namespace

bool GeminiLiveClient::begin(GeminiListener* listener) {
    _listener = listener;
    _inbox = xQueueCreate(kInboxDepth, sizeof(InboundMsg));
    _outbox = xQueueCreate(kOutboxDepth, sizeof(OutboundMsg));
    _cfgLock = xSemaphoreCreateMutex();
    _closed = xSemaphoreCreateBinary();
    if (!_inbox || !_outbox || !_cfgLock || !_closed) return false;

    _ssl = esp_transport_ssl_init();
    if (!_ssl) return false;
    esp_transport_ssl_set_cert_data(_ssl, GOOGLE_ROOT_CA_PEM, strlen(GOOGLE_ROOT_CA_PEM));  // verify the server
    _keepAlive.keep_alive_enable = true;  // TCP keep-alive finds a dead Wi-Fi link even when we are silent
    _keepAlive.keep_alive_idle = 10;
    _keepAlive.keep_alive_interval = 5;
    _keepAlive.keep_alive_count = 3;
    esp_transport_ssl_set_keep_alive(_ssl, &_keepAlive);  // stores the pointer: a member, not a local
    _wst = esp_transport_ws_init(_ssl);
    if (!_wst) return false;
    esp_transport_set_default_port(_wst, GEMINI_PORT);
    return xTaskCreatePinnedToCore(ioEntry, "geminiIO", kIoStack, this, 5, &_io, tskNO_AFFINITY) == pdPASS;
}

bool GeminiLiveClient::configure(const char* apiKey) {
    if (_started) stop();
    _state = GeminiState::Idle;
    _configured = apiKey && strlen(apiKey) >= 20;
    if (!_configured) return false;

    xSemaphoreTake(_cfgLock, portMAX_DELAY);
    // Default: the x-goog-api-key header, as the official SDKs send it (the key never appears in a URL).
    // The documented ?key= form is kept for diagnosis (serial "keymode query|header", not saved).
    if (_keyInHeader) {
        _path = GEMINI_WS_PATH;
        _headers = String("x-goog-api-key: ") + apiKey + "\r\n";
    } else {
        _path = String(GEMINI_WS_PATH) + "?key=" + apiKey;
        _headers = "";
    }
    _cfgGen = _cfgGen + 1;
    xSemaphoreGive(_cfgLock);
    Serial.println("[Gemini] Client configured (TLS verified against Google roots)");
    return true;
}

bool GeminiLiveClient::start() {
    if (!_configured || !_io) return false;
    if (_started) return true;
    _state = GeminiState::Connecting;
    _started = true;
    _want = true;
    xTaskNotifyGive(_io);
    return true;
}

void GeminiLiveClient::stop() {
    if (!_started) return;
    Serial.println("[Gemini] Closing session");
    xSemaphoreTake(_closed, 0);  // clear a stale signal
    _want = false;
    _restartReq = false;
    xTaskNotifyGive(_io);
    if (xSemaphoreTake(_closed, pdMS_TO_TICKS(kCloseWaitMs + 2000)) != pdTRUE) {
        Serial.println("[Gemini] close still in progress (I/O task busy)");
    }
    _started = false;
    _state = GeminiState::Idle;
    // drop anything still queued from the old connection
    InboundMsg m;
    while (_inbox && xQueueReceive(_inbox, &m, 0) == pdTRUE) psramFree(m.data);
}

void GeminiLiveClient::restart() {
    Serial.println("[Gemini] Restarting connection");
    _state = GeminiState::Connecting;
    if (!_started) {
        start();
        return;
    }
    _restartReq = true;
    xTaskNotifyGive(_io);
}

// ============================================================
// I/O task: the only code that touches the transport
// ============================================================

void GeminiLiveClient::ioEntry(void* self) { static_cast<GeminiLiveClient*>(self)->ioLoop(); }

void GeminiLiveClient::applyTransportConfig() {
    xSemaphoreTake(_cfgLock, portMAX_DELAY);
    esp_transport_ws_config_t c = {};
    c.ws_path = _path.c_str();
    c.user_agent = "esp32-ai-agent/3.1";
    c.headers = _headers.length() ? _headers.c_str() : nullptr;
    c.propagate_control_frames = true;  // we answer pings and read the close reason ourselves
    esp_transport_ws_set_config(_wst, &c);  // the transport keeps its own copies
    _appliedGen = _cfgGen;
    xSemaphoreGive(_cfgLock);
}

bool GeminiLiveClient::connectLink() {
    if (_appliedGen != _cfgGen) applyTransportConfig();
    Serial.printf("[Gemini] Connecting to wss://%s ...\n", GEMINI_HOST);
    if (esp_transport_connect(_wst, GEMINI_HOST, GEMINI_PORT, kConnectTimeoutMs) < 0) {
        Serial.printf("[Gemini] Connect failed (errno %d) - retry in %lu s\n", esp_transport_get_errno(_wst),
                      (unsigned long)(_backoffMs / 1000));
        esp_transport_close(_wst);
        pushMessage(MsgKind::Disconnected, nullptr, 0, 0);  // the agent counts it as a transient failure
        _nextAttempt = millis() + _backoffMs;
        _backoffMs = _backoffMs * 2 > kMaxBackoffMs ? kMaxBackoffMs : _backoffMs * 2;
        return false;
    }
    if (!_want || _restartReq) {  // stop()/restart() arrived while connecting
        closeLink(true, false);
        return false;
    }
    _linkUp = true;
    _setupOnLink = false;
    _rxLen = 0;
    _lastRx = _lastPing = millis();
    _backoffMs = 1000;
    pushMessage(MsgKind::Connected, nullptr, 0, 0);
    return true;
}

void GeminiLiveClient::dropOutbox() {
    OutboundMsg m;
    while (xQueueReceive(_outbox, &m, 0) == pdTRUE) psramFree(m.data);
}

// handshake: send a close frame and wait briefly for the server's, so the Live session ends now
// (a session left open on the server counts against the key's quota)
void GeminiLiveClient::closeLink(bool handshake, bool notify) {
    if (handshake) {
        char body[2] = {(char)0x03, (char)0xE8};  // 1000 normal closure (writable: masked in place)
        if (writeFrame(WS_TRANSPORT_OPCODES_CLOSE, body, 2, 1000)) {
            static char scratch[512];
            char ctrl[128];
            int ctrlLen = 0;
            bool fin = false;
            const uint32_t until = millis() + kCloseWaitMs;
            while ((int32_t)(until - millis()) > 0) {
                const int pr = esp_transport_poll_read(_ssl, 100);
                if (pr < 0) break;
                if (pr == 0) continue;
                const int op = readOneFrame(scratch, sizeof(scratch), fin, ctrl, ctrlLen, false);  // skip data
                if (op < 0 || op == WS_TRANSPORT_OPCODES_CLOSE) break;
            }
        }
    }
    esp_transport_close(_wst);
    _linkUp = false;
    _rxLen = 0;
    _streamEndPending = false;
    dropOutbox();
    if (notify) pushMessage(MsgKind::Disconnected, nullptr, 0, 0);
}

void GeminiLiveClient::dropLink(const char* why) {
    Serial.printf("[Gemini] %s - reconnecting in %lu s\n", why, (unsigned long)(_backoffMs / 1000));
    closeLink(false, true);
    _nextAttempt = millis() + _backoffMs;
    _backoffMs = _backoffMs * 2 > kMaxBackoffMs ? kMaxBackoffMs : _backoffMs * 2;
}

bool GeminiLiveClient::writeFrame(uint8_t opcode, char* data, int len, int timeoutMs) {
    const int w = esp_transport_ws_send_raw(_wst, (ws_transport_opcodes_t)(opcode | WS_TRANSPORT_OPCODES_FIN), data,
                                            len, timeoutMs);
    return w >= len;  // 0 = timed out: a half-written frame cannot be recovered
}

// Send what the other tasks queued. A frame that is not fully written breaks the stream, so any
// failure reconnects (from this task, so nothing else is using the TLS session meanwhile).
bool GeminiLiveClient::flushOutbox() {
    const uint32_t t0 = millis();
    OutboundMsg m;
    while (xQueueReceive(_outbox, &m, 0) == pdTRUE) {
        const bool isSetup = m.len > 9 && memcmp(m.data, "{\"setup\"", 8) == 0;
        if (!_setupOnLink && !isSetup) {  // left over from the previous link: the setup must go first
            psramFree(m.data);
            continue;
        }
        if (m.micGen != 0xFF && m.micGen != _micGen) {  // mic audio from before the reply started
            psramFree(m.data);
            _micDiscarded = _micDiscarded + 1;
            continue;
        }
        const uint32_t w0 = millis();
        const bool ok = writeFrame(m.opcode, m.data, (int)m.len, kWriteTimeoutMs);
        if (millis() - w0 > _slowWriteMs) _slowWriteMs = millis() - w0;  // diag: uplink stalls
        const uint32_t len = m.len;
        psramFree(m.data);
        if (!ok) {
            char why[64];
            snprintf(why, sizeof(why), "Send failed (%u bytes)", (unsigned)len);
            dropLink(why);
            return false;
        }
        if (isSetup) _setupOnLink = true;
        if (millis() - t0 > 100) return true;  // keep reading as well
    }
    if (_streamEndPending && _setupOnLink) {  // after the audio it ends
        static char msg[] = "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
        char frame[sizeof(msg)];
        memcpy(frame, msg, sizeof(msg));  // writeFrame masks in place
        _streamEndPending = false;
        if (!writeFrame(WS_TRANSPORT_OPCODES_TEXT, frame, (int)sizeof(msg) - 1, kWriteTimeoutMs)) {
            dropLink("Send failed (stream end)");
            return false;
        }
    }
    return true;
}

int GeminiLiveClient::readExact(void* dst, size_t n, int timeoutMs) {
    char* p = (char*)dst;
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < n) {
        const int left = timeoutMs - (int)(millis() - t0);
        if (left <= 0) return -1;  // a frame cut off mid-way cannot be resynchronised
        const int r = esp_transport_read(_ssl, p + got, (int)(n - got), left);
        if (r < 0) return -1;
        got += (size_t)r;  // TLS hands out at most one record at a time: loop until all n bytes
    }
    return (int)n;
}

// One whole frame (RFC 6455 5.2). Data payload is appended to _rx (or skipped), control payload
// (<= 125 bytes) goes to ctrl. Returns the opcode, or -1 when the link is broken.
int GeminiLiveClient::readOneFrame(char* buf, size_t cap, bool& fin, char* ctrl, int& ctrlLen, bool keepData) {
    uint8_t h[8];
    if (readExact(h, 2, kReadTimeoutMs) < 0) return -1;
    fin = (h[0] & 0x80) != 0;
    const int op = h[0] & 0x0F;
    const bool masked = (h[1] & 0x80) != 0;  // servers do not mask, but follow the header anyway
    uint64_t len = h[1] & 0x7F;
    if (len == 126) {
        if (readExact(h, 2, kReadTimeoutMs) < 0) return -1;
        len = ((uint64_t)h[0] << 8) | h[1];
    } else if (len == 127) {
        if (readExact(h, 8, kReadTimeoutMs) < 0) return -1;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | h[i];
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && readExact(mask, 4, kReadTimeoutMs) < 0) return -1;
    if ((op & 0x08) && len > 125) {  // a control frame this big means the stream is out of sync
        Serial.printf("[Gemini] bad frame header (op %d, %llu bytes)\n", op, (unsigned long long)len);
        return -1;
    }
    if (len > kMaxMessageBytes) {
        Serial.printf("[Gemini] frame too large (%llu bytes)\n", (unsigned long long)len);
        return -1;
    }
    const bool data = op == WS_TRANSPORT_OPCODES_TEXT || op == WS_TRANSPORT_OPCODES_BINARY ||
                      op == WS_TRANSPORT_OPCODES_CONT;
    if (data && op != WS_TRANSPORT_OPCODES_CONT) _rxLen = 0;  // a new message starts
    ctrlLen = 0;
    bool overflow = false;
    size_t done = 0;
    while (done < len) {
        const size_t n = (len - done) < cap ? (size_t)(len - done) : cap;
        if (readExact(buf, n, kReadTimeoutMs) < 0) return -1;
        if (masked) {
            for (size_t i = 0; i < n; i++) buf[i] ^= mask[(done + i) & 3];
        }
        if (!data) {
            memcpy(ctrl + ctrlLen, buf, n);
            ctrlLen += (int)n;
        } else if (keepData && !overflow && !appendRx(buf, n)) {
            overflow = true;  // keep reading the payload so the stream stays in sync
        }
        done += n;
    }
    if (overflow) {
        Serial.println("[Gemini] message too large, dropped");
        _rxLen = 0;
        _dropped = _dropped + 1;
    }
    return op;
}

int GeminiLiveClient::readFrame(char* buf, size_t cap) {
    bool fin = false;
    char control[128];
    int controlLen = 0;
    const int op = readOneFrame(buf, cap, fin, control, controlLen, true);
    if (op < 0) return -1;

    switch (op) {
        case WS_TRANSPORT_OPCODES_TEXT:
        case WS_TRANSPORT_OPCODES_BINARY:
        case WS_TRANSPORT_OPCODES_CONT:
            if (fin && _rxLen) {  // message complete (it may span several frames)
                if (jsonframe::isCompleteObject(_rx, _rxLen)) {
                    pushMessage(MsgKind::Data, _rx, _rxLen, 0);
                } else {
                    Serial.printf("[Gemini] incomplete JSON message (%u bytes) dropped\n", (unsigned)_rxLen);
                    _dropped = _dropped + 1;
                }
                _rxLen = 0;
            }
            return 1;
        case WS_TRANSPORT_OPCODES_PING:
            return writeFrame(WS_TRANSPORT_OPCODES_PONG, controlLen ? control : nullptr, controlLen, 2000) ? 1 : -1;
        case WS_TRANSPORT_OPCODES_CLOSE: {  // 2-byte status code + UTF-8 reason
            int code = 0;
            const char* reason = "";
            size_t rlen = 0;
            if (controlLen >= 2) {
                code = ((uint8_t)control[0] << 8) | (uint8_t)control[1];
                reason = control + 2;
                rlen = (size_t)controlLen - 2;
            }
            pushMessage(MsgKind::Closed, reason, rlen, code);
            char echo[2] = {control[0], control[1]};
            writeFrame(WS_TRANSPORT_OPCODES_CLOSE, controlLen >= 2 ? echo : nullptr, controlLen >= 2 ? 2 : 0, 1000);
            closeLink(false, true);
            _nextAttempt = millis() + _backoffMs;  // the agent decides (restart / pause / stop)
            return 0;
        }
        default:  // pong
            return 1;
    }
}

void GeminiLiveClient::ioLoop() {
    static char rbuf[4096];
    for (;;) {
        if (!_want || _restartReq) {
            if (_linkUp) closeLink(true, _restartReq);  // restart: the agent expects a Disconnected
            dropOutbox();
            if (!_want) {
                xSemaphoreGive(_closed);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // until start()
                continue;
            }
            _restartReq = false;
            _nextAttempt = millis();
            _backoffMs = 1000;
        }
        if (!_linkUp) {
            dropOutbox();
            if ((int32_t)(millis() - _nextAttempt) < 0) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
                continue;
            }
            connectLink();
            continue;
        }

        if (!flushOutbox()) continue;

        const int pr = esp_transport_poll_read(_ssl, 20);  // frames are read straight from TLS (readOneFrame)
        if (pr < 0) {
            char why[48];
            snprintf(why, sizeof(why), "Network error (errno %d)", esp_transport_get_errno(_wst));
            dropLink(why);
            continue;
        }
        if (pr > 0) {
            const uint32_t t0 = millis();
            const int r = readFrame(rbuf, sizeof(rbuf));
            const uint32_t took = millis() - t0;
            if (took > _slowReadMs) _slowReadMs = took;
            if (took > _turnReadMs) _turnReadMs = took;  // diag: a frame trickling in holds up the sends
            if (r < 0) {
                dropLink("Read error");
                continue;
            }
            _lastRx = millis();
            if (r == 0) continue;  // server closed
        }

        const uint32_t now = millis();
        if (now - _lastPing > kPingIntervalMs) {
            _lastPing = now;
            if (!writeFrame(WS_TRANSPORT_OPCODES_PING, nullptr, 0, 2000)) {
                dropLink("Ping failed");
                continue;
            }
        }
        if (now - _lastRx > kDeadLinkMs) dropLink("No data for 45 s");
    }
}

bool GeminiLiveClient::appendRx(const char* data, size_t len) {
    const size_t need = _rxLen + len + 1;
    if (need > kMaxMessageBytes) return false;
    if (need > _rxCap) {
        size_t cap = _rxCap ? _rxCap : 32768;
        while (cap < need) cap *= 2;
        char* p = (char*)psramRealloc(_rx, cap);
        if (!p) return false;
        _rx = p;
        _rxCap = cap;
    }
    memcpy(_rx + _rxLen, data, len);
    _rxLen += len;
    return true;
}

void GeminiLiveClient::pushMessage(MsgKind kind, const char* data, size_t len, int code) {
    InboundMsg m{kind, code, nullptr, len};
    if (data) {
        m.data = (char*)psramMalloc(len + 1);
        if (!m.data) {
            _dropped = _dropped + 1;
            return;
        }
        memcpy(m.data, data, len);
        m.data[len] = '\0';
    }
    if (xQueueSend(_inbox, &m, pdMS_TO_TICKS(100)) != pdTRUE) {
        psramFree(m.data);
        _dropped = _dropped + 1;
    }
}

// ============================================================
// Caller task side: protocol
// ============================================================

void GeminiLiveClient::poll(uint32_t budgetMs) {
    if (!_inbox) return;
    const uint32_t start = millis();
    // A server that accepts the socket but never answers the setup (seen while the key was
    // over quota) would otherwise hold the session until the link times out.
    if (_started && _state == GeminiState::SetupSent && start - _setupAt > kSetupTimeoutMs) {
        Serial.printf("[Gemini] No reply to setup in %u s - reconnecting\n", (unsigned)(kSetupTimeoutMs / 1000));
        restart();  // the Disconnected event that follows counts as a transient failure
    }
    InboundMsg m;
    while (xQueueReceive(_inbox, &m, 0) == pdTRUE) {
        switch (m.kind) {
            case MsgKind::Connected:
                if (!_started) break;
                Serial.println("[Gemini] Connected, sending setup");
                sendSetup();
                break;
            case MsgKind::Disconnected: {
                const bool wasReady = _state == GeminiState::Ready;
                _state = GeminiState::Connecting;  // the I/O task reconnects by itself
                Serial.println("[Gemini] Disconnected");
                if (_listener) _listener->onDisconnected(wasReady);
                break;
            }
            case MsgKind::Closed:
                Serial.printf("[Gemini] Server closed (%d): %s\n", m.code, m.data ? m.data : "");
                if (_listener) _listener->onServerClose(m.code, m.data ? m.data : "");
                break;
            case MsgKind::Data:
                handleMessage(m.data, m.len);
                break;
        }
        psramFree(m.data);
        if (millis() - start > budgetMs) break;
    }
}

bool GeminiLiveClient::ensure(void*& buf, size_t& cap, size_t need) {
    if (need <= cap) return true;
    void* p = psramRealloc(buf, need);
    if (!p) return false;
    buf = p;
    cap = need;
    return true;
}

// Copy into the outbox; the I/O task writes it. Audio does not wait (a full queue = congested link,
// dropping 40 ms beats blocking the mic); JSON messages wait a little.
bool GeminiLiveClient::enqueue(const char* data, size_t len, uint32_t waitMs, bool mic) {
    if (!_linkUp || !data || !len) return false;
    OutboundMsg m{(char*)psramMalloc(len), (uint32_t)len, (uint8_t)WS_TRANSPORT_OPCODES_TEXT,
                  mic ? (uint8_t)_micGen : (uint8_t)0xFF};
    if (!m.data) return false;
    memcpy(m.data, data, len);
    if (xQueueSend(_outbox, &m, pdMS_TO_TICKS(waitMs)) != pdTRUE) {
        psramFree(m.data);
        _dropped = _dropped + 1;
        return false;
    }
    return true;
}

bool GeminiLiveClient::sendJson(const JsonDocument& doc) {
    const size_t need = measureJson(doc) + 1;
    void* buf = _tx;
    if (!ensure(buf, _txCap, need)) return false;
    _tx = (char*)buf;
    const size_t n = serializeJson(doc, _tx, _txCap);
    return enqueue(_tx, n, 1000);
}

void GeminiLiveClient::sendSetup() {
    JsonDocument doc(JsonPsramAllocator::instance());
    JsonObject setup = doc["setup"].to<JsonObject>();
    if (_listener) _listener->buildSetup(setup);
    const size_t size = measureJson(doc);
    if (sendJson(doc)) {
        _state = GeminiState::SetupSent;
        _setupAt = millis();
        Serial.printf("[Gemini] Setup sent (%u bytes)\n", (unsigned)size);
    } else {
        Serial.println("[Gemini] ERROR: failed to queue setup");
    }
}

bool GeminiLiveClient::sendAudio(const int16_t* pcm, size_t samples, uint32_t sampleRate) {
    if (_state != GeminiState::Ready || !pcm || samples == 0) return false;
    const size_t bytes = samples * 2;
    const size_t need = b64::encodedLength(bytes) + 128;
    void* buf = _tx;
    if (!ensure(buf, _txCap, need)) return false;
    _tx = (char*)buf;

    // Built by hand: Base64 needs no JSON escaping and this runs 25x per second
    int n = snprintf(_tx, _txCap,
                     "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=%lu\",\"data\":\"",
                     (unsigned long)sampleRate);
    n += (int)b64::encode((const uint8_t*)pcm, bytes, _tx + n);
    n += snprintf(_tx + n, _txCap - n, "\"}}}");
    return enqueue(_tx, (size_t)n, 0, true);
}

bool GeminiLiveClient::sendClientText(const char* text, bool turnComplete) {
    if (_state != GeminiState::Ready || !text) return false;
    JsonDocument doc(JsonPsramAllocator::instance());
    JsonObject cc = doc["clientContent"].to<JsonObject>();
    JsonObject turn = cc["turns"].to<JsonArray>().add<JsonObject>();
    turn["role"] = "user";
    turn["parts"].to<JsonArray>().add<JsonObject>()["text"] = text;
    cc["turnComplete"] = turnComplete;
    return sendJson(doc);
}

bool GeminiLiveClient::sendAudioStreamEnd() {
    if (_state != GeminiState::Ready) return false;
    static const char kMsg[] = "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
    if (enqueue(kMsg, sizeof(kMsg) - 1, 0)) return true;
    _streamEndPending = true;  // queue full (slow uplink): the I/O task sends it once the queue drains
    return true;               // - never hold the agent loop (it also delivers the reply's audio)
}

void GeminiLiveClient::handleMessage(const char* json, size_t len) {
    JsonDocument doc(JsonPsramAllocator::instance());
    const DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        Serial.printf("[Gemini] JSON parse error: %s (%u bytes)\n", err.c_str(), (unsigned)len);
        return;
    }
    if (!_listener) return;

    if (!doc["setupComplete"].isNull()) {
        _state = GeminiState::Ready;
        Serial.println("[Gemini] Setup complete - session ready");
        _listener->onSetupComplete();
    }

    // A single message may carry audio, transcripts and turn flags together
    JsonObjectConst sc = doc["serverContent"];
    if (!sc.isNull()) {
        for (JsonObjectConst part : sc["modelTurn"]["parts"].as<JsonArrayConst>()) {
            const char* b64 = part["inlineData"]["data"];
            if (!b64) continue;
            const size_t blen = strlen(b64);
            void* buf = _pcm;
            if (!ensure(buf, _pcmCap, b64::decodedMaxLength(blen))) continue;
            _pcm = (uint8_t*)buf;
            const size_t n = b64::decode(b64, blen, _pcm);
            if (n == b64::kError) {
                Serial.println("[Gemini] bad Base64 audio");
                continue;
            }
            _audioBytes += n;
            _listener->onAudio((const int16_t*)_pcm, n / 2);
        }
        const char* in = sc["inputTranscription"]["text"];
        if (in && *in) _listener->onInputTranscript(in);
        const char* out = sc["outputTranscription"]["text"];
        if (out && *out) _listener->onOutputTranscript(out);
        if (sc["interrupted"] | false) _listener->onInterrupted();
        if (sc["generationComplete"] | false) _listener->onGenerationComplete();
        if (sc["turnComplete"] | false) _listener->onTurnComplete();
    }

    JsonObjectConst toolCall = doc["toolCall"];
    if (!toolCall.isNull()) _listener->onToolCall(toolCall["functionCalls"]);

    JsonObjectConst cancel = doc["toolCallCancellation"];
    if (!cancel.isNull()) _listener->onToolCallCancellation(cancel["ids"]);

    JsonObjectConst resume = doc["sessionResumptionUpdate"];
    if (!resume.isNull()) _listener->onSessionHandle(resume["newHandle"] | "", resume["resumable"] | false);

    JsonObjectConst goAway = doc["goAway"];
    if (!goAway.isNull()) _listener->onGoAway(parseDurationMs(goAway["timeLeft"] | ""));

    const uint32_t tokens = doc["usageMetadata"]["totalTokenCount"] | 0u;
    if (tokens) _listener->onUsage(tokens);
}
