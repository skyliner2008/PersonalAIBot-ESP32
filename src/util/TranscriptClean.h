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

/**
 * Copy `in` to `out` without tool calls written out as text: "set_expression(emotion=happy)\nสวัสดีค่ะ" ->
 * "สวัสดีค่ะ" (3.8 Live began saying them on 2026-10-27, T109; kept in the conversation log they were fed back into
 * the next session's prompt and copied again). A call = identifier with an underscore ("get_weather"), then
 * "(...)" closed on the same line; the blanks after it go too. Plain words with brackets stay ("ราคา (บาท)").
 */
template <class Str>
void stripToolCallText(const char* in, Str& out) {
    out.clear();
    const size_t n = strlen(in);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        bool underscore = false;
        if (isalpha((unsigned char)in[i]) && (i == 0 || !isalnum((unsigned char)in[i - 1]))) {
            while (j < n && (isalnum((unsigned char)in[j]) || in[j] == '_')) {
                underscore |= in[j] == '_';
                j++;
            }
        }
        if (j > i && underscore && j < n && in[j] == '(') {
            size_t k = j + 1;
            while (k < n && in[k] != ')' && in[k] != '\n') k++;
            if (k < n && in[k] == ')') {
                k++;
                while (k < n && (in[k] == ' ' || in[k] == '\n' || in[k] == '\r')) k++;
                i = k;
                continue;
            }
        }
        const size_t len = j > i ? j - i : 1;
        out.append(in + i, len);
        i += len;
    }
}

}  // namespace transcript
