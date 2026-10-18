/**
 * @file OtaPublicKey.h
 * @brief Public half of the release signing key (ECDSA P-256) - written by tools/ota_keygen.py
 * @see wiki/13-OTA-Update.md
 *
 * A firmware built with a key here installs over Wi-Fi only a -app.bin whose .sig verifies against it.
 * Empty = no key yet: updates are checked by sha256 + version marker only (as before v4.2.0).
 * The private half never enters the repository (default ~/.pab-esp32/ota_signing_key.pem on the PC
 * that runs tools/publish_release.py). Public keys are not secret: committing this file is fine.
 */

#pragma once

#define OTA_SIGNING_PUBKEY_PEM ""
