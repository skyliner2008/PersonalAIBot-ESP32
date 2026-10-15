/**
 * @file SecretBox.cpp
 * @brief Device-bound AES-256-GCM for secrets kept in NVS
 */

#include "SecretBox.h"

#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_random.h>
#include <esp_system.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace secretbox {

namespace {

constexpr char kLabel[] = "ESP32-AI secretbox v1";
uint8_t gKey[32];
bool gReady = false;

}  // namespace

bool begin() {
    if (gReady) return true;
    uint8_t mac[6] = {0};
    uint8_t uid[16] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
    esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, sizeof(uid) * 8);  // all-zero on old chips: MAC still binds

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0 &&
              mbedtls_sha256_update_ret(&sha, (const uint8_t*)kLabel, sizeof(kLabel) - 1) == 0 &&
              mbedtls_sha256_update_ret(&sha, mac, sizeof(mac)) == 0 &&
              mbedtls_sha256_update_ret(&sha, uid, sizeof(uid)) == 0 &&
              mbedtls_sha256_finish_ret(&sha, gKey) == 0;
    mbedtls_sha256_free(&sha);
    gReady = ok;
    return ok;
}

bool seal(const char* name, const char* plain, uint8_t* out, size_t cap, size_t& outLen) {
    if (!begin() || !name || !plain) return false;
    const size_t n = strlen(plain);
    if (cap < n + kOverhead) return false;
    out[0] = 'S';
    out[1] = '1';
    uint8_t* iv = out + 2;
    uint8_t* tag = out + 14;
    esp_fill_random(iv, 12);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, gKey, 256) == 0 &&
                    mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, n, iv, 12, (const uint8_t*)name,
                                              strlen(name), (const uint8_t*)plain, out + kOverhead, 16, tag) == 0;
    mbedtls_gcm_free(&gcm);
    outLen = ok ? n + kOverhead : 0;
    return ok;
}

bool open(const char* name, const uint8_t* blob, size_t len, char* plain, size_t cap) {
    if (cap) plain[0] = '\0';
    if (!begin() || !name || len < kOverhead || blob[0] != 'S' || blob[1] != '1') return false;
    const size_t n = len - kOverhead;
    if (n + 1 > cap) return false;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, gKey, 256) == 0 &&
                    mbedtls_gcm_auth_decrypt(&gcm, n, blob + 2, 12, (const uint8_t*)name, strlen(name), blob + 14,
                                             16, blob + kOverhead, (uint8_t*)plain) == 0;
    mbedtls_gcm_free(&gcm);
    plain[ok ? n : 0] = '\0';
    if (!ok) memset(plain, 0, cap);
    return ok;
}

}  // namespace secretbox
