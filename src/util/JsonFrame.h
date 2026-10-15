/**
 * @file JsonFrame.h
 * @brief Detect whether a buffer holds one complete JSON object
 *
 * The WebSocket transport may deliver a server message split across
 * several frames/chunks. We only hand a message to the JSON parser once
 * the outer object is closed.
 */

#pragma once

#include <stddef.h>

namespace jsonframe {

/** True if `s` contains exactly one complete JSON object (plus whitespace). */
inline bool isCompleteObject(const char* s, size_t len) {
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    if (i >= len || s[i] != '{') return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (; i < len; i++) {
        const char c = s[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) {
                // Everything after the closing brace must be whitespace
                for (size_t j = i + 1; j < len; j++) {
                    const char t = s[j];
                    if (t != ' ' && t != '\n' && t != '\r' && t != '\t') return false;
                }
                return true;
            }
            if (depth < 0) return false;
        }
    }
    return false;
}

}  // namespace jsonframe
