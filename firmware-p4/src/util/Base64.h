/**
 * @file Base64.h
 * @brief Allocation-free Base64 (RFC 4648, standard alphabet)
 *
 * Used for the Gemini Live audio path:
 *   mic PCM  -> base64 -> realtimeInput.audio.data
 *   modelTurn.parts[].inlineData.data -> base64 -> speaker PCM
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace b64 {

constexpr size_t kError = (size_t)-1;

/** Encoded length (without NUL) for n input bytes. */
inline size_t encodedLength(size_t n) { return 4 * ((n + 2) / 3); }

/** Upper bound of decoded bytes for n Base64 characters. */
inline size_t decodedMaxLength(size_t n) { return (n / 4) * 3 + 3; }

/**
 * Encode `len` bytes into `out` (must hold encodedLength(len) + 1 bytes).
 * Writes a terminating NUL. Returns the encoded length.
 */
size_t encode(const uint8_t* in, size_t len, char* out);

/**
 * Decode `len` Base64 characters into `out`.
 * Whitespace is skipped; padding is optional. In-place decoding
 * (out == (uint8_t*)in) is supported because output never overtakes input.
 * Returns the number of bytes written, or kError on invalid input.
 */
size_t decode(const char* in, size_t len, uint8_t* out);

}  // namespace b64
