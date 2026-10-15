/**
 * @file AudioManager.h
 * @brief Full-duplex audio: ES8311 codec + I2S @24 kHz + PSRAM ring buffers
 * @see wiki/04-System-Architecture.md
 *
 *   Speaker:  Gemini PCM 24 kHz -> playback ring (1 MB PSRAM) -> audioOutTask -> I2S TX
 *   Mic:      I2S RX 24 kHz -> audioInTask -> resample 16 kHz -> capture ring -> agent
 *
 * The I2S bus runs at 24 kHz in both directions because the ES8311 ADC and
 * DAC share one LRCK. Mic audio is converted to 16 kHz for Gemini.
 * FM8002E amplifier enable (GPIO 1) is ACTIVE LOW.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../app/AppConfig.h"
#include "../system/I2cBus.h"
#include "ES8311.h"
#include "Resampler.h"

/** Thread-safe byte ring buffer in PSRAM (one producer, one consumer). */
class RingBuffer {
public:
    bool init(size_t capacity);
    size_t write(const uint8_t* data, size_t len);  // returns bytes stored (drops overflow)
    size_t read(uint8_t* data, size_t len);
    size_t available() const { return _count; }
    size_t freeSpace() const { return _capacity - _count; }
    void clear();

private:
    uint8_t* _buf = nullptr;
    size_t _capacity = 0;
    size_t _head = 0;
    size_t _tail = 0;
    volatile size_t _count = 0;
    SemaphoreHandle_t _mutex = nullptr;
};

class AudioManager {
public:
    /** Codec + I2S + buffers. Returns false if the codec or I2S failed. */
    bool begin(I2cBus* bus);

    /** Start the I2S worker tasks (core 0). */
    void startTasks();

    // ---- Playback (24 kHz, 16-bit mono PCM) ----
    size_t writePlayback(const uint8_t* pcm, size_t bytes);
    void clearPlayback();
    /** No more audio for this turn: play the remaining tail even below the prebuffer. */
    void flushPlayback() { _flush = true; }
    bool isPlaying() const { return _playing || _playback.available() > 0; }
    uint32_t msSincePlayback() const { return millis() - _lastPlayMs; }
    float playbackLevel() const { return _playLevel; }
    size_t playbackDropped() const { return _droppedBytes; }
    /** Mid-reply gaps: the voice ran dry and resumed (network / agent task late). */
    uint32_t voiceUnderruns() const { return _underruns; }
    uint32_t lastGapMs() const { return _lastGapMs; }
    /** Current jitter buffer (grows after a gap, shrinks back after clean replies). */
    uint32_t prebufferMs() const { return _prebufMs; }
    /** Longest pause of the output loop between DMA writes while sounding (> ~60 ms = DMA ran dry). */
    uint32_t maxOutGapMs() const { return _maxOutGapMs; }
    void resetAudioStats() { _maxOutGapMs = 0; }
    /** Per reply (read and reset by the agent when a reply has finished playing). */
    struct ReplyStats {
        uint32_t gaps, longestGapMs, outGapMaxMs, prebufferMs;
    };
    /** After a reply: neededMs = buffer that reply would have needed (from its arrival rate). */
    void tunePrebuffer(uint32_t neededMs, bool hadGap);
    ReplyStats takeReplyStats() {
        ReplyStats r{_replyGaps, _replyLongestGap, _replyOutGap, _prebufMs};
        _replyGaps = _replyLongestGap = _replyOutGap = 0;
        return r;
    }

    // ---- Internet radio: a second mono 24 kHz source mixed under the assistant's voice ----
    size_t writeRadio(const int16_t* pcm, size_t samples) {
        return _radio.write((const uint8_t*)pcm, samples * 2) / 2;
    }
    size_t radioFree() const { return _radio.freeSpace() / 2; }
    size_t radioBuffered() const { return _radio.available() / 2; }
    void clearRadio() {
        _radio.clear();
        _radioOn = false;
    }
    /** 1.0 = full, ~0.12 = ducked under the assistant; ramps smoothly (~200 ms). */
    void setRadioGain(float g) { _radioTarget = g; }
    bool radioSounding() const { return _radioOn; }
    /** RMS (0-1) of the radio as sent to the speaker, after the duck gain: the echo reference. */
    float radioOutLevel() const { return _radioLevel; }

    // ---- Capture (16 kHz, 16-bit mono PCM) ----
    void setCaptureEnabled(bool on);
    bool captureEnabled() const { return _captureOn; }
    size_t captureAvailable() const { return _capture.available(); }
    size_t readCapture(uint8_t* buf, size_t bytes) { return _capture.read(buf, bytes); }
    float micLevel() const { return _micLevel; }

    // ---- Diagnostics ----
    float rawLevelLeft() const { return _rmsLeft; }
    float rawLevelRight() const { return _rmsRight; }
    bool micOnRight() const { return _micRight; }
    uint32_t inBlocks() const { return _inBlocks; }

    // ---- Control ----
    bool setVolume(int volume);
    int volume() const { return _volume; }
    bool setMicGain(uint8_t step) { return _codec.setMicGain(step); }
    bool codecReady() const { return _codec.ready(); }
    bool i2sReady() const { return _i2sReady; }

    /** Re-route the I2S data pins at runtime (diagnostics). */
    bool setDataPins(int doutPin, int dinPin);
    ES8311& codec() { return _codec; }

    /** Queue a sine tone (self-test / feedback beep). */
    void playTone(uint16_t freqHz, uint16_t ms, int16_t amplitude = 6000);

private:
    void endVoiceTurn();

    ES8311 _codec;
    RingBuffer _playback;
    RingBuffer _capture;
    RingBuffer _radio;
    volatile float _radioTarget = 1.0f;
    float _radioGain = 1.0f;
    volatile bool _radioOn = false;
    volatile float _radioLevel = 0.0f;
    Downsampler24to16 _resampler;

    bool _i2sReady = false;
    volatile bool _playing = false;
    volatile bool _flush = false;
    volatile bool _captureOn = false;
    volatile uint32_t _lastPlayMs = 0;
    volatile float _playLevel = 0.0f;
    volatile float _micLevel = 0.0f;
    volatile float _rmsLeft = 0.0f;
    volatile float _rmsRight = 0.0f;
    volatile bool _micRight = false;
    volatile uint32_t _inBlocks = 0;
    volatile size_t _droppedBytes = 0;
    volatile uint32_t _underruns = 0, _lastGapMs = 0, _maxOutGapMs = 0;
    volatile uint32_t _prebufMs = AUDIO_PREBUFFER_MS;
    volatile uint32_t _replyGaps = 0, _replyLongestGap = 0, _replyOutGap = 0;
    bool _starved = false;        // voice ran dry before the end-of-turn flag
    bool _gapThisTurn = false;
    uint32_t _starvedAt = 0;
    int _volume = 70;

    bool initI2S();
    static void outTaskEntry(void* self);
    static void inTaskEntry(void* self);
    void outLoop();
    void inLoop();
};
