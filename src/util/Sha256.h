/**
 * @file Sha256.h
 * @brief SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104) - header-only, host tested with the RFC 4231 vectors
 *
 * For short messages (the phone link's challenge-response, src/phone/PhoneProtocol.h); TLS and the OTA signature
 * use mbedtls. Same results as javax.crypto.Mac("HmacSHA256") in the phone app.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sha256 {

class Hash {
public:
    Hash() { reset(); }

    void reset() {
        static const uint32_t kInit[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        memcpy(_h, kInit, sizeof(_h));
        _len = 0;
        _used = 0;
    }

    void update(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        _len += n;
        while (n) {
            const size_t take = n < 64 - _used ? n : 64 - _used;
            memcpy(_block + _used, p, take);
            _used += take;
            p += take;
            n -= take;
            if (_used == 64) {
                compress(_block);
                _used = 0;
            }
        }
    }

    void finish(uint8_t out[32]) {
        const uint64_t bits = _len * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (_used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 8; i++) {
            out[4 * i] = (uint8_t)(_h[i] >> 24);
            out[4 * i + 1] = (uint8_t)(_h[i] >> 16);
            out[4 * i + 2] = (uint8_t)(_h[i] >> 8);
            out[4 * i + 3] = (uint8_t)_h[i];
        }
    }

private:
    uint32_t _h[8];
    uint8_t _block[64];
    size_t _used;
    uint64_t _len;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const uint8_t* b) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = _h[0], bb = _h[1], c = _h[2], d = _h[3], e = _h[4], f = _h[5], g = _h[6], h = _h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = bb;
            bb = a;
            a = t1 + t2;
        }
        _h[0] += a;
        _h[1] += bb;
        _h[2] += c;
        _h[3] += d;
        _h[4] += e;
        _h[5] += f;
        _h[6] += g;
        _h[7] += h;
    }
};

inline void hmac(const void* key, size_t keyLen, const void* msg, size_t msgLen, uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (keyLen > 64) {
        Hash h;
        h.update(key, keyLen);
        h.finish(k);
    } else {
        memcpy(k, key, keyLen);
    }
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    Hash inner;
    inner.update(pad, 64);
    inner.update(msg, msgLen);
    uint8_t ih[32];
    inner.finish(ih);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    Hash outer;
    outer.update(pad, 64);
    outer.update(ih, 32);
    outer.finish(out);
}

/** 32 bytes -> 64 lowercase hex characters + '\0'. */
inline void toHex(const uint8_t in[32], char out[65]) {
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = kHex[in[i] >> 4];
        out[2 * i + 1] = kHex[in[i] & 15];
    }
    out[64] = '\0';
}

}  // namespace sha256
