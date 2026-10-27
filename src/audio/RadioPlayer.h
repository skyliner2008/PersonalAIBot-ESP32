/**
 * @file RadioPlayer.h
 * @brief Internet radio: HTTP(S) Shoutcast/Icecast stream -> Helix MP3/AAC -> mono 24 kHz -> speaker mixer
 * @see wiki/12-Internet-Radio.md
 *
 * Runs in its own task ("radio", core 0). play()/stop() are safe from any task. The decoded audio
 * goes to AudioManager::writeRadio(), which mixes it under the assistant's voice; the agent ducks
 * it with AudioManager::setRadioGain() while it listens or talks.
 *
 * Streams carry no secrets, so HTTPS streams are opened without certificate checks (radio hosts
 * use every CA under the sun). ICY "StreamTitle" metadata (now playing) is read; Thai titles in
 * TIS-620 are converted to UTF-8.
 *
 * T122: the same task also plays MP3 / AAC files from the SD card (playFile, the song list is MusicLibrary):
 * only the byte source differs, the decoder / resampler / mixer are shared - radio OR a song, never both.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AudioManager;

class RadioPlayer {
public:
    enum class State : uint8_t { Idle, Connecting, Playing, Error };

    bool begin(AudioManager* audio);

    /** Start (or switch to) a stream. `name` is shown on the screen. Returns false if not started. */
    bool play(const char* url, const char* name);
    /** Play a file ("/sdcard/music/..."), from byte `offset` (0 = start, an ID3 tag is skipped). */
    bool playFile(const char* path, const char* name, uint32_t offset = 0);
    void stop();
    /** What plays is a file (else a stream). */
    bool isFile() const { return _isFile; }
    /** Where in the file the decoder is (bytes) - for pause / resume. */
    uint32_t filePos() const { return _filePos; }
    /** Radio task: a file ended or could not be read (`played` = any audio came out). Set by MusicLibrary. */
    static void (*onFileEnd)(bool played);

    /** Started and not stopped by the user (connecting, playing or retrying). */
    bool active() const { return _want; }
    State state() const { return _state; }
    const char* stateName() const;

    /** Thread-safe snapshot of what is playing. */
    void info(char* name, size_t nameCap, char* title, size_t titleCap) const;
    const char* codec() const { return _codec; }
    uint32_t sampleRate() const { return _sampleRate; }
    const char* lastError() const { return _error; }

private:
    AudioManager* _audio = nullptr;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _lock = nullptr;
    volatile bool _want = false;
    volatile uint32_t _gen = 0;  // bumped by play()/stop(): a running session notices and ends
    volatile State _state = State::Idle;
    char _url[256] = "";
    char _name[128] = "";  // station, or a song name (T122: Thai names run past 64 bytes)
    char _title[128] = "";
    const char* _codec = "";
    volatile uint32_t _sampleRate = 0;
    char _error[64] = "";
    volatile bool _isFile = false;
    volatile uint32_t _fileOffset = 0;  // where playFile() asked to start
    volatile uint32_t _filePos = 0;
    bool _fileEnded = false;            // the last session reached the end of its file (or could not open it)

    static void taskEntry(void* self);
    void loop();
    bool session(uint32_t gen);      // one connection: returns when stopped / dropped
    bool fileSession(uint32_t gen);  // one file: returns at its end / when stopped
    // decoder loop shared by both: pull(dst, n, fill) gives >0 bytes, 0 = none yet, -1 = no more
    template <class Pull>
    bool decodeLoop(uint32_t gen, bool aac, Pull& pull, uint32_t& consumed, bool& reachedEnd);
    void setError(const char* e);
    void setTitle(const char* raw, size_t len);
};

extern RadioPlayer gRadio;
