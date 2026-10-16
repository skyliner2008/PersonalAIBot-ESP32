/**
 * @file TranscriptClean.h
 * @brief Drops tool-call scaffolding that leaks into the Live output transcription (host tested)
 *
 * 3.1 Flash Live sometimes prints "functionreponsetextfunctionสวัสดีค่ะ..." after parallel tool
 * calls. An ASCII run is removed only when it is made of scaffolding words alone and contains
 * "function" - the old rule (any run containing "function") also ate real words such as
 * "functional" or "dysfunction" from English answers.
 */

#pragma once

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace transcript {

/** True if s[0..n) (ASCII letters) is only scaffolding words and has "function" in it. */
inline bool isToolScaffolding(const char* s, size_t n) {
    static const char* const kWords[] = {"functionresponse", "functioncall", "function", "response", "reponse",
                                         "respone", "result", "calls", "call", "text", "name", "args", "id"};
    bool sawFunction = false;
    size_t i = 0;
    while (i < n) {
        size_t matched = 0;
        for (const char* w : kWords) {
            const size_t len = strlen(w);
            if (len > n - i) continue;
            size_t k = 0;
            while (k < len && tolower((unsigned char)s[i + k]) == w[k]) k++;
            if (k == len) {
                matched = len;
                if (!strncmp(w, "function", 8)) sawFunction = true;
                break;  // kWords is ordered so the longest form wins
            }
        }
        if (!matched) return false;
        i += matched;
    }
    return sawFunction;
}

/** Copy `in` to `out` (anything with append(const char*, size_t)) without scaffolding runs. */
template <class Str>
void stripToolScaffolding(const char* in, Str& out) {
    out.clear();
    const size_t n = strlen(in);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && isalpha((unsigned char)in[j])) j++;
        if (j > i) {
            if (!isToolScaffolding(in + i, j - i)) out.append(in + i, j - i);
            i = j;
        } else {
            out.append(in + i, 1);
            i++;
        }
    }
}

}  // namespace transcript
