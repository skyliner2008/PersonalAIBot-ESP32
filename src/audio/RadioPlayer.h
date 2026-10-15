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
    void stop();

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
    char _name[64] = "";
    char _title[128] = "";
    const char* _codec = "";
    volatile uint32_t _sampleRate = 0;
    char _error[64] = "";

    static void taskEntry(void* self);
    void loop();
    bool session(uint32_t gen);  // one connection: returns when stopped / dropped
    void setError(const char* e);
    void setTitle(const char* raw, size_t len);
};

extern RadioPlayer gRadio;
