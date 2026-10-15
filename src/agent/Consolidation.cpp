/**
 * @file Consolidation.cpp
 * @brief Prompt, schema, parser and rate limiter for memory consolidation
 */

#include "Consolidation.h"

#include <string.h>

#include "../util/TimeFmt.h"

namespace consolidate {

const char* systemPrompt() {
    return "You maintain the long-term memory of a personal voice assistant. "
           "From the conversation, extract only NEW durable information about the user and their world: "
           "identity, preferences, health/allergies, people and pets, places, work, plans and appointments, "
           "things the user asked to remember. Ignore greetings, small talk, the assistant's own statements, "
           "questions, and temporary states. Do not repeat the known memories. "
           "Write facts as one self-contained third-person sentence in the conversation's language "
           "(Thai: start with 'ผู้ใช้'). importance 1-5 (5 = essential). "
           "Use relations for links between named entities. Return empty arrays if there is nothing new.";
}

void buildSchema(JsonObject schema) {
    schema["type"] = "OBJECT";
    JsonObject props = schema["properties"].to<JsonObject>();

    JsonObject facts = props["facts"].to<JsonObject>();
    facts["type"] = "ARRAY";
    JsonObject fi = facts["items"].to<JsonObject>();
    fi["type"] = "OBJECT";
    JsonObject fp = fi["properties"].to<JsonObject>();
    fp["content"]["type"] = "STRING";
    JsonObject cat = fp["category"].to<JsonObject>();
    cat["type"] = "STRING";
    JsonArray ce = cat["enum"].to<JsonArray>();
    for (size_t i = 0; i < (size_t)ltm::Category::Count; i++) ce.add(ltm::categoryName((ltm::Category)i));
    fp["importance"]["type"] = "INTEGER";
    JsonArray fr = fi["required"].to<JsonArray>();
    fr.add("content");
    fr.add("category");
    fr.add("importance");

    JsonObject rel = props["relations"].to<JsonObject>();
    rel["type"] = "ARRAY";
    JsonObject ri = rel["items"].to<JsonObject>();
    ri["type"] = "OBJECT";
    JsonObject rp = ri["properties"].to<JsonObject>();
    rp["subject"]["type"] = "STRING";
    rp["relation"]["type"] = "STRING";
    rp["object"]["type"] = "STRING";
    JsonArray rr = ri["required"].to<JsonArray>();
    rr.add("subject");
    rr.add("relation");
    rr.add("object");

    JsonArray req = schema["required"].to<JsonArray>();
    req.add("facts");
    req.add("relations");
}

bool buildUserPrompt(const PsString& coreMemory, const std::vector<ltm::Turn>& turns, size_t minUserTurns,
                     size_t maxBytes, PsString& out) {
    size_t userTurns = 0;
    for (const ltm::Turn& t : turns) userTurns += t.role == 'u';
    if (userTurns < minUserTurns) return false;

    out = "Known memories:\n";
    out += coreMemory.empty() ? PsString("(none)\n") : coreMemory;
    out += "\nConversation:\n";
    // keep the newest turns that fit
    size_t used = out.size();
    size_t first = turns.size();
    for (size_t k = turns.size(); k-- > 0;) {
        const size_t len = turns[k].text.size() + 8;
        if (used + len > maxBytes) break;
        used += len;
        first = k;
    }
    for (size_t i = first; i < turns.size(); i++) {
        out += turns[i].role == 'a' ? "Assistant: " : "User: ";
        out += turns[i].text;
        out += "\n";
    }
    return first < turns.size();
}

bool parseResult(const char* json, size_t len, std::vector<Fact>& facts, std::vector<Relation>& relations) {
    facts.clear();
    relations.clear();
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    for (JsonObjectConst f : doc["facts"].as<JsonArrayConst>()) {
        const char* c = f["content"] | "";
        if (strlen(c) < 6) continue;
        Fact x;
        x.content = c;
        x.category = ltm::parseCategory(f["category"] | "other");
        const int imp = f["importance"] | 3;
        x.importance = (uint8_t)(imp < 1 ? 1 : (imp > 5 ? 5 : imp));
        facts.push_back(std::move(x));
        if (facts.size() >= 12) break;  // a runaway answer should not flood memory
    }
    for (JsonObjectConst r : doc["relations"].as<JsonArrayConst>()) {
        Relation x;
        x.subject = r["subject"] | "";
        x.relation = r["relation"] | "";
        x.object = r["object"] | "";
        if (x.subject.empty() || x.relation.empty() || x.object.empty()) continue;
        relations.push_back(std::move(x));
        if (relations.size() >= 12) break;
    }
    return true;
}

bool RateLimiter::allowed(uint32_t now) const {
    if (now < timefmt::kMinValidEpoch || now < _blockedUntil) return false;
    if (_last && now - _last < _interval) return false;
    return usedToday(now) < _maxPerDay;
}

void RateLimiter::record(uint32_t now) {
    const int64_t day = timefmt::localDay(now, _off);
    if (day != _day) {
        _day = day;
        _count = 0;
    }
    _count++;
    _last = now;
}

uint32_t RateLimiter::usedToday(uint32_t now) const {
    return timefmt::localDay(now, _off) == _day ? _count : 0;
}

}  // namespace consolidate
