/**
 * @file AppendSink.h
 * @brief Abstraction for appending lines to memory log files
 *
 * The memory store writes one JSON line per change. On the device the sink
 * hands lines to a low-priority writer task (SD card latency must never
 * stall the conversation); on the host and during boot it writes directly.
 */

#pragma once

#include <stddef.h>
#include <stdio.h>

class AppendSink {
public:
    virtual ~AppendSink() = default;
    /** Append `line` (without newline) to the file at `path`. */
    virtual bool appendLine(const char* path, const char* line, size_t len) = 0;
};

/** Synchronous stdio implementation (works on ESP32 VFS paths and on the host). */
class DirectFileSink : public AppendSink {
public:
    bool appendLine(const char* path, const char* line, size_t len) override {
        FILE* f = fopen(path, "ab");
        if (!f) return false;
        const bool ok = fwrite(line, 1, len, f) == len && fputc('\n', f) != EOF;
        fclose(f);
        return ok;
    }

    static DirectFileSink* instance() {
        static DirectFileSink sink;
        return &sink;
    }
};
