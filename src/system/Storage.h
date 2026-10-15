/**
 * @file Storage.h
 * @brief Mounts the memory storage: MicroSD (4-bit SDMMC) or internal LittleFS
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * Files are accessed through the ESP-IDF VFS with plain stdio (fopen etc.),
 * e.g. /sdcard/agent/ltm.jsonl, so the memory code stays portable.
 */

#pragma once

#include <Arduino.h>
#include <FS.h>

enum class StorageKind : uint8_t { None, SdCard, Flash };

class Storage {
public:
    /** Try SD (4-bit, then 1-bit), fall back to LittleFS on the "spiffs" partition. */
    bool begin();

    StorageKind kind() const { return _kind; }
    const char* kindName() const;
    /** VFS directory for agent data, e.g. "/sdcard/agent" (no trailing slash). */
    const char* agentDir() const { return _agentDir; }
    const char* episodesDir() const { return _episodesDir; }
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

private:
    StorageKind _kind = StorageKind::None;
    char _agentDir[48] = "";
    char _episodesDir[64] = "";

    bool mountSd();
    bool mountFlash();
    bool makeDirs(fs::FS& fs, const char* mount);
};
