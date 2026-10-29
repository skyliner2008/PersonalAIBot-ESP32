/**
 * @file Base64.cpp
 * @brief Allocation-free Base64 implementation
 */

#include "Base64.h"

namespace b64 {

static const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0-63 = value, 64 = padding, 65 = whitespace, 255 = invalid
static uint8_t decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 52);
    if (c == '+' || c == '-') return 62;  // also accept URL-safe alphabet
    if (c == '/' || c == '_') return 63;
    if (c == '=') return 64;
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return 65;
    return 255;
}

size_t encode(const uint8_t* in, size_t len, char* out) {
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = kAlphabet[(v >> 6) & 0x3F];
        out[o++] = kAlphabet[v & 0x3F];
        i += 3;
    }
    const size_t rest = len - i;
    if (rest == 1) {
        const uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rest == 2) {
        const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = kAlphabet[(v >> 18) & 0x3F];
        out[o++] = kAlphabet[(v >> 12) & 0x3F];
        out[o++] = kAlphabet[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

size_t decode(const char* in, size_t len, uint8_t* out) {
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    bool padding = false;

    for (size_t i = 0; i < len; i++) {
        const uint8_t v = decodeChar(in[i]);
        if (v == 65) continue;
        if (v == 255) return kError;
        if (v == 64) {
            padding = true;
            continue;
        }
        if (padding) return kError;  // data after '='
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return o;
}

}  // namespace b64
