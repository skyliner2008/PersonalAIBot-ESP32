/**
 * @file GzipHttpStream.h
 * @brief HTTPS GET whose body is read as a Stream: chunked transfer + gzip undone on the fly (T100)
 *
 * ThaiWater answers in gzip when asked (nationwide 24 h rain: 4.8 MB of JSON = 608 KB on the wire). Downloading
 * 4.8 MB at the ~130 KB/s this board reads TLS would take ~40 s and could not even sit in PSRAM; streamed through
 * the ROM's inflate (tinfl, 32 KB window in PSRAM) straight into ArduinoJson (with a Filter) it is ~5-8 s and the
 * raw text never exists in memory. Servers that ignore Accept-Encoding work too (plain body passed through).
 * Only used in the webTools task.
 */

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <miniz.h>

#include "../ai/GoogleRootCA.h"
#include "../util/PsramAlloc.h"
#include "WebTools.h"  // setStage

namespace webtools {

class GzipHttpStream : public Stream {
public:
    ~GzipHttpStream() override { close(); }

    /**
     * GET `url` (https only); returns the HTTP status (200 = the body can be read), < 0 = no connection.
     * `stallMs`: no byte for this long (headers or body) = given up (stalled()) - the board saw downloads stop dead
     * mid-way at random and sit until the overall timeout, while a new connection went through (T104).
     */
    int open(const String& url, uint32_t timeoutMs, uint32_t stallMs = 6000) {
        if (WiFi.status() != WL_CONNECTED) return -10;
        if (!url.startsWith("https://")) return -3;
        _deadline = millis() + timeoutMs;
        _stallMs = stallMs;
        _tls.setCACert(WEB_ROOT_CA_PEM);
        _tls.setHandshakeTimeout(10);
        _http.setTimeout(stallMs + 2000);  // waiting for the headers (HTTPClient reads them itself)
        _http.setConnectTimeout(timeoutMs);
        _http.setUserAgent("Mozilla/5.0 (ESP32-AI personal assistant)");
        _http.setAcceptEncoding("gzip");
        static const char* kHeaders[] = {"Content-Encoding", "Transfer-Encoding"};
        _http.collectHeaders(kHeaders, 2);
        if (!_http.begin(_tls, url)) return -2;
        // connected here (DNS + TCP + TLS) rather than inside GET(): the stage shows which half got stuck (T105);
        // HTTPClient then reuses the open connection
        const int hostEnd = url.indexOf('/', 8);
        const String host = url.substring(8, hostEnd < 0 ? url.length() : hostEnd);
        setStage("connect (DNS + TLS)");
        if (!_tls.connect(host.c_str(), 443, (int32_t)timeoutMs)) return -1;
        setStage("request + headers");
        const int code = _http.GET();
        setStage("body");
        if (code != 200) return code;
        _s = _http.getStreamPtr();
        if (!_s) return -1;
        _lastData = millis();
        _chunked = _http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
        _left = _chunked ? -1 : _http.getSize();  // -1 = until the connection closes
        _gzip = _http.header("Content-Encoding").equalsIgnoreCase("gzip");
        if (_gzip) {
            _inf = (tinfl_decompressor*)psramMalloc(sizeof(tinfl_decompressor));
            _dict = (uint8_t*)psramMalloc(TINFL_LZ_DICT_SIZE);
            if (!_inf || !_dict) return -4;
            tinfl_init(_inf);
        }
        return code;
    }

    void close() {
        setStage("close");
        _http.end();
        psramFree(_inf);
        psramFree(_dict);
        _inf = nullptr;
        _dict = nullptr;
        _s = nullptr;
    }

    size_t wireBytes() const { return _wire; }  // as received (compressed)
    size_t bodyBytes() const { return _body; }  // as parsed (plain JSON)
    bool timedOut() const { return _timedOut; }
    bool stalled() const { return _stalled; }  // nothing arrived for stallMs: a new connection may get through
    uint32_t netMs() const { return _netUs / 1000; }   // waiting for / reading the connection
    uint32_t inflateMs() const { return _infUs / 1000; }  // gzip

    /**
     * Next byte without the Stream machinery (-1 = end). Stream::readBytes() costs a virtual call and a clock
     * read per byte: ~12 of the 21 s for the 4.8 MB nationwide rain (board test) - see Reader.
     */
    inline int next() {
        if (!_outLen && !fill()) return -1;
        _outLen--;
        _body++;
        return *_out++;
    }

    /** ArduinoJson custom reader (read / readBytes, not a Stream): deserializeJson(doc, stream.reader(), ...). */
    struct Reader {
        GzipHttpStream* s;
        int read() { return s->next(); }
        size_t readBytes(char* buf, size_t n) {
            size_t i = 0;
            for (int c; i < n && (c = s->next()) >= 0; i++) buf[i] = (char)c;
            return i;
        }
    };
    Reader reader() { return Reader{this}; }

    // ---- Stream (find / findUntil between the elements) ----
    int available() override { return (_outLen || fill()) ? (int)_outLen : 0; }
    int read() override { return next(); }
    int peek() override { return (_outLen || fill()) ? *_out : -1; }
    size_t write(uint8_t) override { return 0; }

private:
    WiFiClientSecure _tls;
    HTTPClient _http;
    NetworkClient* _s = nullptr;
    uint32_t _deadline = 0;
    uint32_t _stallMs = 6000, _lastData = 0;
    bool _chunked = false, _gzip = false, _eof = false, _timedOut = false, _stalled = false;
    long _left = -1;       // plain body: bytes still to come (-1 = until close)
    long _chunkLeft = 0;   // chunked: bytes left in the current chunk
    size_t _wire = 0, _body = 0;
    uint32_t _netUs = 0, _infUs = 0;
    // gzip
    tinfl_decompressor* _inf = nullptr;
    uint8_t* _dict = nullptr;
    size_t _dictPos = 0;
    bool _header = false, _infDone = false;
    uint8_t _in[2048];
    size_t _inPos = 0, _inLen = 0;
    // what read() hands out next
    const uint8_t* _out = nullptr;
    size_t _outLen = 0;
    uint8_t _plain[1024];

    /** One byte from the connection, waiting up to the deadline. -1 = closed / timed out. */
    int rawByte() {
        for (;;) {
            if (!_s) return -1;
            if (_s->available() > 0) {
                // read(buf, 1), not read(): when available() says yes but nothing can be read (closed / TLS error)
                // read() hands out 255 without consuming anything - the chunk-size loop waited for its '\n' for ever
                // at job priority 4 and froze the console (board, T105 "stuck in water: parse stations")
                uint8_t b;
                if (_s->read(&b, 1) == 1) {
                    _lastData = millis();
                    return b;
                }
            }
            if (!_s->connected()) return -1;
            if ((int32_t)(millis() - _deadline) > 0) {
                _timedOut = true;
                return -1;
            }
            if (millis() - _lastData > _stallMs) {
                _timedOut = _stalled = true;
                return -1;
            }
            delay(1);
        }
    }

    /** Body bytes with the chunked framing removed: up to `n` into `buf`, 0 = end. */
    size_t bodyRead(uint8_t* buf, size_t n) {
        if (_eof) return 0;
        const uint32_t t0 = micros();
        struct Add {
            uint32_t& acc;
            uint32_t t0;
            ~Add() { acc += micros() - t0; }
        } add{_netUs, t0};
        while (_chunked && _chunkLeft == 0) {  // "<hex size>[;ext]\r\n" (after the previous chunk's "\r\n")
            char line[20];
            size_t len = 0;
            int c;
            while ((c = rawByte()) >= 0 && c != '\n') {
                if (len < sizeof(line) - 1 && c != '\r') line[len++] = (char)c;
            }
            if (c < 0) return (_eof = true), 0;
            line[len] = '\0';
            if (!len) continue;  // the CRLF that ends the previous chunk (a loop: one timer for the whole read)
            _chunkLeft = strtol(line, nullptr, 16);
            if (_chunkLeft <= 0) return (_eof = true), 0;  // the last chunk
        }
        long want = (long)n;
        if (_chunked && want > _chunkLeft) want = _chunkLeft;
        if (!_chunked && _left >= 0 && want > _left) want = _left;
        if (want <= 0) return (_eof = true), 0;
        size_t got = 0;
        while (got < (size_t)want) {
            const int avail = _s ? _s->available() : 0;
            if (avail > 0) {
                const size_t take = (size_t)avail < (size_t)want - got ? (size_t)avail : (size_t)want - got;
                const int r = _s->read(buf + got, take);
                if (r > 0) {
                    got += (size_t)r;
                    _lastData = millis();
                    continue;
                }
                // available() said yes but read() gave 0 (closed) / < 0 (TLS error): added to `got` as before, that
                // spun here for ever at job priority 4 - the console (priority 1) froze (board, T104) - or wrapped
                // `got` past the buffer. Fall through: rawByte() notices the closed / dead connection.
            }
            if (got) break;  // hand out what is here
            const int c = rawByte();
            if (c < 0) return (_eof = true), 0;
            buf[got++] = (uint8_t)c;
        }
        _wire += got;
        if (_chunked) _chunkLeft -= (long)got;
        else if (_left >= 0) _left -= (long)got;
        return got;
    }

    bool refillIn() {
        if (_inPos < _inLen) return true;
        _inLen = bodyRead(_in, sizeof(_in));
        _inPos = 0;
        return _inLen > 0;
    }

    /** gzip header (RFC 1952): magic, method, flags, mtime, xfl, os + optional extra / name / comment / crc. */
    bool skipGzipHeader() {
        auto byte = [&]() -> int { return refillIn() ? _in[_inPos++] : -1; };
        if (byte() != 0x1f || byte() != 0x8b || byte() != 8) return false;
        const int flags = byte();
        for (int i = 0; i < 6; i++) byte();
        if (flags & 4) {  // FEXTRA
            const int lo = byte(), hi = byte();
            for (int i = 0; i < lo + (hi << 8); i++) byte();
        }
        if (flags & 8) while (byte() > 0) {}   // FNAME
        if (flags & 16) while (byte() > 0) {}  // FCOMMENT
        if (flags & 2) {                        // FHCRC
            byte();
            byte();
        }
        return true;
    }

    /** Make the next bytes available in _out / _outLen. false = end of the body (or an error). */
    bool fill() {
        if (!_s) return false;
        if (!_gzip) {
            _outLen = bodyRead(_plain, sizeof(_plain));
            _out = _plain;
            return _outLen > 0;
        }
        if (!_header) {
            _header = true;
            if (!skipGzipHeader()) {
                _infDone = true;
                return false;
            }
        }
        while (!_infDone) {
            const bool more = refillIn();
            size_t inSize = _inLen - _inPos;
            size_t outSize = TINFL_LZ_DICT_SIZE - _dictPos;
            const uint32_t t0 = micros();
            const tinfl_status st = tinfl_decompress(_inf, _in + _inPos, &inSize, _dict, _dict + _dictPos, &outSize,
                                                     more ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            _infUs += micros() - t0;
            _inPos += inSize;
            if (st < TINFL_STATUS_DONE || (st == TINFL_STATUS_NEEDS_MORE_INPUT && !more)) _infDone = true;
            if (st == TINFL_STATUS_DONE) _infDone = true;  // the 8-byte crc/size trailer is not needed
            if (outSize) {
                _out = _dict + _dictPos;
                _outLen = outSize;
                _dictPos = (_dictPos + outSize) & (TINFL_LZ_DICT_SIZE - 1);
                return true;
            }
            if (!more && inSize == 0) _infDone = true;
        }
        return false;
    }
};

}  // namespace webtools
