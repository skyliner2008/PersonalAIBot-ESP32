/**
 * @file TextFeatures.cpp
 * @brief Character-bigram / word features for Thai + English text
 */

#include "TextFeatures.h"

#include <algorithm>

#include "../util/Utf8.h"

namespace features {

namespace {

enum CharClass : uint8_t { kSeparator, kLatin, kScript, kSkip };

// Map a code point to its normalised value and class.
CharClass classify(uint32_t cp, uint32_t& norm) {
    norm = cp;
    if (cp < 0x80) {
        if (cp >= 'A' && cp <= 'Z') {
            norm = cp + 32;
            return kLatin;
        }
        if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) return kLatin;
        return kSeparator;
    }
    if (cp >= 0x0E00 && cp <= 0x0E7F) {
        if (cp >= 0x0E48 && cp <= 0x0E4B) return kSkip;   // tone marks
        if (cp >= 0x0E50 && cp <= 0x0E59) {               // Thai digits -> ASCII
            norm = '0' + (cp - 0x0E50);
            return kLatin;
        }
        if (cp == 0x0E2F || cp == 0x0E46 || cp == 0x0E5A || cp == 0x0E5B) return kSeparator;  // ฯ ๆ ๚ ๛
        return kScript;
    }
    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) ||
        cp == 0xFEFF || cp == utf8::kReplacement) {
        return kSeparator;  // spaces, general/CJK punctuation, BOM
    }
    if (cp >= 0xFF01 && cp <= 0xFF5E) {  // full-width ASCII
        return classify(cp - 0xFEE0, norm);
    }
    return kScript;
}

inline uint32_t fnv1a(uint32_t h, uint32_t v) {
    for (int k = 0; k < 4; k++) {
        h ^= (v >> (k * 8)) & 0xFF;
        h *= 16777619u;
    }
    return h;
}

constexpr uint32_t kSeedWord = 2166136261u;
constexpr uint32_t kSeedUnigram = 0x9E3779B1u;
constexpr uint32_t kSeedBigram = 0x85EBCA6Bu;

}  // namespace

void extract(const char* text, size_t len, HashSet& out, uint16_t* tokenCount) {
    out.clear();
    if (!text || len == 0) {
        if (tokenCount) *tokenCount = 0;
        return;
    }

    uint32_t count = 0;

    // Current run state
    CharClass runClass = kSeparator;
    uint32_t wordHash = kSeedWord;
    uint32_t prevScript = 0;
    uint32_t runLength = 0;

    auto flushRun = [&]() {
        if (runClass == kLatin && runLength > 0) {
            out.push_back(wordHash);
            count++;
        } else if (runClass == kScript && runLength == 1) {
            out.push_back(fnv1a(kSeedUnigram, prevScript));
            count++;
        }
        runClass = kSeparator;
        wordHash = kSeedWord;
        prevScript = 0;
        runLength = 0;
    };

    size_t i = 0;
    while (i < len) {
        const uint32_t cp = utf8::next(text, len, i);
        uint32_t norm;
        const CharClass cls = classify(cp, norm);
        if (cls == kSkip) continue;

        if (cls != runClass) flushRun();
        if (cls == kSeparator) continue;

        runClass = cls;
        if (cls == kLatin) {
            wordHash = fnv1a(wordHash, norm);
            runLength++;
        } else {  // kScript
            if (runLength > 0) {
                out.push_back(fnv1a(fnv1a(kSeedBigram, prevScript), norm));
                count++;
            }
            prevScript = norm;
            runLength++;
        }
    }
    flushRun();

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (tokenCount) *tokenCount = (uint16_t)(count > 0xFFFF ? 0xFFFF : count);
}

void normalize(const char* text, size_t len, PsString& out) {
    out.clear();
    if (!text) return;
    bool pendingSpace = false;
    size_t i = 0;
    char buf[4];
    while (i < len) {
        const uint32_t cp = utf8::next(text, len, i);
        uint32_t norm;
        const CharClass cls = classify(cp, norm);
        if (cls == kSkip) continue;
        if (cls == kSeparator) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        const size_t n = utf8::encode(norm, buf);
        out.append(buf, n);
    }
}

size_t intersectCount(const HashSet& a, const HashSet& b) {
    size_t i = 0, j = 0, n = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) {
            i++;
        } else if (a[i] > b[j]) {
            j++;
        } else {
            n++;
            i++;
            j++;
        }
    }
    return n;
}

float jaccard(const HashSet& a, const HashSet& b) {
    if (a.empty() && b.empty()) return 0.0f;
    const size_t inter = intersectCount(a, b);
    const size_t uni = a.size() + b.size() - inter;
    return uni ? (float)inter / (float)uni : 0.0f;
}

bool contains(const HashSet& set, uint32_t h) {
    return std::binary_search(set.begin(), set.end(), h);
}

}  // namespace features
