/**
 * @file Id3.h
 * @brief Song names for the MP3 player: ID3v2 title / artist, else a name made from the file name
 * @see wiki/12-Internet-Radio.md (MP3 from the SD card, T122)
 *
 * Header-only and portable (host tests). id3::read() takes a reader `int rd(uint32_t offset, uint8_t* buf, int n)`
 * so a tag with a big cover picture before the title frame never has to be loaded whole: frames are walked
 * header by header and only text frames are read. Text: UTF-8, UTF-16 (BOM / BE) and "Latin-1" - which Thai
 * taggers fill with TIS-620, so 0xA1-0xFB in a non-UTF-8 string is read as Thai (as the radio's ICY titles).
 */

#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Utf8.h"

namespace textcodec {

inline bool validUtf8(const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n;) {
        const uint8_t c = s[i];
        const size_t extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 9;
        if (extra == 9 || i + extra >= n + (extra ? 0 : 1)) return false;
        for (size_t k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

/** Thai TIS-620: 0xA1-0xFB -> U+0E01-U+0E5B; other high bytes are dropped. */
inline void tis620ToUtf8(const uint8_t* s, size_t n, char* out, size_t cap) {
    if (!cap) return;
    size_t o = 0;
    for (size_t i = 0; i < n && s[i] && o + 4 < cap; i++) {
        const uint8_t c = s[i];
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c >= 0xA1 && c <= 0xFB) {
            o += utf8::encode(0x0E00 + (c - 0xA0), out + o);
        }
    }
    out[o] = '\0';
}

/** UTF-8 as it is (cut on a character boundary), else TIS-620. Stops at a NUL. */
inline void narrowToUtf8(const uint8_t* s, size_t n, char* out, size_t cap) {
    if (!cap) return;
    const void* z = memchr(s, 0, n);
    if (z) n = (size_t)((const uint8_t*)z - s);
    if (validUtf8(s, n)) {
        const size_t len = utf8::clampToBoundary((const char*)s, n, cap - 1);
        memcpy(out, s, len);
        out[len] = '\0';
    } else {
        tis620ToUtf8(s, n, out, cap);
    }
}

/** UTF-16 (surrogate pairs too) -> UTF-8, stops at a NUL. */
inline void utf16ToUtf8(const uint8_t* s, size_t n, bool bigEndian, char* out, size_t cap) {
    if (!cap) return;
    size_t o = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t u = bigEndian ? (uint32_t)(s[i] << 8 | s[i + 1]) : (uint32_t)(s[i + 1] << 8 | s[i]);
        if (!u) break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
            const uint32_t lo = bigEndian ? (uint32_t)(s[i + 2] << 8 | s[i + 3]) : (uint32_t)(s[i + 3] << 8 | s[i + 2]);
            if (lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (u >= 0xD800 && u < 0xE000) continue;  // a lone surrogate
        char enc[4];
        const size_t k = utf8::encode(u, enc);
        if (o + k + 1 > cap) break;
        memcpy(out + o, enc, k);
        o += k;
    }
    out[o] = '\0';
}

}  // namespace textcodec

namespace id3 {

struct Tag {
    char title[96] = "";
    char artist[64] = "";
    uint32_t size = 0;  // bytes of ID3v2 at the start of the file (skip them before decoding), 0 = none
};

/** One ID3 text frame body (encoding byte + text) -> UTF-8, surrounding spaces trimmed. */
inline void decodeText(const uint8_t* d, size_t n, char* out, size_t cap) {
    if (!cap) return;
    out[0] = '\0';
    if (n < 2) return;
    const uint8_t enc = d[0];
    d++, n--;
    if (enc == 1 || enc == 2) {  // UTF-16 with BOM / UTF-16BE
        bool be = enc == 2;
        if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) be = false, d += 2, n -= 2;
        else if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) be = true, d += 2, n -= 2;
        textcodec::utf16ToUtf8(d, n, be, out, cap);
    } else {
        textcodec::narrowToUtf8(d, n, out, cap);  // 0 "Latin-1" (Thai taggers: TIS-620), 3 UTF-8
    }
    size_t a = 0, len = strlen(out);
    while (a < len && (unsigned char)out[a] <= ' ') a++;
    while (len > a && (unsigned char)out[len - 1] <= ' ') len--;
    memmove(out, out + a, len - a);
    out[len - a] = '\0';
}

inline uint32_t syncsafe(const uint8_t* b) {
    return (uint32_t)(b[0] & 0x7F) << 21 | (uint32_t)(b[1] & 0x7F) << 14 | (uint32_t)(b[2] & 0x7F) << 7 | (b[3] & 0x7F);
}
inline uint32_t be32(const uint8_t* b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }

/**
 * Reads the ID3v2 tag at the start of a file (v2.2 / v2.3 / v2.4). False = no tag (t.size 0); true also when
 * the tag has no title (t.title ""). `rd(offset, buf, n)` returns the bytes read.
 */
template <class Read>
bool read(Read&& rd, Tag& t) {
    t = Tag();
    uint8_t h[10];
    if (rd(0, h, 10) != 10 || memcmp(h, "ID3", 3) != 0 || h[3] < 2 || h[3] > 4) return false;
    const uint8_t ver = h[3], flags = h[5];
    const uint32_t body = syncsafe(h + 6);
    t.size = 10 + body + (ver == 4 && (flags & 0x10) ? 10 : 0);
    uint32_t pos = 10;
    const uint32_t end = 10 + body;
    if ((flags & 0x40) && ver >= 3) {  // extended header
        uint8_t e[4];
        if (rd(pos, e, 4) != 4) return true;
        pos += ver == 4 ? syncsafe(e) : 4 + be32(e);
    }
    const int hdr = ver == 2 ? 6 : 10;
    for (int guard = 0; guard < 256 && pos + hdr <= end; guard++) {
        uint8_t f[10];
        if (rd(pos, f, hdr) != hdr || f[0] == 0) break;  // padding
        uint32_t size;
        char id[5] = {(char)f[0], (char)f[1], (char)f[2], ver == 2 ? '\0' : (char)f[3], '\0'};
        if (ver == 2) size = (uint32_t)f[3] << 16 | (uint32_t)f[4] << 8 | f[5];
        else size = ver == 4 ? syncsafe(f + 4) : be32(f + 4);
        pos += hdr;
        if (size == 0 || size > end - pos) break;
        const bool isTitle = !strcmp(id, "TIT2") || !strcmp(id, "TT2");
        const bool isArtist = !strcmp(id, "TPE1") || !strcmp(id, "TP1");
        if (isTitle || isArtist) {
            uint8_t buf[256];
            const int n = size < sizeof(buf) ? (int)size : (int)sizeof(buf);
            if (rd(pos, buf, n) == n) {
                if (isTitle) decodeText(buf, (size_t)n, t.title, sizeof(t.title));
                else decodeText(buf, (size_t)n, t.artist, sizeof(t.artist));
            }
            if (t.title[0] && t.artist[0]) break;
        }
        pos += size;
    }
    return true;
}

/** "/sdcard/music/Pop/01 - Song_Name.mp3" -> "Song Name": no folder, extension, leading track number. */
inline void titleFromPath(const char* path, char* out, size_t cap) {
    if (!cap) return;
    const char* s = strrchr(path, '/');
    s = s ? s + 1 : path;
    size_t n = strlen(s);
    const char* dot = strrchr(s, '.');
    if (dot && dot > s) n = (size_t)(dot - s);
    size_t i = 0;
    while (i < n && i < 3 && isdigit((unsigned char)s[i])) i++;
    // "01 - ", "01. ", "01_" ... only after a number, and never the whole name ("1999.mp3" stays)
    size_t j = i;
    while (j < n && (s[j] == ' ' || s[j] == '-' || s[j] == '.' || s[j] == '_')) j++;
    if (i == 0 || j == i || j >= n) j = 0;
    // cut on the source (a cut copy has no next byte to tell a split character: "...ผ่านพ้น\xE0\xB9" showed as "�")
    const size_t o = utf8::clampToBoundary(s + j, n - j, cap - 1);
    for (size_t k = 0; k < o; k++) out[k] = s[j + k] == '_' ? ' ' : s[j + k];
    out[o] = '\0';
}

}  // namespace id3
