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

    /**
     * T125: an SD card read / write / open failed (any task). Seen on the board: the card stopped answering in the
     * middle of a song (sdmmc 0x107 timeouts) and every file op failed until a reboot - songs, the episode log, memory.
     */
    static void reportError();
    /** Console "sdremount": mount the card again on the next poll() even if it answers (tests the recovery). */
    static void requestRemount();
    /** loop(): after reported errors, read sector 0 - a card that no longer answers is mounted again (same paths). */
    void poll(uint32_t now);
    uint32_t remounts() const { return _remounts; }

private:
    StorageKind _kind = StorageKind::None;
    char _agentDir[48] = "";
    char _episodesDir[64] = "";
    static volatile uint32_t s_errors;
    static volatile bool s_forceRemount;
    uint32_t _seenErrors = 0, _nextCheck = 0, _remounts = 0;
    uint8_t _failedTries = 0;
    bool _sd4bit = true;

    bool mountSd();
    bool mountFlash();
    bool makeDirs(fs::FS& fs, const char* mount);
};
