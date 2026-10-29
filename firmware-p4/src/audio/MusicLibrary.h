/**
 * @file MusicLibrary.h
 * @brief MP3 player from the SD card: the song list of /sdcard/music, played through the radio's decoder
 * @see wiki/12-Internet-Radio.md (T122)
 *
 * Songs: *.mp3 / *.aac (ADTS) in /sdcard/music and one level of sub-folders, sorted by path, up to kMax.
 * Scanned at boot and on request (console "music rescan", the screen's MP3 tab while nothing plays).
 * The list lives in PSRAM. Playing goes through RadioPlayer::playFile() - the same task, decoder, mixer and
 * ducking as the Internet radio, so only one of the two plays at a time and no second decoder is held.
 * At the end of a song the next one starts (the list repeats). Thread-safe (UI, agent, console, radio task).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../util/PsramAlloc.h"

class MusicLibrary {
public:
    static constexpr size_t kMax = 500;

    /** `root` = the music folder ("" = no SD card: the player is off). Creates it and scans it. */
    void begin(const char* root);
    bool available() const { return _root[0] != '\0'; }
    const char* root() const { return _root; }

    /** Reads the folder again; the song playing keeps its place in the new list. Returns the song count. */
    size_t scan();
    size_t count() const;
    /** Song `i`: its name (from the file name) and the sub-folder it is in ("" = the music folder itself). */
    bool get(size_t i, char* name, size_t nameCap, char* folder = nullptr, size_t folderCap = 0) const;
    /** "3" (1-based), else words of the name / folder / file (case-insensitive for Latin). -1 if none. */
    int find(const char* query) const;

    /** Play song `i` (wraps around) from byte `offset` of its file (0 = the start). */
    bool play(int i, uint32_t offset = 0);
    /** Next / previous song (from the current one). */
    bool step(int dir);
    /** Pause = stop, remembering where; resume = from there (or the last song from the start). */
    void pause();
    bool resume();

    /** A song of this list is playing (or connecting). */
    bool playing() const;
    /** The song playing, else the last one played, else 0; -1 when the list is empty. */
    int current() const;

    // ---- songs from ESP32-AI Studio over USB (console musicput / musicdata / musicend, T123) ----
    // One upload at a time, written to "<name>.part" and renamed once its sha256 matches. Console task only.
    /** Start receiving `name` (a plain file name, .mp3 / .aac) of `size` bytes. nullptr = ok, else why not. */
    const char* uploadBegin(const char* name, uint32_t size, uint64_t freeBytes);
    /** Bytes `offset..` of the upload; returns how many bytes are in so far (a wrong offset changes nothing). */
    uint32_t uploadChunk(uint32_t offset, const uint8_t* data, size_t len);
    /** All bytes in + this sha256 (hex): saved and the list scanned again. nullptr = ok, else why not. */
    const char* uploadEnd(const char* sha256hex);
    void uploadAbort(const char* why);
    bool uploading() const { return _up.f != nullptr; }
    uint32_t uploadSize() const { return _up.size; }
    /** loop(): a PC that stopped sending (unplugged, Studio closed) - drop the half file. */
    void uploadTick(uint32_t now);
    /** Delete song `i` from the card. nullptr = ok. */
    const char* remove(size_t i);
    /** {"available","root","songs":[{"name","file","folder"}],"playing","current","max"} for Studio. */
    void toJson(JsonObject out) const;

private:
    struct Upload {
        FILE* f = nullptr;
        char* buf = nullptr;  // FILE buffer (PSRAM)
        PsString path, part;
        uint32_t size = 0, got = 0, lastAt = 0;
        void* sha = nullptr;  // mbedtls_sha256_context (PSRAM)
    };
    Upload _up;

    struct Track {
        PsString path;
        PsString name;
        PsString folder;
    };
    PsVector<Track> _tracks;
    mutable SemaphoreHandle_t _lock = nullptr;
    char _root[64] = "";
    int _current = -1;
    uint32_t _pausedAt = 0;  // file offset of a paused song (0 = start)
    int _failures = 0;       // songs in a row that ended without playing a note (missing / unreadable)

    void scanDir(const PsString& dir, int depth, PsVector<Track>& out);
    static void onFileEnd(bool played);
};

extern MusicLibrary gMusic;
