/**
 * @file ModelFallback.h
 * @brief Automatic model fallback with per-model cool-downs
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 *
 * Live models   : preferred -> gemini-3.8-live -> 3.8-live-extended-thinking -> 3.1-flash-live-preview
 * Text models   : preferred -> gemini-3.5-flash-lite -> gemini-3.1-flash-lite
 *
 * A failing model is put on a cool-down that depends on why it failed
 * (quota, overloaded, not found, repeated transient errors) and the next
 * model in the list is used. When the preferred model's cool-down ends it is
 * tried again automatically. Authentication errors are not model problems and
 * never trigger a switch. When every model is cooling down (typically a quota
 * error on the whole API key) the agent pauses instead of cycling through them.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fallback {

enum class Failure : uint8_t {
    None,
    Transient,    // network blip, early disconnect: switch only after repeats
    Unavailable,  // 503 / overloaded / internal error
    Quota,        // 429 / RESOURCE_EXHAUSTED / rate limit
    NotFound,     // model id not available for this key / unsupported
    Auth,         // invalid API key / permission: switching would not help
    SessionExpired,  // resumption handle no longer valid: start a fresh session, model is fine
};

const char* failureName(Failure f);

/** Classify a WebSocket close (code + reason text from the server). */
Failure classifyClose(int code, const char* reason);
/** Classify an HTTP status (+ body) from generateContent. */
Failure classifyHttp(int status, const char* body);

class ModelSelector {
public:
    static constexpr size_t kMax = 4;

    /** `count` models identified by index 0..count-1, tried in index order after the preferred one. */
    explicit ModelSelector(size_t count);

    void setPreferred(uint8_t model);
    uint8_t preferred() const { return _preferred; }

    /** Model to use now (preferred if usable, else the next usable one). */
    uint8_t active(uint32_t nowSec) const;
    bool usingFallback(uint32_t nowSec) const { return active(nowSec) != _preferred; }

    /** Returns true if this report changed which model is active. */
    bool reportFailure(uint8_t model, Failure f, uint32_t nowSec);
    void reportSuccess(uint8_t model);
    /** Forget all cool-downs (new API key). */
    void reset();

    bool coolingDown(uint8_t model, uint32_t nowSec) const;
    uint32_t cooldownLeft(uint8_t model, uint32_t nowSec) const;

    /** Every model is cooling down: the failure is not model-specific (e.g. the key's
     *  quota), so the caller should stop retrying until nextRecoveryIn() seconds pass. */
    bool allCoolingDown(uint32_t nowSec) const;
    uint32_t nextRecoveryIn(uint32_t nowSec) const;
    Failure lastFailure() const { return _last; }

private:
    size_t _count;
    uint8_t _preferred = 0;
    Failure _last = Failure::None;
    uint32_t _until[kMax] = {};
    uint8_t _transient[kMax] = {};
    uint8_t _strikes[kMax] = {};  // escalates quota cool-downs
};

}  // namespace fallback
