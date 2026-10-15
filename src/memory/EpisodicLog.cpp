/**
 * @file EpisodicLog.cpp
 * @brief Working memory + daily JSONL conversation logs with keyword search
 */

#include "EpisodicLog.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "../util/TimeFmt.h"
#include "../util/Utf8.h"

#if defined(_WIN32)
#include <filesystem>
#else
#include <dirent.h>
#endif

namespace ltm {

namespace {

constexpr size_t kLineBufferSize = 6144;
constexpr float kMinCoverage = 0.34f;   // share of query features a turn must contain
constexpr size_t kMaxCandidates = 256;  // bound memory while scanning many files

struct Candidate {
    Turn turn;
    float score;
};

PsString clampTurn(const char* s, size_t maxBytes) {
    if (!s) return PsString();
    const size_t len = strlen(s);
    size_t a = 0;
    while (a < len && isspace((unsigned char)s[a])) a++;
    size_t b = len;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return PsString(s + a, utf8::clampToBoundary(s + a, b - a, maxBytes));
}

bool parseTurnLine(char* line, size_t len, Turn& out) {
    JsonDocument doc;
    if (deserializeJson(doc, line, len)) return false;
    const char* text = doc["x"] | "";
    if (!*text) return false;
    out.ts = doc["t"] | 0u;
    const char* role = doc["r"] | "u";
    out.role = role[0] == 'a' ? 'a' : 'u';
    out.text = text;
    return true;
}

// Calls fn(line, len) for every non-empty line of the file.
template <class Fn>
void forEachLine(const char* path, Fn fn) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char* buf = (char*)psramMalloc(kLineBufferSize);
    if (!buf) {
        fclose(f);
        return;
    }
    while (fgets(buf, (int)kLineBufferSize, f)) {
        size_t n = strlen(buf);
        const bool complete = n > 0 && buf[n - 1] == '\n';
        if (!complete && !feof(f)) {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
            }
            continue;
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n > 0) fn(buf, n);
    }
    psramFree(buf);
    fclose(f);
}

// Parses "YYYY-MM-DD.jsonl"; returns false for other names.
bool parseDayFile(const char* name, int64_t& day) {
    int y, m, d;
    char ext[8] = {0};
    if (strlen(name) != 16) return false;
    if (sscanf(name, "%4d-%2d-%2d.%5s", &y, &m, &d, ext) != 4) return false;
    if (strcmp(ext, "jsonl") != 0 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    day = timefmt::daysFromCivil(y, m, d);
    return true;
}

}  // namespace

EpisodicLog::EpisodicLog() : EpisodicLog(Options()) {}

EpisodicLog::EpisodicLog(const Options& options) : _opt(options) {}

void EpisodicLog::filePathFor(uint32_t ts, char* buf, size_t cap) const {
    if (ts < timefmt::kMinValidEpoch) {
        snprintf(buf, cap, "%s/undated.jsonl", _dir.c_str());
        return;
    }
    const timefmt::Civil c = timefmt::toCivil(ts, _opt.utcOffsetSec);
    snprintf(buf, cap, "%s/%04d-%02d-%02d.jsonl", _dir.c_str(), c.year, c.month, c.day);
}

void EpisodicLog::loadTail(const char* path, size_t maxTurns) {
    std::deque<Turn> tail;
    forEachLine(path, [&](char* line, size_t len) {
        Turn t;
        if (!parseTurnLine(line, len, t)) return;
        tail.push_back(std::move(t));
        if (tail.size() > maxTurns) tail.pop_front();
    });
    for (Turn& t : tail) {
        _working.push_back(std::move(t));
        if (_working.size() > _opt.workingTurns) _working.pop_front();
    }
}

bool EpisodicLog::open(const char* dir, AppendSink* sink, uint32_t now) {
    _dir = dir ? dir : "";
    _sink = sink;
    _working.clear();
    if (_dir.empty()) return false;

    char path[160];
    if (now >= timefmt::kMinValidEpoch) {
        for (int d = 1; d >= 0; d--) {  // yesterday, then today
            filePathFor(now - (uint32_t)d * 86400u, path, sizeof(path));
            loadTail(path, _opt.workingTurns);
        }
    } else {
        filePathFor(0, path, sizeof(path));
        loadTail(path, _opt.workingTurns);
    }
    return true;
}

void EpisodicLog::append(char role, const char* text, uint32_t ts) {
    Turn t;
    t.ts = ts;
    t.role = role == 'a' ? 'a' : 'u';
    t.text = clampTurn(text, _opt.maxTurnBytes);
    if (t.text.empty()) return;

    if (_sink && !_dir.empty()) {
        JsonDocument doc;
        doc["t"] = t.ts;
        doc["r"] = t.role == 'a' ? "a" : "u";
        doc["x"] = t.text.c_str();
        const size_t need = measureJson(doc);
        PsString line;
        line.resize(need + 1);
        const size_t n = serializeJson(doc, &line[0], line.size());
        char path[160];
        filePathFor(t.ts, path, sizeof(path));
        _sink->appendLine(path, line.c_str(), n);
    }

    _working.push_back(std::move(t));
    while (_working.size() > _opt.workingTurns) _working.pop_front();
}

size_t EpisodicLog::recent(size_t n, std::vector<Turn>& out) const {
    out.clear();
    const size_t start = _working.size() > n ? _working.size() - n : 0;
    for (size_t i = start; i < _working.size(); i++) out.push_back(_working[i]);
    return out.size();
}

size_t EpisodicLog::search(const char* query, int daysBack, size_t limit, uint32_t now,
                           std::vector<Turn>& out) const {
    out.clear();
    if (limit == 0) return 0;

    features::HashSet q;
    if (query) features::extract(query, strlen(query), q);
    if (q.empty()) return recent(limit, out);

    if (daysBack < 1) daysBack = 1;
    if (daysBack > 90) daysBack = 90;

    std::vector<Candidate> cands;
    features::HashSet feats;

    auto consider = [&](const Turn& t) {
        features::extract(t.text.data(), t.text.size(), feats);
        const size_t matched = features::intersectCount(q, feats);
        const float coverage = (float)matched / (float)q.size();
        if (coverage < kMinCoverage) return;
        for (const Candidate& c : cands) {  // same turn seen in file and in RAM
            if (c.turn.ts == t.ts && c.turn.role == t.role && c.turn.text == t.text) return;
        }
        cands.push_back(Candidate{t, coverage});
        if (cands.size() > kMaxCandidates) {
            std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.turn.ts > b.turn.ts;
            });
            cands.resize(kMaxCandidates / 2);
        }
    };

    char path[160];
    auto scanFile = [&](const char* p) {
        forEachLine(p, [&](char* line, size_t len) {
            Turn t;
            if (parseTurnLine(line, len, t)) consider(t);
        });
    };

    if (now >= timefmt::kMinValidEpoch) {
        for (int d = 0; d < daysBack; d++) {
            filePathFor(now - (uint32_t)d * 86400u, path, sizeof(path));
            scanFile(path);
        }
    }
    filePathFor(0, path, sizeof(path));  // turns recorded before the clock was synced
    scanFile(path);

    // Turns still queued for the SD card are only in RAM
    for (const Turn& t : _working) consider(t);

    std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.turn.ts > b.turn.ts;
    });
    for (size_t i = 0; i < cands.size() && i < limit; i++) out.push_back(cands[i].turn);
    return out.size();
}

namespace {
// Lists the *.jsonl files of `dir`; keepIf(name) decides which ones are returned.
template <class Fn>
std::vector<PsString> listLogFiles(const PsString& dir, Fn select) {
    std::vector<PsString> out;
#if defined(_WIN32)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir.c_str(), ec)) {
        const std::string name = entry.path().filename().string();
        if (select(name.c_str())) out.push_back(PsString(entry.path().string().c_str()));
    }
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (select(e->d_name)) out.push_back(dir + "/" + e->d_name);
    }
    closedir(d);
#endif
    return out;
}
}  // namespace

size_t EpisodicLog::prune(int keepDays, uint32_t now) {
    if (_dir.empty() || now < timefmt::kMinValidEpoch || keepDays < 1) return 0;
    const int64_t today = timefmt::localDay(now, _opt.utcOffsetSec);
    const auto victims = listLogFiles(_dir, [&](const char* name) {
        int64_t day;
        return parseDayFile(name, day) && today - day > keepDays;
    });
    size_t removed = 0;
    for (const PsString& v : victims) {
        if (::remove(v.c_str()) == 0) removed++;
    }
    return removed;
}

size_t EpisodicLog::clearAll() {
    _working.clear();
    if (_dir.empty()) return 0;
    // Only our own files: YYYY-MM-DD.jsonl and undated.jsonl (never ltm.jsonl etc.)
    const auto victims = listLogFiles(_dir, [](const char* name) {
        int64_t day;
        return parseDayFile(name, day) || strcmp(name, "undated.jsonl") == 0;
    });
    size_t removed = 0;
    for (const PsString& v : victims) {
        if (::remove(v.c_str()) == 0) removed++;
    }
    return removed;
}

size_t EpisodicLog::buildRecentContext(PsString& out, size_t maxTurns, size_t maxBytes,
                                       const char* userLabel, const char* agentLabel) const {
    out.clear();
    const size_t n = std::min(maxTurns, _working.size());
    // Walk backwards to keep the newest turns that fit, then emit chronologically
    size_t used = 0;
    size_t first = _working.size();
    for (size_t k = 0; k < n; k++) {
        const Turn& t = _working[_working.size() - 1 - k];
        const size_t lineLen = strlen(t.role == 'a' ? agentLabel : userLabel) + 2 + t.text.size() + 1;
        if (used + lineLen > maxBytes) break;
        used += lineLen;
        first = _working.size() - 1 - k;
    }
    size_t count = 0;
    for (size_t i = first; i < _working.size(); i++) {
        const Turn& t = _working[i];
        out += t.role == 'a' ? agentLabel : userLabel;
        out += ": ";
        out += t.text;
        out += "\n";
        count++;
    }
    return count;
}

}  // namespace ltm
