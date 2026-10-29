/**
 * @file MemoryStore.cpp
 * @brief Facts + knowledge graph with BM25 retrieval and JSONL persistence
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 */

#include "MemoryStore.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "../util/JsonAlloc.h"
#include "../util/Utf8.h"

namespace ltm {

namespace {

const char* const kCategoryNames[] = {"profile", "preference", "person", "event",
                                      "task", "knowledge", "other"};
const char* const kCategoryLabels[] = {"ข้อมูลส่วนตัว", "ความชอบ", "บุคคล", "เหตุการณ์",
                                       "งาน/แผน", "ความรู้", "อื่นๆ"};

constexpr float kBm25K1 = 1.2f;
constexpr float kBm25B = 0.5f;  // short documents: weaker length normalisation
constexpr float kMinMatchedIdf = 0.5f;
constexpr size_t kLineBufferSize = 4096;

PsString clampText(const char* s, size_t maxBytes) {
    if (!s) return PsString();
    const size_t len = strlen(s);
    size_t a = 0;
    while (a < len && isspace((unsigned char)s[a])) a++;
    size_t b = len;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    const size_t n = utf8::clampToBoundary(s + a, b - a, maxBytes);
    return PsString(s + a, n);
}

uint8_t clampImportance(int v) { return (uint8_t)(v < 1 ? 1 : (v > 5 ? 5 : v)); }

// Words for "the user / me" connect to almost every memory; expanding the
// graph through them would return everything, so they are never used as seeds.
bool isSelfEntity(const PsString& norm) {
    static const char* const kSelf[] = {"ผู้ใช้", "ผู้ใช้งาน", "ฉัน", "ผม", "ดิฉัน", "เรา",
                                        "user", "me", "i", nullptr};
    static std::vector<PsString> normalized;  // same normalisation as entity names
    if (normalized.empty()) {
        for (const char* const* s = kSelf; *s; s++) {
            PsString n;
            features::normalize(*s, strlen(*s), n);
            normalized.push_back(n);
        }
    }
    for (const PsString& s : normalized) {
        if (norm == s) return true;
    }
    return false;
}

bool newerFirst(const Record* a, const Record* b) {
    if (a->importance != b->importance) return a->importance > b->importance;
    return a->updated > b->updated;
}

size_t serializeRecord(const Record& r, const char* op, char* buf, size_t cap) {
    JsonDocument doc(JsonPsramAllocator::instance());
    doc["op"] = op;
    doc["id"] = r.id;
    doc["k"] = r.kind == Kind::Fact ? "f" : "r";
    doc["c"] = categoryName(r.category);
    doc["i"] = r.importance;
    doc["t"] = r.created;
    doc["u"] = r.updated;
    if (r.kind == Kind::Fact) {
        doc["x"] = r.text.c_str();
    } else {
        doc["s"] = r.subject.c_str();
        doc["p"] = r.relation.c_str();
        doc["o"] = r.object.c_str();
    }
    const size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

}  // namespace

const char* categoryName(Category c) {
    const size_t i = (size_t)c;
    return i < (size_t)Category::Count ? kCategoryNames[i] : "other";
}

const char* categoryLabelTh(Category c) {
    const size_t i = (size_t)c;
    return i < (size_t)Category::Count ? kCategoryLabels[i] : kCategoryLabels[(size_t)Category::Other];
}

Category parseCategory(const char* name) {
    if (!name) return Category::Other;
    for (size_t i = 0; i < (size_t)Category::Count; i++) {
        if (strcmp(name, kCategoryNames[i]) == 0) return (Category)i;
    }
    return Category::Other;
}

MemoryStore::MemoryStore() : MemoryStore(Options()) {}

MemoryStore::MemoryStore(const Options& options) : _opt(options) {}

// ============================================================
// Indexing helpers
// ============================================================

int MemoryStore::indexOf(uint32_t id) const {
    for (size_t i = 0; i < _records.size(); i++) {
        if (_records[i].id == id) return (int)i;
    }
    return -1;
}

const Record* MemoryStore::find(uint32_t id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &_records[(size_t)i];
}

void MemoryStore::index(Record& r) {
    features::extract(r.text.data(), r.text.size(), r.feats, &r.length);
    features::normalize(r.text.data(), r.text.size(), r.normText);
    if (r.kind == Kind::Relation) {
        features::normalize(r.subject.data(), r.subject.size(), r.normSubject);
        features::normalize(r.object.data(), r.object.size(), r.normObject);
    }
}

size_t MemoryStore::entityDegree(const PsString& normEntity) const {
    size_t n = 0;
    for (const Record& r : _records) {
        if (r.kind == Kind::Relation && (r.normSubject == normEntity || r.normObject == normEntity)) n++;
    }
    return n;
}

// ============================================================
// Persistence
// ============================================================

void MemoryStore::persist(const Record& r, const char* op) {
    if (!_sink || _path.empty()) return;
    char buf[2048];
    const size_t n = serializeRecord(r, op, buf, sizeof(buf));
    if (n == 0) return;
    if (_sink->appendLine(_path.c_str(), buf, n)) _logLines++;
}

void MemoryStore::persistDelete(uint32_t id) {
    if (!_sink || _path.empty()) return;
    char buf[64];
    const int n = snprintf(buf, sizeof(buf), "{\"op\":\"del\",\"id\":%lu}", (unsigned long)id);
    if (n > 0 && _sink->appendLine(_path.c_str(), buf, (size_t)n)) _logLines++;
}

bool MemoryStore::applyLogLine(char* line, size_t len) {
    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, line, len)) return false;

    const char* op = doc["op"] | "";
    const uint32_t id = doc["id"] | 0u;
    if (id == 0) return false;

    if (strcmp(op, "del") == 0) {
        const int i = indexOf(id);
        if (i >= 0) _records.erase(_records.begin() + i);
        return true;
    }
    if (strcmp(op, "add") != 0 && strcmp(op, "upd") != 0) return false;

    Record r;
    r.id = id;
    r.kind = strcmp(doc["k"] | "f", "r") == 0 ? Kind::Relation : Kind::Fact;
    r.category = parseCategory(doc["c"] | "other");
    r.importance = clampImportance(doc["i"] | 3);
    r.created = doc["t"] | 0u;
    r.updated = doc["u"] | r.created;

    if (r.kind == Kind::Fact) {
        r.text = clampText(doc["x"] | "", _opt.maxTextBytes);
    } else {
        r.subject = clampText(doc["s"] | "", _opt.maxEntityBytes);
        r.relation = clampText(doc["p"] | "", _opt.maxEntityBytes);
        r.object = clampText(doc["o"] | "", _opt.maxEntityBytes);
        if (r.subject.empty() || r.relation.empty() || r.object.empty()) return false;
        r.text = r.subject + " " + r.relation + " " + r.object;
    }
    if (r.text.empty()) return false;
    index(r);

    const int i = indexOf(id);
    if (i >= 0) {
        _records[(size_t)i] = std::move(r);
    } else {
        _records.push_back(std::move(r));
    }
    if (id >= _nextId) _nextId = id + 1;
    return true;
}

bool MemoryStore::open(const char* path, AppendSink* sink) {
    _records.clear();
    _nextId = 1;
    _logLines = 0;
    _path = path ? path : "";
    _sink = sink;
    if (_path.empty()) return false;

    // Recover from a compaction that was interrupted after the old file was removed
    const PsString tmp = _path + ".tmp";
    FILE* f = fopen(_path.c_str(), "rb");
    if (!f) {
        FILE* t = fopen(tmp.c_str(), "rb");
        if (t) {
            fclose(t);
            ::rename(tmp.c_str(), _path.c_str());
            f = fopen(_path.c_str(), "rb");
        }
    }
    if (!f) return true;  // fresh store; the file is created on the first write

    char* buf = (char*)psramMalloc(kLineBufferSize);
    if (!buf) {
        fclose(f);
        return false;
    }
    while (fgets(buf, (int)kLineBufferSize, f)) {
        size_t n = strlen(buf);
        const bool complete = n > 0 && buf[n - 1] == '\n';
        if (!complete && !feof(f)) {  // over-long line: skip the remainder
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
            }
            continue;
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n == 0) continue;
        _logLines++;
        applyLogLine(buf, n);
    }
    fclose(f);
    psramFree(buf);

    while (_records.size() > _opt.maxRecords) evictIfFull();
    if (_logLines > _records.size() * 2 + 32) compact();
    return true;
}

bool MemoryStore::compact() {
    if (_path.empty()) return false;
    const PsString tmp = _path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;

    char buf[2048];
    bool ok = true;
    for (const Record& r : _records) {
        const size_t n = serializeRecord(r, "add", buf, sizeof(buf));
        if (n == 0) continue;
        if (fwrite(buf, 1, n, f) != n || fputc('\n', f) == EOF) {
            ok = false;
            break;
        }
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        ::remove(tmp.c_str());
        return false;
    }
    ::remove(_path.c_str());  // FAT cannot rename over an existing file
    if (::rename(tmp.c_str(), _path.c_str()) != 0) return false;
    _logLines = _records.size();
    return true;
}

bool MemoryStore::clearAll() {
    _records.clear();
    return compact();  // ids keep increasing (_nextId is not reset)
}

// ============================================================
// Mutations
// ============================================================

void MemoryStore::evictIfFull() {
    if (_records.size() < _opt.maxRecords || _records.empty()) return;
    size_t victim = 0;
    for (size_t i = 1; i < _records.size(); i++) {
        const Record& a = _records[i];
        const Record& v = _records[victim];
        if (a.importance < v.importance || (a.importance == v.importance && a.updated < v.updated)) {
            victim = i;
        }
    }
    const uint32_t id = _records[victim].id;
    _records.erase(_records.begin() + (long)victim);
    persistDelete(id);
}

uint32_t MemoryStore::addFact(const char* textIn, Category category, uint8_t importance,
                              uint32_t now, bool* merged) {
    if (merged) *merged = false;
    Record r;
    r.kind = Kind::Fact;
    r.text = clampText(textIn, _opt.maxTextBytes);
    if (r.text.empty()) return 0;
    index(r);
    if (r.feats.empty()) return 0;  // nothing searchable (e.g. only punctuation)
    importance = clampImportance(importance);

    // Near-duplicate of an existing fact? Update it instead of adding noise.
    int best = -1;
    float bestSim = 0.0f;
    for (size_t i = 0; i < _records.size(); i++) {
        if (_records[i].kind != Kind::Fact) continue;
        const float sim = features::jaccard(r.feats, _records[i].feats);
        if (sim > bestSim) {
            bestSim = sim;
            best = (int)i;
        }
    }
    if (best >= 0 && bestSim >= _opt.mergeSimilarity) {
        Record& old = _records[(size_t)best];
        old.text = std::move(r.text);
        old.normText = std::move(r.normText);
        old.feats = std::move(r.feats);
        old.length = r.length;
        if (category != Category::Other) old.category = category;
        old.importance = std::max(old.importance, importance);
        old.updated = now;
        persist(old, "upd");
        if (merged) *merged = true;
        return old.id;
    }

    evictIfFull();
    r.id = _nextId++;
    r.category = category;
    r.importance = importance;
    r.created = now;
    r.updated = now;
    _records.push_back(std::move(r));
    persist(_records.back(), "add");
    return _records.back().id;
}

uint32_t MemoryStore::addRelation(const char* subject, const char* relation, const char* object,
                                  Category category, uint8_t importance, uint32_t now,
                                  bool* merged) {
    if (merged) *merged = false;
    Record r;
    r.kind = Kind::Relation;
    r.subject = clampText(subject, _opt.maxEntityBytes);
    r.relation = clampText(relation, _opt.maxEntityBytes);
    r.object = clampText(object, _opt.maxEntityBytes);
    if (r.subject.empty() || r.relation.empty() || r.object.empty()) return 0;
    r.text = r.subject + " " + r.relation + " " + r.object;
    index(r);
    if (r.feats.empty()) return 0;
    importance = clampImportance(importance);

    for (Record& old : _records) {
        if (old.kind == Kind::Relation && old.normText == r.normText) {
            if (category != Category::Other) old.category = category;
            old.importance = std::max(old.importance, importance);
            old.updated = now;
            persist(old, "upd");
            if (merged) *merged = true;
            return old.id;
        }
    }

    evictIfFull();
    r.id = _nextId++;
    r.category = category;
    r.importance = importance;
    r.created = now;
    r.updated = now;
    _records.push_back(std::move(r));
    persist(_records.back(), "add");
    return _records.back().id;
}

bool MemoryStore::remove(uint32_t id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    _records.erase(_records.begin() + i);
    persistDelete(id);
    return true;
}

// ============================================================
// Retrieval
// ============================================================

size_t MemoryStore::search(const char* query, size_t limit, std::vector<Hit>& out,
                           size_t graphLimit) const {
    out.clear();
    if (!query || _records.empty() || limit == 0) return 0;
    const size_t qlen = strlen(query);

    features::HashSet q;
    features::extract(query, qlen, q);
    PsString normQuery;
    features::normalize(query, qlen, normQuery);

    // ---- 1. BM25 over bigram/word features ----
    if (!q.empty()) {
        const size_t n = _records.size();
        std::vector<uint32_t> df(q.size(), 0);
        double totalLen = 0.0;
        for (const Record& r : _records) {
            totalLen += r.length;
            size_t i = 0, j = 0;
            while (i < q.size() && j < r.feats.size()) {
                if (q[i] < r.feats[j]) {
                    i++;
                } else if (q[i] > r.feats[j]) {
                    j++;
                } else {
                    df[i]++;
                    i++;
                    j++;
                }
            }
        }
        const float avgLen = (float)std::max(1.0, totalLen / (double)n);

        // Coverage is measured only over query features that exist somewhere in
        // memory: filler words ("จำได้ไหม", "อะไร") must not hide a real match.
        std::vector<float> idf(q.size());
        float knownIdf = 0.0f;
        for (size_t i = 0; i < q.size(); i++) {
            idf[i] = logf(1.0f + ((float)n - (float)df[i] + 0.5f) / ((float)df[i] + 0.5f));
            if (df[i] > 0) knownIdf += idf[i];
        }

        for (const Record& r : _records) {
            float matchedIdf = 0.0f;
            size_t i = 0, j = 0;
            while (i < q.size() && j < r.feats.size()) {
                if (q[i] < r.feats[j]) {
                    i++;
                } else if (q[i] > r.feats[j]) {
                    j++;
                } else {
                    matchedIdf += idf[i];
                    i++;
                    j++;
                }
            }
            // Needs a meaningful share of the known query terms, and not only
            // very common bigrams (e.g. the ones of "ผู้ใช้" that every fact has)
            if (matchedIdf < kMinMatchedIdf || matchedIdf < _opt.minCoverage * knownIdf) continue;
            const float lengthNorm =
                (kBm25K1 + 1.0f) / (1.0f + kBm25K1 * (1.0f - kBm25B + kBm25B * r.length / avgLen));
            const float boost = 0.85f + 0.05f * r.importance;
            out.push_back(Hit{&r, matchedIdf * lengthNorm * boost, false});
        }

        std::sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.rec->updated > b.rec->updated;
        });
        if (out.size() > limit) out.resize(limit);
    }

    // ---- 2. Knowledge-graph expansion (1 hop) ----
    if (graphLimit == 0) return out.size();

    std::vector<const PsString*> seeds;
    auto addSeed = [&seeds](const PsString& e) {
        if (e.size() < 3 || isSelfEntity(e)) return;
        for (const PsString* s : seeds) {
            if (*s == e) return;
        }
        seeds.push_back(&e);
    };

    // Entities of relations that matched the text (skip hubs such as "ผู้ใช้")
    for (const Hit& h : out) {
        if (h.rec->kind != Kind::Relation) continue;
        if (entityDegree(h.rec->normSubject) <= _opt.hubDegree) addSeed(h.rec->normSubject);
        if (entityDegree(h.rec->normObject) <= _opt.hubDegree) addSeed(h.rec->normObject);
    }
    // Entities mentioned explicitly in the query (hubs allowed)
    for (const Record& r : _records) {
        if (r.kind != Kind::Relation) continue;
        if (!r.normSubject.empty() && normQuery.find(r.normSubject) != PsString::npos) addSeed(r.normSubject);
        if (!r.normObject.empty() && normQuery.find(r.normObject) != PsString::npos) addSeed(r.normObject);
    }
    if (seeds.empty()) return out.size();

    std::vector<const Record*> extra;
    for (const Record& r : _records) {
        bool already = false;
        for (const Hit& h : out) {
            if (h.rec == &r) {
                already = true;
                break;
            }
        }
        if (already) continue;

        bool linked = false;
        for (const PsString* s : seeds) {
            if (r.kind == Kind::Relation) {
                linked = (r.normSubject == *s || r.normObject == *s);
            } else {
                // facts mentioning the entity by name (at least ~2 Thai characters)
                linked = s->size() >= 6 && r.normText.find(*s) != PsString::npos;
            }
            if (linked) break;
        }
        if (linked) extra.push_back(&r);
    }
    std::sort(extra.begin(), extra.end(), newerFirst);
    for (size_t i = 0; i < extra.size() && i < graphLimit; i++) {
        out.push_back(Hit{extra[i], 0.0f, true});
    }
    return out.size();
}

size_t MemoryStore::buildCoreMemory(PsString& out, size_t maxBytes) const {
    out.clear();
    std::vector<const Record*> cand;
    for (const Record& r : _records) {
        if (r.importance >= 4 || r.category == Category::Profile) cand.push_back(&r);
    }
    std::sort(cand.begin(), cand.end(), newerFirst);

    size_t count = 0;
    for (const Record* r : cand) {
        const size_t lineLen = 2 + r->text.size() + 1;
        if (out.size() + lineLen > maxBytes) continue;  // a shorter one may still fit
        out += "- ";
        out += r->text;
        out += "\n";
        count++;
    }
    return count;
}

Stats MemoryStore::stats() const {
    Stats s;
    for (const Record& r : _records) {
        if (r.kind == Kind::Fact) s.facts++;
        else s.relations++;
    }
    s.logLines = _logLines;
    return s;
}

}  // namespace ltm
