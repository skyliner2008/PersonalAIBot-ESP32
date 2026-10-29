/**
 * @file TextWrap.h
 * @brief Line breaking for mixed Thai/English text on a narrow LCD
 *
 * Thai has no spaces between words, so we break between characters, but
 * never in the middle of a syllable cluster:
 *   - combining vowels/tone marks stay with their base consonant
 *   - leading vowels (เ แ โ ใ ไ) stay with the following consonant
 *   - trailing vowels (ะ า ำ ๅ ๆ) never start a line
 * Spaces are preferred break points when available.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "Utf8.h"

namespace textwrap {

struct Line {
    size_t start;  // byte offset
    size_t len;    // byte length (without trailing spaces)
};

inline bool canBreakBefore(uint32_t prev, uint32_t cp) {
    if (utf8::isThaiCombining(cp) || utf8::isThaiTrailing(cp)) return false;
    if (utf8::isThaiLeadingVowel(prev)) return false;
    return true;
}

/**
 * Wrap `s` into lines no wider than maxWidth.
 * widthOf(cp) returns the advance width of one code point in pixels.
 */
template <class WidthFn>
std::vector<Line> wrap(const char* s, size_t len, int maxWidth, WidthFn widthOf) {
    std::vector<Line> lines;
    size_t i = 0;

    while (i < len) {
        // Skip leading spaces of a line
        while (i < len && s[i] == ' ') i++;
        if (i >= len) break;

        const size_t lineStart = i;
        int width = 0;
        size_t spaceBreak = 0;   // byte index of a space we may break at
        int spaceBreakWidth = 0; // line width up to that space
        size_t charBreak = 0;    // byte index where a new line may start
        uint32_t prev = 0;
        bool hardBreak = false;
        size_t end = len;

        while (i < len) {
            const size_t cpStart = i;
            const uint32_t cp = utf8::next(s, len, i);

            if (cp == '\n') {
                end = cpStart;
                hardBreak = true;
                break;
            }

            const int w = utf8::isThaiCombining(cp) ? 0 : widthOf(cp);
            if (cpStart > lineStart && canBreakBefore(prev, cp)) {
                charBreak = cpStart;
            }
            if (cp == ' ') {
                spaceBreak = cpStart;
                spaceBreakWidth = width;
            }

            if (width + w > maxWidth && cpStart > lineStart && w > 0) {
                // English words must not be split; for Thai, only use a space
                // if it does not leave a very short line behind.
                const bool latinWord = cp < 0x80;
                const bool spaceOk = spaceBreak > lineStart &&
                                     (latinWord || spaceBreakWidth >= maxWidth / 2);
                if (spaceOk) {
                    end = spaceBreak;
                } else if (charBreak > lineStart) {
                    end = charBreak;
                } else {
                    end = cpStart;  // forced break (single very wide cluster)
                }
                i = end;
                break;
            }
            width += w;
            prev = cp;
        }

        size_t lineLen = end - lineStart;
        while (lineLen > 0 && s[lineStart + lineLen - 1] == ' ') lineLen--;
        lines.push_back(Line{lineStart, lineLen});

        if (hardBreak) i = end + 1;  // skip '\n'
        else if (i < end) i = end;
    }
    return lines;
}

}  // namespace textwrap
