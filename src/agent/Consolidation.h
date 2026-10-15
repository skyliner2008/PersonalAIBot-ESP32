/**
 * @file Consolidation.h
 * @brief Background memory consolidation with a text model (Gemini Flash-Lite)
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * The live model saves memories itself, but it sometimes forgets to. After a
 * conversation goes quiet, the recent turns are sent to Flash-Lite (cheap text
 * model: 15 RPM / 500 RPD on this account) which extracts durable facts and
 * relations as JSON; MemoryStore merges near-duplicates, so re-extracting a
 * known fact only refreshes it.
 *
 * This file holds the pure parts (prompt, schema, parsing, rate limiting) so
 * they can be unit-tested on the host.
 */

#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include <vector>

#include "../memory/EpisodicLog.h"
#include "../memory/MemoryStore.h"
#include "../util/PsramAlloc.h"

namespace consolidate {

struct Fact {
    PsString content;
    ltm::Category category = ltm::Category::Other;
    uint8_t importance = 3;
};

struct Relation {
    PsString subject, relation, object;
};

/** System instruction for the extractor. */
const char* systemPrompt();

/** Fill generationConfig.responseSchema for {facts:[...], relations:[...]}. */
void buildSchema(JsonObject schema);

/**
 * User prompt: known core memories + the conversation turns.
 * Returns false if there is nothing worth sending (fewer than minTurns user turns).
 */
bool buildUserPrompt(const PsString& coreMemory, const std::vector<ltm::Turn>& turns, size_t minUserTurns,
                     size_t maxBytes, PsString& out);

/** Parse the model's JSON answer. Invalid entries are skipped. */
bool parseResult(const char* json, size_t len, std::vector<Fact>& facts, std::vector<Relation>& relations);

/** Simple limiter: minimum spacing + daily cap (local day). */
class RateLimiter {
public:
    RateLimiter(uint32_t minIntervalSec, uint32_t maxPerDay, int32_t utcOffsetSec)
        : _interval(minIntervalSec), _maxPerDay(maxPerDay), _off(utcOffsetSec) {}
    bool allowed(uint32_t now) const;
    void record(uint32_t now);
    /** Back off after an error (e.g. HTTP 429). */
    void backoff(uint32_t now, uint32_t seconds) { _blockedUntil = now + seconds; }
    uint32_t usedToday(uint32_t now) const;

private:
    uint32_t _interval, _maxPerDay;
    int32_t _off;
    uint32_t _last = 0, _blockedUntil = 0;
    int64_t _day = -1;
    uint32_t _count = 0;
};

}  // namespace consolidate
