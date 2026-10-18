/**
 * @file JsonFile.h
 * @brief Load / atomically save small JSON documents (settings-like state files)
 *
 * Save writes "<path>.tmp" then replaces the original. FAT cannot rename over
 * an existing file, so the old file is removed first; load() falls back to the
 * .tmp copy if a power cut happened in between.
 */

#pragma once

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <string>

namespace jsonfile {

enum class Status : uint8_t {
    Ok,
    Missing,     // no file (or an empty one): nothing to lose
    Unreadable,  // there is data but it cannot be read (damaged, too big, card error): do not save over it
};

inline Status loadStatus(const char* path, JsonDocument& doc) {
    std::string p = path;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) {
        struct stat st;
        if (stat(p.c_str(), &st) == 0) return Status::Unreadable;  // there, but it will not open
        p += ".tmp";
        f = fopen(p.c_str(), "rb");
        if (!f) return Status::Missing;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 256 * 1024) {
        fclose(f);
        return size == 0 ? Status::Missing : Status::Unreadable;
    }
    std::string buf((size_t)size, '\0');
    const size_t n = fread(&buf[0], 1, (size_t)size, f);
    fclose(f);
    return n == (size_t)size && !deserializeJson(doc, buf) ? Status::Ok : Status::Unreadable;
}

inline bool load(const char* path, JsonDocument& doc) { return loadStatus(path, doc) == Status::Ok; }

inline bool save(const char* path, const JsonDocument& doc) {
    const std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    std::string out;
    serializeJson(doc, out);
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    if (fclose(f) != 0 || !ok) {
        ::remove(tmp.c_str());
        return false;
    }
    ::remove(path);
    return ::rename(tmp.c_str(), path) == 0;
}

}  // namespace jsonfile
