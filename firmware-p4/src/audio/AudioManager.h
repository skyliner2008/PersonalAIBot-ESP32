/**
 * @file AudioManager.h
 * @brief Full-duplex audio: ES8311 codec + I2S @24 kHz + PSRAM ring buffers
 * @see wiki/04-System-Architecture.md
 *
 *   Speaker:  Gemini PCM 24 kHz -> playback ring (1 MB PSRAM) -> audioOutTask -> I2S TX
 *   Mic:      I2S RX 24 kHz -> audioInTask -> resample 16 kHz -> capture ring -> agent
 *             (left slot = mic, right slot = DAC loopback when AUDIO_ECHO_REF: stored as pairs)
 *
 * The I2S bus runs at 24 kHz in both directions because the ES8311 ADC and
 * DAC share one LRCK. Mic audio is converted to 16 kHz for Gemini.
 * FM8002E amplifier enable (GPIO 1) is ACTIVE LOW.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s_std.h>  // the channel driver (IDF 5+); the legacy driver/i2s.h goes away in IDF 6
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
    /** The assistant's voice: more is on its way, so an end-of-turn flag left by a tone (playTone
     *  flushes) no longer holds - else the reply started below the jitter buffer and stuttered. */
    size_t writeVoice(const uint8_t* pcm, size_t bytes) {
        _flush = false;
        return writePlayback(pcm, bytes);
    }
    void clearPlayback();
    /** No more audio for this turn: play the remaining tail even below the prebuffer. */
    void flushPlayback() { _flush = true; }
    bool isPlaying() const { return _playing || _playback.available() > 0; }
    size_t playbackFree() const { return _playback.freeSpace(); }
    uint32_t msSincePlayback() const { return millis() - _lastPlayMs; }
    float playbackLevel() const { return _playLevel; }
    size_t playbackDropped() const { return _droppedBytes; }
    size_t takePlaybackDropped() {
        const size_t d = _droppedBytes;
        _droppedBytes = 0;
        return d;
    }
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
        _replyGaps = 0;
        _replyLongestGap = 0;
        _replyOutGap = 0;
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

    // ---- Capture (16 kHz, 16-bit mono PCM + the echo reference of the same samples) ----
    void setCaptureEnabled(bool on);
    bool captureEnabled() const { return _captureOn; }
    /** Mic samples waiting (each stored with its echo-reference sample). */
    size_t captureSamples() const { return _capture.available() / 4; }
    /**
     * Up to n mic samples into `mic`; `refRms` = RMS (0-1) of what the speaker played during exactly
     * those samples (DAC loopback, before the codec volume; 0 without AUDIO_ECHO_REF). Single reader.
     */
    size_t readCapture(int16_t* mic, size_t n, float& refRms, int16_t* refOut = nullptr);
    /** The right slot carries the DAC loopback (ES8311 reg 0x44 = 0x50). */
    bool hasEchoRef() const { return _echoRef; }
    float micLevel() const { return _micLevel; }

    // ---- Diagnostics ----
    float rawLevelLeft() const { return _rmsLeft; }
    float rawLevelRight() const { return _rmsRight; }
    bool micOnRight() const { return _micRight; }
    uint32_t inBlocks() const { return _inBlocks; }

    // ---- Control ----
    /** Codec (speaker) volume 0-100: the assistant's voice and tones play at this level. */
    bool setVolume(int volume);
    int volume() const { return _volume; }
    /**
     * Radio level 0-100 on the same scale, applied digitally in the mix (0.5 dB a step like the codec,
     * relative to the codec volume, never above it): turning the radio down leaves the voice as it is.
     * The echo reference is taken after the mix, so echo removal follows it by itself.
     */
    void setRadioVolume(int volume);
    int radioVolume() const { return _radioVolumeSetting; }
    bool setMicGain(uint8_t step) {
        _micGain = step > 7 ? 7 : step;
        return _codec.setMicGain(_micGain);
    }
    /** ES8311 ADC gain step 0-7 (6 dB each): the echo canceller rescales when it changes. */
    int micGain() const { return _micGain; }
    bool codecReady() const { return _codec.ready(); }
    bool i2sReady() const { return _i2sReady; }

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
    volatile float _radioVolume = 1.0f;  // setRadioVolume(): linear gain on the radio, applied with the duck
    int _radioVolumeSetting = 100;
    volatile bool _radioOn = false;
    volatile float _radioLevel = 0.0f;
    Downsampler24to16 _resampler;
    Downsampler24to16 _refResampler;  // same filter as the mic: the pair stays sample-aligned
    bool _echoRef = false;

    bool _i2sReady = false;
    i2s_chan_handle_t _tx = nullptr;  // one full-duplex port: TX and RX share MCLK / BCLK / WS
    i2s_chan_handle_t _rx = nullptr;
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
    volatile int _micGain = AUDIO_MIC_GAIN;

    bool initI2S();
    static void outTaskEntry(void* self);
    static void inTaskEntry(void* self);
    void outLoop();
    void inLoop();
};
