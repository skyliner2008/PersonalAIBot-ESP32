/**
 * @file HttpRedirect.h
 * @brief Where an HTTP redirect points: the Location header resolved against the URL that sent it
 *        (header-only, host tested)
 *
 * Radio servers answer with all forms (RFC 9110 allows a relative reference): "https://cdn/x",
 * "//cdn/x" (same scheme), "/stream" (same host) or "live.mp3" (same folder). Only the first one
 * worked: the others went to HTTPClient as they were and failed with "bad URL".
 */

#pragma once

#include <stddef.h>
#include <string.h>

namespace redirect {

/** Copy `location` resolved against `base` into out[cap]. False if either is unusable or it does not fit. */
inline bool resolve(const char* base, const char* location, char* out, size_t cap) {
    if (!base || !location || !cap) return false;
    while (*location == ' ' || *location == '\t') location++;
    size_t locLen = strlen(location);
    while (locLen && (location[locLen - 1] == ' ' || location[locLen - 1] == '\t' || location[locLen - 1] == '\r')) locLen--;
    if (!locLen) return false;
    auto put = [&](const char* a, size_t na, const char* b, size_t nb) {
        if (na + nb + 1 > cap) return false;
        memcpy(out, a, na);
        memcpy(out + na, b, nb);
        out[na + nb] = '\0';
        return true;
    };
    if (!strncmp(location, "http://", 7) || !strncmp(location, "https://", 8)) return put(location, locLen, "", 0);

    const char* scheme = strstr(base, "://");
    if (!scheme) return false;
    const size_t schemeLen = (size_t)(scheme - base) + 1;  // "https:"
    const char* host = scheme + 3;
    const size_t originLen = (size_t)(host - base) + strcspn(host, "/?#");  // "https://host:port"
    if (location[0] == '/' && location[1] == '/') return put(base, schemeLen, location, locLen);
    if (location[0] == '/') return put(base, originLen, location, locLen);
    if (location[0] == '?') {  // same path, new query
        return put(base, originLen + strcspn(base + originLen, "?#"), location, locLen);
    }
    // relative path: the folder of the base path ("http://h/a/b.pls" + "c.mp3" -> "http://h/a/c.mp3")
    const size_t pathLen = strcspn(base + originLen, "?#");
    size_t dir = originLen + pathLen;
    while (dir > originLen && base[dir - 1] != '/') dir--;
    if (dir == originLen) {  // no path at all: "http://h" + "x" -> "http://h/x"
        if (originLen + 1 + locLen + 1 > cap) return false;
        memcpy(out, base, originLen);
        out[originLen] = '/';
        memcpy(out + originLen + 1, location, locLen);
        out[originLen + 1 + locLen] = '\0';
        return true;
    }
    return put(base, dir, location, locLen);
}

}  // namespace redirect
