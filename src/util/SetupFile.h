/**
 * @file SetupFile.h
 * @brief "ai-setup.txt" on the SD card: Wi-Fi / API key typed on a PC, imported once by the board
 *
 *     # comments are ignored
 *     WIFI_SSID=MyHome_2.4G
 *     WIFI_PASSWORD=secret
 *     GEMINI_API_KEY=AIza...
 *
 * Unknown keys (e.g. MT5_URL / MT5_TOKEN from older templates) are ignored. Keys are case-insensitive, values may be quoted, a UTF-8 BOM (Windows Notepad) is skipped and
 * template values ("your-...", "<...>", "YourWiFiName") count as empty. Header-only (host tested).
 */

#pragma once

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace setupfile {

struct Values {
    char ssid[33] = "", pass[65] = "", key[128] = "";
    bool any = false;
};

// Only untouched template values count as empty: "<...>" as a whole, or the secrets.example.h words.
// (A real password may contain '<' or start with "your".)
inline bool isTemplate(const char* v) {
    if (!v || !*v) return true;
    const size_t n = strlen(v);
    if (v[0] == '<' && v[n - 1] == '>') return true;
    return !strcmp(v, "YourWiFiName") || !strcmp(v, "YourWiFiPassword") || !strcmp(v, "your-gemini-api-key-here");
}

inline void copyTo(char* dst, size_t cap, const char* src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/** Parse one line into `out`; returns true if it set a value. */
inline bool parseLine(char* s, Values& out) {
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == ';' || !*s) return false;
    char* eq = strchr(s, '=');
    if (!eq) return false;
    *eq = '\0';
    char* k = s;
    char* v = eq + 1;
    for (char* e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); *--e = '\0') {}
    while (*v == ' ' || *v == '\t') v++;
    for (char* e = v + strlen(v); e > v && strchr(" \t\r\n", e[-1]); *--e = '\0') {}
    const size_t vl = strlen(v);
    if (vl >= 2 && (v[0] == '"' || v[0] == '\'') && v[vl - 1] == v[0]) {
        v[vl - 1] = '\0';
        v++;
    }
    for (char* c = k; *c; c++) *c = (char)toupper((unsigned char)*c);
    if (isTemplate(v)) return false;
    if (!strcmp(k, "WIFI_SSID")) copyTo(out.ssid, sizeof(out.ssid), v);
    else if (!strcmp(k, "WIFI_PASSWORD")) copyTo(out.pass, sizeof(out.pass), v);
    else if (!strcmp(k, "GEMINI_API_KEY")) copyTo(out.key, sizeof(out.key), v);
    else return false;
    out.any = true;
    return true;
}

/** Read the file; false if it is missing or only holds template values. */
inline bool parse(const char* path, Values& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) parseLine(line, out);
    memset(line, 0, sizeof(line));
    fclose(f);
    return out.any;
}

/** Overwrite the file with zeros, then delete it (best effort: its sectors no longer hold the key). */
inline void wipe(const char* path) {
    FILE* f = fopen(path, "r+b");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        static const char kZeros[64] = {0};
        while (n > 0) {
            const size_t chunk = n > (long)sizeof(kZeros) ? sizeof(kZeros) : (size_t)n;
            fwrite(kZeros, 1, chunk, f);
            n -= (long)chunk;
        }
        fflush(f);
        fclose(f);
    }
    ::remove(path);
}

}  // namespace setupfile
