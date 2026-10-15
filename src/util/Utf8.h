/**
 * @file Utf8.h
 * @brief Minimal UTF-8 helpers (decode / encode / boundary-safe truncation)
 *
 * Portable (no Arduino dependency) so it can be unit-tested on the host.
 * Used by the memory index (Thai bigram features) and the subtitle layout.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace utf8 {

constexpr uint32_t kReplacement = 0xFFFD;

inline bool isContinuation(uint8_t b) { return (b & 0xC0) == 0x80; }

/**
 * Decode the code point starting at s[i] and advance i.
 * Invalid or truncated sequences return U+FFFD and advance by one byte,
 * so callers always make progress.
 */
inline uint32_t next(const char* s, size_t len, size_t& i) {
    const uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
        i += 1;
        return c;
    }

    int extra;
    uint32_t cp;
    uint32_t minValue;
    if ((c & 0xE0) == 0xC0) {
        extra = 1; cp = c & 0x1F; minValue = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2; cp = c & 0x0F; minValue = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3; cp = c & 0x07; minValue = 0x10000;
    } else {
        i += 1;
        return kReplacement;
    }

    if (i + extra >= len) {  // truncated sequence
        i += 1;
        return kReplacement;
    }
    for (int k = 1; k <= extra; k++) {
        const uint8_t b = (uint8_t)s[i + k];
        if (!isContinuation(b)) {
            i += 1;
            return kReplacement;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < minValue || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        i += 1;
        return kReplacement;
    }
    i += extra + 1;
    return cp;
}

/** Encode one code point; returns the number of bytes written (1-4). */
inline size_t encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/** Largest length <= maxBytes that does not split a multi-byte sequence. */
inline size_t clampToBoundary(const char* s, size_t len, size_t maxBytes) {
    if (maxBytes >= len) return len;
    size_t n = maxBytes;
    while (n > 0 && isContinuation((uint8_t)s[n])) n--;
    return n;
}

/** Smallest offset >= from that starts a code point (skips continuation bytes). */
inline size_t alignForward(const char* s, size_t len, size_t from) {
    while (from < len && isContinuation((uint8_t)s[from])) from++;
    return from;
}

/**
 * Length without a multi-byte sequence cut off at the end (text truncated by bytes elsewhere).
 * Invalid UTF-8 in a WebSocket text frame lets the server close the session (RFC 6455: 1007).
 */
inline size_t completeLength(const char* s, size_t len) {
    size_t lead = len;
    while (lead > 0 && isContinuation((uint8_t)s[lead - 1])) lead--;
    if (lead == 0) return len;  // only continuation bytes: nothing to anchor on
    const uint8_t b = (uint8_t)s[lead - 1];
    const size_t need = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
    return len - (lead - 1) >= need ? len : lead - 1;
}

/** Thai vowels/tone marks that are drawn above/below the previous character. */
inline bool isThaiCombining(uint32_t cp) {
    return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E3A) || (cp >= 0x0E47 && cp <= 0x0E4E);
}

/** Thai leading vowels (เ แ โ ใ ไ) — written before the consonant they belong to. */
inline bool isThaiLeadingVowel(uint32_t cp) { return cp >= 0x0E40 && cp <= 0x0E44; }

/** Thai following vowels/marks that must not start a new line. */
inline bool isThaiTrailing(uint32_t cp) {
    return cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E33 || cp == 0x0E45 || cp == 0x0E46;
}

}  // namespace utf8
