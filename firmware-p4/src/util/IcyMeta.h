/**
 * @file IcyMeta.h
 * @brief "icy-metaint" header of an Internet radio stream (header-only, host tested)
 *
 * The value comes from the station's server (or anyone on the way: most streams are plain http) and
 * sets how many audio bytes to read before each metadata block. A negative one became a negative read
 * size, which the client takes as a size_t: kilobytes written past the end of the 16 KB stream buffer.
 */

#pragma once

#include <ctype.h>
#include <stdlib.h>

namespace icy {

constexpr long kMaxMetaInterval = 256 * 1024;  // real streams send 8000-32768

/** Audio bytes between ICY metadata blocks; 0 = no metadata (missing, not a number, <= 0 or absurd). */
inline int metaInterval(const char* header) {
    if (!header) return 0;
    while (*header == ' ' || *header == '\t') header++;
    if (!isdigit((unsigned char)*header)) return 0;  // also "-5", "+5"
    char* end = nullptr;
    const long v = strtol(header, &end, 10);
    while (*end == ' ' || *end == '\t') end++;
    if (*end || v <= 0 || v > kMaxMetaInterval) return 0;
    return (int)v;
}

}  // namespace icy
