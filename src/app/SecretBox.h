/**
 * @file SecretBox.h
 * @brief AES-256-GCM sealing of the Wi-Fi password and Gemini API key stored in NVS / on the SD card
 * @see wiki/07-Troubleshooting-Log.md (T042)
 *
 * The key is SHA-256 over a fixed label + this chip's factory MAC + its 128-bit OPTIONAL_UNIQUE_ID
 * eFuse block, so a copied NVS partition does not open on another board and a plain flash dump no
 * longer shows the secrets as text. It is not a substitute for flash encryption: someone holding
 * this board AND this firmware can still derive the key (both are readable without eFuse protection).
 *
 * Blob layout: 'S' '1' | IV (12) | tag (16) | ciphertext. The NVS key name is authenticated as
 * additional data, so blobs cannot be swapped between fields.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace secretbox {

constexpr size_t kOverhead = 2 + 12 + 16;

/** Derive the device key (idempotent). False if the eFuse/MAC read or hashing failed. */
bool begin();

/** Encrypt NUL-terminated `plain`; `out` needs strlen(plain) + kOverhead bytes. */
bool seal(const char* name, const char* plain, uint8_t* out, size_t cap, size_t& outLen);

/** Decrypt into `plain` (NUL-terminated). False if tampered, from another chip, or too long. */
bool open(const char* name, const uint8_t* blob, size_t len, char* plain, size_t cap);

}  // namespace secretbox
