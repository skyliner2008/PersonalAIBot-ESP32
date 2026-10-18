/**
 * @file OtaVerify.h
 * @brief ECDSA P-256 check of a firmware image's SHA-256 against the release signing key (mbedtls)
 * @see src/app/OtaPublicKey.h, tools/ota_keygen.py, wiki/13-OTA-Update.md
 *
 * Ed25519 would be shorter, but the mbedtls of IDF 5.5 has no EdDSA; ECDSA P-256 is already in the
 * firmware for TLS. tools/make_release.py signs the -app.bin with Python's `cryptography`
 * (ECDSA + SHA-256, DER signature) - the same thing mbedtls_pk_verify checks here.
 */

#pragma once

#include <mbedtls/pk.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ota {

/** nullptr = `sig` (DER) is a valid signature of `digest` (SHA-256 of the image) by `publicKeyPem`, else why not. */
inline const char* verifySignature(const char* publicKeyPem, const uint8_t digest[32], const uint8_t* sig, size_t sigLen) {
    if (!publicKeyPem || !*publicKeyPem) return "no public key";
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    const char* why = nullptr;
    if (mbedtls_pk_parse_public_key(&pk, (const unsigned char*)publicKeyPem, strlen(publicKeyPem) + 1) != 0) {
        why = "public key unreadable";
    } else if (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_ECKEY || mbedtls_pk_get_bitlen(&pk) != 256) {
        why = "public key is not ECDSA P-256";
    } else if (mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, sig, sigLen) != 0) {
        why = "bad signature";
    }
    mbedtls_pk_free(&pk);
    return why;
}

}  // namespace ota
