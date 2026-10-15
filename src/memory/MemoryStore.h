/**
 * @file MemoryStore.h
 * @brief Standalone long-term memory: semantic facts + entity knowledge graph
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * Everything lives on the device (SD card or internal LittleFS):
 *   - Facts      "ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาล"            (Layer 3: semantic memory)
 *   - Relations  (ผู้ใช้) -[มีสุนัขชื่อ]-> (โมจิ)              (Layer 4: knowledge graph)
 *
 * Retrieval = BM25 over character-bigram features (works for Thai without a
 * word segmenter) + 1-hop graph expansion from the entities that were hit
 * ("GraphRAG-lite"). Persistence is an append-only JSONL operation log that
 * is replayed and compacted at boot.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "AppendSink.h"
#include "TextFeatures.h"

namespace ltm {

enum class Kind : uint8_t { Fact = 0, Relation = 1 };

enum class Category : uint8_t {
    Profile = 0,  // name, age, job, home...
    Preference,   // likes / dislikes
    Person,       // family, friends, pets
    Event,        // past events, appointments
    Task,         // plans, todos, reminders
    Knowledge,    // general facts the user taught
    Other,
    Count
};

const char* categoryName(Category c);
Category parseCategory(const char* name);  // unknown -> Other
const char* categoryLabelTh(Category c);

struct Record {
    uint32_t id = 0;
    Kind kind = Kind::Fact;
    Category category = Category::Other;
    uint8_t importance = 3;  // 1..5
    uint32_t created = 0;    // unix epoch, 0 = unknown
    uint32_t updated = 0;

    PsString text;  // fact sentence, or "subject relation object"
    PsString subject, relation, object;  // relations only
    PsString normText, normSubject, normObject;

    features::HashSet feats;
    uint16_t length = 0;  // feature count before de-dup (BM25 doc length)
};

struct Hit {
    const Record* rec;
    float score;
    bool viaGraph;  // added by knowledge-graph expansion, not by text match
};

struct Stats {
    size_t facts = 0;
    size_t relations = 0;
    size_t logLines = 0;  // lines in the operation log at open()
};

class MemoryStore {
public:
    struct Options {
        size_t maxRecords = 2000;
        float mergeSimilarity = 0.75f;  // Jaccard above which a new fact updates an old one
        float minCoverage = 0.25f;      // share of (known) query IDF mass a hit must match
        size_t maxTextBytes = 600;
        size_t maxEntityBytes = 160;
        size_t hubDegree = 8;           // entities with more links are not expanded implicitly
    };

    MemoryStore();
    explicit MemoryStore(const Options& options);

    /** Load (replay) the log at `path`, compacting it when it has grown. */
    bool open(const char* path, AppendSink* sink);

    /** Add or merge a fact. Returns its id, or 0 if the text is empty/unusable. */
    uint32_t addFact(const char* text, Category category, uint8_t importance, uint32_t now,
                     bool* merged = nullptr);

    /** Add or merge a relation triple. Returns its id, or 0 if invalid. */
    uint32_t addRelation(const char* subject, const char* relation, const char* object,
                         Category category, uint8_t importance, uint32_t now,
                         bool* merged = nullptr);

    bool remove(uint32_t id);
    const Record* find(uint32_t id) const;

    /**
     * Ranked retrieval. Text hits come first (by score), followed by up to
     * `graphLimit` records reached through shared entities.
     */
    size_t search(const char* query, size_t limit, std::vector<Hit>& out,
                  size_t graphLimit = 6) const;

    /** Bullet list of the most important memories for the system prompt. */
    size_t buildCoreMemory(PsString& out, size_t maxBytes) const;

    /** Rewrite the log with only live records. */
    bool compact();

    /** Forget everything (empties the log file). */
    bool clearAll();

    size_t size() const { return _records.size(); }
    Stats stats() const;
    const Options& options() const { return _opt; }

private:
    Options _opt;
    PsVector<Record> _records;
    uint32_t _nextId = 1;
    PsString _path;
    AppendSink* _sink = nullptr;
    size_t _logLines = 0;

    int indexOf(uint32_t id) const;
    void index(Record& r);
    void persist(const Record& r, const char* op);
    void persistDelete(uint32_t id);
    void evictIfFull();
    bool applyLogLine(char* line, size_t len);
    size_t entityDegree(const PsString& normEntity) const;
};

}  // namespace ltm
