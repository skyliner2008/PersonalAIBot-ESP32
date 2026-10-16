/**
 * @file EpisodicLog.h
 * @brief Layer 1-2 memory: working memory (RAM) + daily conversation logs
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * Every completed turn ("u" = user, "a" = assistant) is appended to
 *   <dir>/YYYY-MM-DD.jsonl   {"t":1760432000,"r":"u","x":"..."}
 * The most recent turns are also kept in RAM so a brand-new Gemini session
 * can be primed with "what we talked about last time".
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <vector>

#include "AppendSink.h"
#include "TextFeatures.h"

namespace ltm {

struct Turn {
    uint32_t ts = 0;
    char role = 'u';  // 'u' user, 'a' assistant
    PsString text;
};

class EpisodicLog {
public:
    struct Options {
        size_t workingTurns = 24;     // turns kept in RAM
        size_t maxTurnBytes = 1500;   // longer turns are truncated
        int32_t utcOffsetSec = 7 * 3600;
        // search() reads the daily files on the agent task (the assistant's voice goes through that
        // loop): stop going further back once this much was read (today is always read)
        size_t maxScanBytes = 384 * 1024;
    };

    EpisodicLog();
    explicit EpisodicLog(const Options& options);

    /** Use `dir` (must exist) and preload recent turns from the latest log files. */
    bool open(const char* dir, AppendSink* sink, uint32_t now);

    /**
     * The clock got synced after open() (which could only read the undated file): preload
     * yesterday's and today's tails, and keep the turns recorded since open() after them - opening
     * again dropped what was said before the sync from the next session's context.
     */
    void reloadDays(uint32_t now);

    /** Record a finished turn (RAM + async file append). Empty text is ignored. */
    void append(char role, const char* text, uint32_t ts);

    /** Most recent turns, oldest first. */
    size_t recent(size_t n, std::vector<Turn>& out) const;

    /**
     * Keyword search over the last `daysBack` daily files (and the undated
     * file), best matches first. Empty query -> most recent turns. Older days are skipped once
     * Options::maxScanBytes were read; `daysScanned` (optional) tells how many days were searched.
     */
    size_t search(const char* query, int daysBack, size_t limit, uint32_t now,
                  std::vector<Turn>& out, int* daysScanned = nullptr) const;

    /** Delete daily files older than keepDays. Returns files removed. */
    size_t prune(int keepDays, uint32_t now);

    /** Delete every log file and the working memory. Returns files removed. */
    size_t clearAll();

    /** "role: text" lines of the most recent turns, newest last, within maxBytes. */
    size_t buildRecentContext(PsString& out, size_t maxTurns, size_t maxBytes,
                              const char* userLabel, const char* agentLabel) const;

    size_t workingSize() const { return _working.size(); }

private:
    Options _opt;
    PsString _dir;
    AppendSink* _sink = nullptr;
    std::deque<Turn> _working;
    size_t _appended = 0;  // turns appended since open() (reloadDays keeps them)

    void filePathFor(uint32_t ts, char* buf, size_t cap) const;
    void loadTail(const char* path, size_t maxTurns);
};

}  // namespace ltm
