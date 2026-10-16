/**
 * @file RadioPlayer.cpp
 * @brief Internet radio stream task (see RadioPlayer.h)
 */

#include "RadioPlayer.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <aacdec.h>
#include <esp_heap_caps.h>
#include <mp3dec.h>

#include "../system/PsramTask.h"
#include "../util/RadioResampler.h"
#include "../util/Utf8.h"
#include "AudioManager.h"

RadioPlayer gRadio;

namespace {

constexpr size_t kInBuf = 16 * 1024;     // compressed bytes waiting for the decoder
constexpr size_t kPcmMax = 4096;         // HE-AAC: 2048 samples x 2 channels per frame
constexpr size_t kOutMax = 4608;         // resampled mono samples per frame (upsampling worst case)
// no stream data this long -> reconnect. The speaker holds ~4 s: 12 s left 8 s of silence. Seen on a phone
// hotspot (IDF 5): one TCP window (5760 bytes) arrived, then nothing on an open socket until the timeout.
constexpr uint32_t kStallMs = 5000;
// ...and it happened only right after connecting (one Icecast host, 3 times in 20 connections): before the
// first 16 KB a 2 s pause already means a stuck start - nothing is buffered to cover it
constexpr uint32_t kStartStallMs = 2000;
constexpr uint32_t kStartBytes = 16 * 1024;
constexpr int kMaxRetries = 4;

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

void copyStr(char* dst, size_t cap, const char* src, size_t len = (size_t)-1) {
    if (!src) src = "";
    size_t n = strnlen(src, len == (size_t)-1 ? cap - 1 : len);
    if (n > cap - 1) n = cap - 1;
    n = utf8::clampToBoundary(src, n, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool validUtf8(const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n;) {
        const uint8_t c = s[i];
        size_t extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 9;
        if (extra == 9 || i + extra >= n + (extra ? 0 : 1)) return false;
        for (size_t k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

/** Thai stations often send TIS-620 titles: 0xA1-0xFB -> U+0E01-U+0E5B. */
void tis620ToUtf8(const uint8_t* s, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < cap; i++) {
        const uint8_t c = s[i];
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c >= 0xA1 && c <= 0xFB) {
            const uint16_t cp = 0x0E00 + (c - 0xA0);
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = '\0';
}

}  // namespace

bool RadioPlayer::begin(AudioManager* audio) {
    _audio = audio;
    _lock = xSemaphoreCreateMutex();
    return _lock && startPsramTask(taskEntry, "radio", 16384, this, 4, &_task, 0);
}

bool RadioPlayer::play(const char* url, const char* name) {
    if (!_task || !url || strncmp(url, "http", 4) != 0) return false;
    {
        Guard g(_lock);
        copyStr(_url, sizeof(_url), url);
        copyStr(_name, sizeof(_name), name && *name ? name : "Internet radio");
        _title[0] = '\0';
        _error[0] = '\0';
    }
    _gen = _gen + 1;
    _want = true;
    _state = State::Connecting;
    if (_audio) _audio->clearRadio();
    xTaskNotifyGive(_task);
    return true;
}

void RadioPlayer::stop() {
    _want = false;
    _gen = _gen + 1;
    _state = State::Idle;
    if (_audio) _audio->clearRadio();
}

const char* RadioPlayer::stateName() const {
    switch (_state) {
        case State::Connecting: return "connecting";
        case State::Playing: return "playing";
        case State::Error: return "error";
        default: return "idle";
    }
}

void RadioPlayer::info(char* name, size_t nameCap, char* title, size_t titleCap) const {
    Guard g(_lock);
    if (name) copyStr(name, nameCap, _name);
    if (title) copyStr(title, titleCap, _title);
}

void RadioPlayer::setError(const char* e) {
    Guard g(_lock);
    copyStr(_error, sizeof(_error), e);
}

void RadioPlayer::setTitle(const char* raw, size_t len) {
    char buf[sizeof(_title)];
    if (validUtf8((const uint8_t*)raw, len)) copyStr(buf, sizeof(buf), raw, len);
    else tis620ToUtf8((const uint8_t*)raw, len, buf, sizeof(buf));
    Guard g(_lock);
    if (strcmp(buf, _title) != 0) {
        copyStr(_title, sizeof(_title), buf);
        Serial.printf("[Radio] Now playing: %s\n", _title);
    }
}

void RadioPlayer::taskEntry(void* self) { static_cast<RadioPlayer*>(self)->loop(); }

void RadioPlayer::loop() {
    int retries = 0;
    for (;;) {
        if (!_want) {
            retries = 0;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        const uint32_t gen = _gen;
        const uint32_t t0 = millis();
        const bool played = session(gen);
        const bool ranLong = millis() - t0 > 10000;
        if (!_want || gen != _gen) {  // stopped or switched station: start over at once
            retries = 0;
            continue;
        }
        retries = played ? 1 : retries + 1;  // a stream that played resets the count
        if (retries > kMaxRetries) {
            Serial.printf("[Radio] Giving up: %s\n", _error);
            _want = false;
            _state = State::Error;
            continue;
        }
        _state = State::Connecting;
        // a stream that played a while and broke: back at once, the speaker still plays what is buffered
        // (one that keeps dropping right after starting waits as before - no hammering the server)
        const uint32_t waitMs = played && ranLong ? 300 : (uint32_t)retries * 2000;
        Serial.printf("[Radio] Reconnecting (%d/%d) in %lu ms\n", retries, kMaxRetries, (unsigned long)waitMs);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));  // play()/stop() cut the wait short
    }
}

// One connection. Returns true if any audio was played (for the retry policy).
bool RadioPlayer::session(uint32_t gen) {
    if (WiFi.status() != WL_CONNECTED) {
        setError("no Wi-Fi");
        return false;
    }
    char url[sizeof(_url)];
    {
        Guard g(_lock);
        copyStr(url, sizeof(url), _url);
    }

    WiFiClientSecure tls;
    WiFiClient plain;
    tls.setInsecure();  // public audio, no credentials: radio hosts use all kinds of CAs
    tls.setTimeout(10);
    plain.setTimeout(10);
    HTTPClient http;
    http.setTimeout(10000);
    http.setUserAgent("Mozilla/5.0 (ESP32-AI radio)");
    static const char* kHeaders[] = {"icy-metaint", "content-type", "location", "icy-name"};
    int code = 0;
    for (int hop = 0; hop < 4; hop++) {  // manual redirects: http <-> https changes the client
        const bool https = !strncmp(url, "https://", 8);
        if (!(https ? http.begin(tls, url) : http.begin(plain, url))) {
            setError("bad URL");
            return false;
        }
        http.collectHeaders(kHeaders, 4);
        http.addHeader("Icy-MetaData", "1");
        code = http.GET();
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.header("location");
            http.end();
            if (loc.isEmpty()) break;
            copyStr(url, sizeof(url), loc.c_str());
            continue;
        }
        break;
    }
    if (code != 200) {
        char e[48];
        snprintf(e, sizeof(e), "HTTP %d", code);
        setError(e);
        Serial.printf("[Radio] Stream refused: %s\n", e);
        http.end();
        return false;
    }

    String ctype = http.header("content-type");
    ctype.toLowerCase();
    const int metaint = http.header("icy-metaint").toInt();
    bool aac = ctype.indexOf("aac") >= 0 || ctype.indexOf("mp4a") >= 0;
    const bool mp3 = ctype.indexOf("mpeg") >= 0 || ctype.indexOf("mp3") >= 0;
    if (!aac && !mp3) {
        String u = url;
        u.toLowerCase();
        if (u.indexOf(".aac") >= 0) aac = true;
        else if (u.indexOf(".mp3") < 0 && ctype.length() && ctype.indexOf("octet") < 0) {
            char e[64];
            snprintf(e, sizeof(e), "unsupported stream (%s)", ctype.c_str());
            setError(e);
            Serial.printf("[Radio] %s\n", e);
            http.end();
            return false;
        }
    }
    _codec = aac ? "AAC" : "MP3";
    WiFiClient* stream = http.getStreamPtr();

    uint8_t* in = (uint8_t*)heap_caps_malloc(kInBuf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t* pcm = (int16_t*)heap_caps_malloc(kPcmMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t* out = (int16_t*)heap_caps_malloc(kOutMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    HMP3Decoder mp3dec = nullptr;
    HAACDecoder aacdec = nullptr;
    if (aac) aacdec = AACInitDecoder();
    else mp3dec = MP3InitDecoder();
    if (!in || !pcm || !out || (!mp3dec && !aacdec)) {
        setError("out of memory");
        if (mp3dec) MP3FreeDecoder(mp3dec);
        if (aacdec) AACFreeDecoder(aacdec);
        heap_caps_free(in);
        heap_caps_free(pcm);
        heap_caps_free(out);
        http.end();
        return false;
    }
    Serial.printf("[Radio] Connected: %s, %s%s\n", url, _codec, metaint ? ", ICY titles" : "");

    RadioResampler rs;
    uint32_t rate = 0;
    int lockRate = 0, lockCh = 0, mismatch = 0;  // format of the stream once established
    size_t fill = 0;           // bytes in `in`
    int untilMeta = metaint;   // audio bytes before the next ICY metadata block
    const uint32_t connectedAt = millis();
    uint32_t lastData = connectedAt;
    uint32_t totalBytes = 0;   // stream bytes read (a stall report says when the data stopped)
    bool played = false;
    char meta[256 * 16 + 1];

    // ---- HTTP body reader: Transfer-Encoding: chunked is common on CDN-hosted streams ----
    String te = http.header("transfer-encoding");
    te.toLowerCase();
    const bool chunked = te.indexOf("chunked") >= 0;
    long chunkLeft = 0;
    bool bodyEnded = false;
    auto rawByte = [&](uint32_t ms) -> int {  // one byte, waiting up to `ms`
        const uint32_t t0 = millis();
        for (;;) {
            const int c = stream->read();
            if (c >= 0) return c;
            if (!stream->connected() || millis() - t0 > ms || !_want || gen != _gen) return -1;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    };
    // up to n payload bytes that are already here (0 = none yet, -1 = stream over)
    auto body = [&](uint8_t* d, int n) -> int {
        if (!chunked) {
            const int r = stream->read(d, n);
            return r < 0 ? 0 : r;
        }
        if (chunkLeft == 0) {  // "<hex size>\r\n" (the CRLF after the previous chunk is an empty line)
            if (stream->available() <= 0) return 0;
            char line[20];
            int len = 0;
            for (int guard = 0; guard < 4; guard++) {
                len = 0;
                for (;;) {
                    const int c = rawByte(3000);
                    if (c < 0) return -1;
                    if (c == '\n') break;
                    if (c != '\r' && len < (int)sizeof(line) - 1) line[len++] = (char)c;
                }
                if (len) break;
            }
            line[len] = '\0';
            chunkLeft = strtol(line, nullptr, 16);
            if (chunkLeft <= 0) {
                bodyEnded = true;
                return -1;
            }
        }
        const int r = stream->read(d, n < chunkLeft ? n : (int)chunkLeft);
        if (r <= 0) return 0;
        chunkLeft -= r;
        return r;
    };
    auto bodyFull = [&](uint8_t* d, int n, uint32_t ms) -> int {  // exactly n bytes (metadata)
        int got = 0;
        const uint32_t t0 = millis();
        while (got < n && millis() - t0 < ms && _want && gen == _gen) {
            const int r = body(d + got, n - got);
            if (r < 0) break;
            if (r == 0) vTaskDelay(pdMS_TO_TICKS(5));
            got += r > 0 ? r : 0;
        }
        return got;
    };

    while (_want && gen == _gen) {
        // ---- network -> input buffer (chunk framing and ICY metadata stripped out) ----
        int avail = stream->available();
        if (avail > 0 && fill < kInBuf) {
            int want = (int)(kInBuf - fill);
            if (want > avail) want = avail;
            if (metaint && want > untilMeta) want = untilMeta;
            const int got = body(in + fill, want);
            if (got < 0) {
                setError(bodyEnded ? "stream ended" : "stream broken");
                break;
            }
            if (got > 0) {
                fill += got;
                totalBytes += (uint32_t)got;
                lastData = millis();
                if (metaint) {
                    untilMeta -= got;
                    if (untilMeta == 0) {  // 1 length byte (x16) + StreamTitle='...';
                        uint8_t lb = 0;
                        const int len = bodyFull(&lb, 1, 3000) == 1 ? lb * 16 : 0;
                        const int m = len ? bodyFull((uint8_t*)meta, len, 3000) : 0;
                        meta[m > 0 ? m : 0] = '\0';
                        const char* s = strstr(meta, "StreamTitle='");
                        if (s) {
                            s += 13;
                            const char* e = strstr(s, "';");
                            if (!e) e = s + strlen(s);
                            setTitle(s, (size_t)(e - s));
                        }
                        untilMeta = metaint;
                    }
                }
            }
        } else if (avail <= 0) {
            if (!stream->connected() && avail <= 0 && fill < 2048) {
                setError("stream ended");
                break;
            }
            const bool stuckStart = totalBytes > 0 && totalBytes < kStartBytes;  // (a slow first byte is not)
            if (millis() - lastData > (stuckStart ? kStartStallMs : kStallMs)) {
                setError("stream stalled");
                Serial.printf("[Radio] Stalled: %lu bytes, data stopped %lu ms after connecting, %s, %u bytes undecoded, "
                              "%u ms buffered\n",
                              (unsigned long)totalBytes, (unsigned long)(lastData - connectedAt),
                              stream->connected() ? "socket open" : "socket closed", (unsigned)fill,
                              (unsigned)(_audio->radioBuffered() / 24));
                break;
            }
        }

        // ---- decode as many frames as the buffer holds ----
        bool progressed = false;
        while (fill > 0 && _want && gen == _gen) {
            const int sync = aac ? AACFindSyncWord(in, (int)fill) : MP3FindSyncWord(in, (int)fill);
            if (sync < 0) {  // keep the last bytes: a sync word may straddle the boundary
                if (fill > 4) {
                    memmove(in, in + fill - 4, 4);
                    fill = 4;
                }
                break;
            }
            if (sync > 0) {
                memmove(in, in + sync, fill - sync);
                fill -= sync;
            }
            unsigned char* ptr = in;
            int left = (int)fill;
            int err;
            int nChans, sr, samps;
            if (aac) {
                err = AACDecode(aacdec, &ptr, &left, pcm);
                AACFrameInfo fi;
                AACGetLastFrameInfo(aacdec, &fi);
                nChans = fi.nChans, sr = fi.sampRateOut, samps = fi.outputSamps;
            } else {
                err = MP3Decode(mp3dec, &ptr, &left, pcm, 0);
                MP3FrameInfo fi;
                MP3GetLastFrameInfo(mp3dec, &fi);
                nChans = fi.nChans, sr = fi.samprate, samps = fi.outputSamps;
            }
            if (err == ERR_AAC_INDATA_UNDERFLOW || err == ERR_MP3_INDATA_UNDERFLOW) break;  // need more bytes
            if (err != 0 && !(err == ERR_MP3_MAINDATA_UNDERFLOW && !aac)) {
                // corrupt frame: skip a byte and look for the next sync word
                memmove(in, in + 1, fill - 1);
                fill -= 1;
                continue;
            }
            const size_t used = fill - (size_t)left;
            memmove(in, in + used, (size_t)left);
            fill = (size_t)left;
            progressed = true;
            if (err != 0 || samps <= 0 || nChans < 1 || nChans > 2) continue;

            // a false sync in damaged data decodes as a "frame" with another rate/channel count:
            // keep the established format unless the new one persists
            if (!lockRate) {
                lockRate = sr;
                lockCh = nChans;
            } else if (sr != lockRate || nChans != lockCh) {
                if (++mismatch < 8) continue;
                lockRate = sr;
                lockCh = nChans;
            }
            mismatch = 0;

            if ((uint32_t)sr != rate) {
                rate = (uint32_t)sr;
                _sampleRate = rate;
                rs.reset(rate);
                Serial.printf("[Radio] %s %lu Hz, %d ch\n", _codec, (unsigned long)rate, nChans);
            }
            const size_t n = rs.process(pcm, (size_t)samps / nChans, nChans, out, kOutMax);
            // pace to real time: wait for room in the speaker's radio buffer
            while (_audio->radioFree() < n && _want && gen == _gen) vTaskDelay(pdMS_TO_TICKS(15));
            if (!_want || gen != _gen) break;
            _audio->writeRadio(out, n);
            if (!played) {
                played = true;
                _state = State::Playing;
            }
            // while the buffer fills faster than real time, decoding never waits: give the core
            // back every frame (the idle task must run, or the task watchdog resets the board)
            vTaskDelay(1);
        }
        if (!progressed && stream->available() <= 0) vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (mp3dec) MP3FreeDecoder(mp3dec);
    if (aacdec) AACFreeDecoder(aacdec);
    heap_caps_free(in);
    heap_caps_free(pcm);
    heap_caps_free(out);
    http.end();
    return played;
}
