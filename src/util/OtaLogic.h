/**
 * @file OtaLogic.h
 * @brief Pure parts of the firmware updater (host tested): versions, release JSON, checksums, image checks
 * @see src/system/OtaUpdater.cpp, wiki/13-OTA-Update.md
 */

#pragma once

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace ota {

/** Marker compiled into every image ("PAB-ESP32-FW:<version>"): an update must carry it, with the
 *  version the release says - so a binary of another project (or a mislabelled one) is refused. */
inline constexpr char kMarkerPrefix[] = "PAB-ESP32-FW:";

/** "v3.4.10" / "3.4.10" -> {3,4,10}. False if it is not 1-3 dot-separated numbers. */
inline bool parseVersion(const char* s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        char* end = nullptr;
        const long v = strtol(s, &end, 10);
        if (v < 0 || v > 9999) return false;
        out[i] = (int)v;
        s = end;
        if (*s == '\0') return true;
        if (*s != '.') return false;
        s++;
    }
    return false;  // a fourth part or trailing text ("3.5.0-rc1")
}

/** <0 / 0 / >0 like strcmp; 0 as well when either is not a plain version (never "upgrades" to junk). */
inline int compareVersions(const char* a, const char* b) {
    int x[3], y[3];
    if (!parseVersion(a, x) || !parseVersion(b, y)) return 0;
    for (int i = 0; i < 3; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

/** First 64 hex characters of a "<sha256>  <file>" line, lower-cased into out[65]. */
inline bool parseSha256(const char* text, size_t len, char out[65]) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)text[i])) i++;
    if (len - i < 64) return false;
    for (size_t k = 0; k < 64; k++) {
        const char c = (char)tolower((unsigned char)text[i + k]);
        if (!isxdigit((unsigned char)c)) return false;
        out[k] = c;
    }
    if (i + 64 < len && !isspace((unsigned char)text[i + 64])) return false;  // longer than a sha256
    out[64] = '\0';
    return true;
}

struct Release {
    char tag[24] = "";       // "v3.5.0"
    char appUrl[256] = "";   // browser_download_url of *-app.bin
    char shaUrl[256] = "";   // ... of *-app.bin.sha256
    uint32_t appSize = 0;
};

/**
 * GitHub "latest release" JSON -> tag + the app image and its checksum. False if either asset is
 * missing (r.tag is still filled when the tag itself is valid: older releases have no app image).
 */
inline bool parseRelease(const char* json, size_t len, Release& r) {
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["draft"] = true;
    filter["prerelease"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
    if ((doc["draft"] | false) || (doc["prerelease"] | false)) return false;
    const char* tag = doc["tag_name"] | "";
    int v[3];
    if (!parseVersion(tag, v) || strlen(tag) >= sizeof(r.tag)) return false;
    r = Release();
    strcpy(r.tag, tag);
    for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>()) {
        const char* name = a["name"] | "";
        const char* url = a["browser_download_url"] | "";
        const size_t n = strlen(name);
        if (strncmp(url, "https://", 8) != 0 || strlen(url) >= sizeof(r.appUrl)) continue;
        if (n > 8 && !strcmp(name + n - 8, "-app.bin")) {
            strcpy(r.appUrl, url);
            r.appSize = a["size"] | 0u;
        } else if (n > 15 && !strcmp(name + n - 15, "-app.bin.sha256")) {
            strcpy(r.shaUrl, url);
        }
    }
    return r.appUrl[0] && r.shaUrl[0];
}

/**
 * First bytes of an ESP application image: magic 0xE9, the chip id at offset 12 (ESP32-S3 = 9), and
 * the app descriptor magic at offset 32 (after the image + first segment headers).
 */
inline const char* checkImageHeader(const uint8_t* p, size_t n, uint16_t chipId) {
    if (n < 36) return "image too short";
    if (p[0] != 0xE9) return "not an ESP32 application image";
    if ((uint16_t)(p[12] | (p[13] << 8)) != chipId) return "image is for another chip";
    const uint32_t descMagic = (uint32_t)p[32] | ((uint32_t)p[33] << 8) | ((uint32_t)p[34] << 16) | ((uint32_t)p[35] << 24);
    if (descMagic != 0xABCD5432u) return "no app descriptor";
    return nullptr;
}

/**
 * Finds "PAB-ESP32-FW:<version>" in a stream fed in chunks of any size (it may straddle two chunks).
 * version() is the text after the prefix once a terminating NUL has been seen.
 */
class MarkerScanner {
public:
    void feed(const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n && !_done; i++) step((char)p[i]);
    }
    bool found() const { return _done; }
    const char* version() const { return _ver; }

private:
    static constexpr size_t kPrefix = sizeof(kMarkerPrefix) - 1;
    char _win[kPrefix] = {};  // the last kPrefix bytes (sliding window: exact, ~2 MB images scan in ~0.1 s)
    size_t _seen = 0;
    bool _inVer = false, _done = false;
    char _ver[24] = "";
    size_t _vlen = 0;

    void step(char c) {
        if (_inVer) {
            if (c == '\0') {  // "PAB-ESP32-FW:" followed directly by NUL is the scanner's own constant
                _ver[_vlen] = '\0';
                _done = _vlen > 0;
                _inVer = false;
            } else if (_vlen + 1 < sizeof(_ver) && (isdigit((unsigned char)c) || c == '.')) {
                _ver[_vlen++] = c;
                return;
            } else {
                _inVer = false;  // not ours: keep looking
            }
            _vlen = 0;
        }
        memmove(_win, _win + 1, kPrefix - 1);
        _win[kPrefix - 1] = c;
        if (++_seen >= kPrefix && !memcmp(_win, kMarkerPrefix, kPrefix)) {
            _inVer = true;
            _vlen = 0;
        }
    }
};

}  // namespace ota
