/**
 * @file ModelFallback.cpp
 * @brief Failure classification and model selection with cool-downs
 */

#include "ModelFallback.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

namespace fallback {

namespace {

bool containsNoCase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return false;
    const size_t n = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

Failure classifyText(const char* t) {
    if (!t || !*t) return Failure::None;
    // "BidiGenerateContent session not found or expired" (1008) = stale resumption handle, seen on the
    // device after waking from sleep. Must be checked before the generic "not found" (model) rule.
    if ((containsNoCase(t, "session") && (containsNoCase(t, "expired") || containsNoCase(t, "not found"))) ||
        containsNoCase(t, "resumption")) {
        return Failure::SessionExpired;
    }
    if (containsNoCase(t, "api key") || containsNoCase(t, "api_key") || containsNoCase(t, "permission") ||
        containsNoCase(t, "unauthenticated") || containsNoCase(t, "PERMISSION_DENIED")) {
        return Failure::Auth;
    }
    if (containsNoCase(t, "quota") || containsNoCase(t, "RESOURCE_EXHAUSTED") || containsNoCase(t, "rate limit") ||
        containsNoCase(t, "too many requests")) {
        return Failure::Quota;
    }
    if (containsNoCase(t, "not found") || containsNoCase(t, "NOT_FOUND") || containsNoCase(t, "not supported") ||
        containsNoCase(t, "is not available") || containsNoCase(t, "unsupported")) {
        return Failure::NotFound;
    }
    if (containsNoCase(t, "UNAVAILABLE") || containsNoCase(t, "overloaded") || containsNoCase(t, "internal") ||
        containsNoCase(t, "deadline")) {
        return Failure::Unavailable;
    }
    return Failure::None;
}

}  // namespace

const char* failureName(Failure f) {
    switch (f) {
        case Failure::Transient: return "transient";
        case Failure::Unavailable: return "unavailable";
        case Failure::Quota: return "quota";
        case Failure::NotFound: return "not_found";
        case Failure::Auth: return "auth";
        case Failure::SessionExpired: return "session_expired";
        default: return "none";
    }
}

Failure classifyClose(int code, const char* reason) {
    const Failure byText = classifyText(reason);
    if (byText != Failure::None) return byText;
    switch (code) {
        case 1000: return Failure::None;         // normal close
        case 1007: return Failure::Unavailable;  // invalid argument (model-specific setup field): retry later
        // policy violation: real "model not found" closes say so in the reason (rule above); a bare
        // 1008 like "The operation was aborted." (seen 2026-10-15) is a server-side hiccup
        case 1008: return Failure::Transient;
        case 1011: return Failure::Unavailable;  // server internal error
        case 1013: return Failure::Unavailable;  // try again later
        default: return Failure::Transient;
    }
}

Failure classifyHttp(int status, const char* body) {
    if (status == 200) return Failure::None;
    const Failure byText = classifyText(body);
    if (status == 401 || status == 403) return Failure::Auth;
    if (status == 429) return Failure::Quota;
    if (status == 404) return Failure::NotFound;
    if (status == 400) return byText == Failure::None ? Failure::NotFound : byText;
    if (status >= 500) return Failure::Unavailable;
    if (byText != Failure::None) return byText;
    return Failure::Transient;
}

ModelSelector::ModelSelector(size_t count) : _count(count > kMax ? kMax : count) {}

void ModelSelector::setPreferred(uint8_t model) {
    if (model < _count) _preferred = model;
}

bool ModelSelector::coolingDown(uint8_t m, uint32_t now) const { return m < _count && now < _until[m]; }

uint32_t ModelSelector::cooldownLeft(uint8_t m, uint32_t now) const {
    return coolingDown(m, now) ? _until[m] - now : 0;
}

bool ModelSelector::allCoolingDown(uint32_t now) const {
    for (size_t k = 0; k < _count; k++) {
        if (!coolingDown((uint8_t)k, now)) return false;
    }
    return _count > 0;
}

uint32_t ModelSelector::nextRecoveryIn(uint32_t now) const {
    uint32_t best = UINT32_MAX;
    for (size_t k = 0; k < _count; k++) {
        const uint32_t left = cooldownLeft((uint8_t)k, now);
        if (left < best) best = left;
    }
    return _count ? best : 0;
}

uint8_t ModelSelector::active(uint32_t now) const {
    if (!coolingDown(_preferred, now)) return _preferred;
    for (size_t k = 0; k < _count; k++) {
        if (k != _preferred && !coolingDown((uint8_t)k, now)) return (uint8_t)k;
    }
    // everything is cooling down: use the one that recovers first
    uint8_t best = _preferred;
    for (size_t k = 0; k < _count; k++) {
        if (_until[k] < _until[best]) best = (uint8_t)k;
    }
    return best;
}

bool ModelSelector::reportFailure(uint8_t m, Failure f, uint32_t now) {
    if (m >= _count || f == Failure::None || f == Failure::Auth || f == Failure::SessionExpired) return false;
    const uint8_t before = active(now);
    uint32_t cooldown = 0;
    switch (f) {
        case Failure::Transient:
            if (++_transient[m] >= 3) {  // three in a row: probably not a network blip
                cooldown = 120;
                _transient[m] = 0;
            }
            break;
        case Failure::Unavailable:
            cooldown = 300;
            break;
        case Failure::Quota: {
            // 10 min, 20 min, 40 min ... capped at 2 h
            const uint8_t s = _strikes[m] < 4 ? _strikes[m] : 4;
            cooldown = 600u << s;
            if (cooldown > 7200) cooldown = 7200;
            _strikes[m]++;
            break;
        }
        case Failure::NotFound:
            cooldown = 24 * 3600;
            break;
        default:
            break;
    }
    // never shorten a running cool-down (e.g. a transient retry after a quota error)
    if (cooldown && now + cooldown > _until[m]) _until[m] = now + cooldown;
    _last = f;
    return active(now) != before;
}

void ModelSelector::reportSuccess(uint8_t m) {
    if (m >= _count) return;
    _transient[m] = 0;
    _strikes[m] = 0;
    _until[m] = 0;
}

void ModelSelector::reset() {
    for (size_t k = 0; k < kMax; k++) reportSuccess((uint8_t)k);
    _last = Failure::None;
}

}  // namespace fallback
