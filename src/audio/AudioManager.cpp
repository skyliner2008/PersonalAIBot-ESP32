/**
 * @file AudioManager.cpp
 * @brief ES8311 + I2S full-duplex audio with PSRAM ring buffers
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 */

#include "AudioManager.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "../app/AppConfig.h"
#include "pins_config.h"

namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kDmaBufCount = 8;  // 80 ms: rides out short stalls of the output task (Wi-Fi bursts)
constexpr int kDmaBufLen = 240;  // frames per DMA buffer = 10 ms @ 24 kHz
constexpr size_t kOutChunkSamples = 480;  // 20 ms
constexpr size_t kInChunkSamples = 480;   // 20 ms @ 24 kHz -> 320 samples @ 16 kHz

// The bus runs as 16-bit STEREO frames: mono playback is duplicated to L/R, and
// the mic channel is picked automatically (which slot the ES8311 ADC data lands
// in differs between setups).

float rms(const int16_t* s, size_t n) {
    if (n == 0) return 0.0f;
    float acc = 0.0f;
    for (size_t i = 0; i < n; i++) {
        const float v = s[i] / 32768.0f;
        acc += v * v;
    }
    return sqrtf(acc / n);
}
}  // namespace

// ============================================================
// RingBuffer
// ============================================================

bool RingBuffer::init(size_t capacity) {
    _buf = (uint8_t*)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf) _buf = (uint8_t*)malloc(capacity);
    if (!_buf) return false;
    _capacity = capacity;
    _head = _tail = _count = 0;
    _mutex = xSemaphoreCreateMutex();
    return _mutex != nullptr;
}

size_t RingBuffer::write(const uint8_t* data, size_t len) {
    if (!_buf || !data || len == 0) return 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const size_t n = len < (_capacity - _count) ? len : (_capacity - _count);
    const size_t first = n < (_capacity - _head) ? n : (_capacity - _head);
    memcpy(_buf + _head, data, first);
    memcpy(_buf, data + first, n - first);
    _head = (_head + n) % _capacity;
    _count += n;
    xSemaphoreGive(_mutex);
    return n;
}

size_t RingBuffer::read(uint8_t* data, size_t len) {
    if (!_buf || !data || len == 0) return 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const size_t n = len < _count ? len : _count;
    const size_t first = n < (_capacity - _tail) ? n : (_capacity - _tail);
    memcpy(data, _buf + _tail, first);
    memcpy(data + first, _buf, n - first);
    _tail = (_tail + n) % _capacity;
    _count -= n;
    xSemaphoreGive(_mutex);
    return n;
}

void RingBuffer::clear() {
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _head = _tail = _count = 0;
    xSemaphoreGive(_mutex);
}

// ============================================================
// AudioManager
// ============================================================

bool AudioManager::begin(I2cBus* bus) {
    Serial.println("[Audio] Initializing...");
    if (!_playback.init(AUDIO_PLAYBACK_BUFFER) || !_capture.init(AUDIO_CAPTURE_BUFFER) || !_radio.init(AUDIO_RADIO_BUFFER)) {
        Serial.println("[Audio] ERROR: ring buffer allocation failed");
        return false;
    }

    // Amplifier off while the codec is configured (avoids pops). Active LOW.
    pinMode(PA_ENABLE, OUTPUT);
    digitalWrite(PA_ENABLE, HIGH);

    // I2S first: the codec needs MCLK running when its clock tree is set up
    _i2sReady = initI2S();
    if (!_i2sReady) return false;

    if (!_codec.begin(bus, AUDIO_I2S_RATE)) {
        Serial.println("[Audio] ERROR: ES8311 init failed");
        return false;
    }
    _codec.setMicGain(AUDIO_MIC_GAIN);
    setVolume(_volume);
#if AUDIO_ECHO_REF
    // ADCDAT = mic on the left slot, DAC loopback on the right (measured: wiki 01): the echo reference
    // for waking by voice over the radio now, for AEC later (wiki 14)
    _echoRef = _codec.write(0x44, 0x50);
    _micRight = false;
    Serial.printf("[Audio] Echo reference %s (ES8311 reg 0x44 = 0x50: L mic, R DAC loopback)\n", _echoRef ? "on" : "FAILED");
#endif

    digitalWrite(PA_ENABLE, LOW);  // speaker ON
    Serial.printf("[Audio] Ready: I2S %d Hz, playback buffer %u KB, mic -> %d Hz\n", AUDIO_I2S_RATE,
                  (unsigned)(AUDIO_PLAYBACK_BUFFER / 1024), AUDIO_UPLOAD_RATE);
    return true;
}

// Same bus as the legacy driver had (T074): master, Philips 16-bit stereo (16-bit slots: BCLK = 32 fs),
// MCLK = 256 fs (the ES8311 coefficients assume it), 8 x 240-frame DMA buffers each way, silence on underrun
bool AudioManager::initI2S() {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(kPort, I2S_ROLE_MASTER);
    chan.dma_desc_num = kDmaBufCount;
    chan.dma_frame_num = kDmaBufLen;
    chan.auto_clear = true;  // TX sends zeros when nothing new was written (was tx_desc_auto_clear)
    esp_err_t err = i2s_new_channel(&chan, &_tx, &_rx);
    if (err != ESP_OK) {
        Serial.printf("[Audio] i2s_new_channel failed: %d\n", err);
        return false;
    }
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_RATE);  // PLL source (the ESP32-S3 has no APLL), MCLK 256 fs
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = (gpio_num_t)I2S_MCLK;
    cfg.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK;
    cfg.gpio_cfg.ws = (gpio_num_t)I2S_LRCK;
    cfg.gpio_cfg.dout = (gpio_num_t)I2S_DOUT;
    cfg.gpio_cfg.din = (gpio_num_t)I2S_DIN;
    if ((err = i2s_channel_init_std_mode(_tx, &cfg)) != ESP_OK || (err = i2s_channel_init_std_mode(_rx, &cfg)) != ESP_OK) {
        Serial.printf("[Audio] i2s_channel_init_std_mode failed: %d\n", err);
        return false;
    }
    // the clocks run from here on: the codec needs MCLK while its clock tree is set up (begin())
    if ((err = i2s_channel_enable(_tx)) != ESP_OK || (err = i2s_channel_enable(_rx)) != ESP_OK) {
        Serial.printf("[Audio] i2s_channel_enable failed: %d\n", err);
        return false;
    }
    return true;
}

void AudioManager::startTasks() {
    xTaskCreatePinnedToCore(outTaskEntry, "audioOut", 4096, this, 6, nullptr, 0);
    xTaskCreatePinnedToCore(inTaskEntry, "audioIn", 6144, this, 6, nullptr, 0);
}

void AudioManager::outTaskEntry(void* self) { static_cast<AudioManager*>(self)->outLoop(); }
void AudioManager::inTaskEntry(void* self) { static_cast<AudioManager*>(self)->inLoop(); }

void AudioManager::outLoop() {
    static int16_t chunk[kOutChunkSamples];
    static int16_t radio[kOutChunkSamples];
    static int16_t stereo[kOutChunkSamples * 2];
    const size_t radioPrebufferBytes = (size_t)AUDIO_I2S_RATE * 2 * AUDIO_RADIO_PREBUFFER_MS / 1000;

    for (;;) {
        // ---- assistant voice: adaptive jitter buffer ----
        const size_t avail = _playback.available();
        const size_t prebufferBytes = (size_t)AUDIO_I2S_RATE * 2 * _prebufMs / 1000;
        if (!_playing && avail == 0 && _flush) {
            // the end-of-turn flag came after the last sample had already played: clear it, or the
            // next reply would start with no buffer at all and stutter
            _starved = false;
            endVoiceTurn();
        }
        if (!_playing && (avail >= prebufferBytes || (avail > 0 && _flush))) {
            _playing = true;
            // more speech soon after running dry mid-reply = a real gap: buffer more from now on
            // (seconds later it was a new reply after one that ended without the flag)
            if (_starved && millis() - _starvedAt < 3000) {
                _underruns = _underruns + 1;
                _lastGapMs = millis() - _starvedAt;
                _replyGaps = _replyGaps + 1;
                if (_lastGapMs > _replyLongestGap) _replyLongestGap = _lastGapMs;
                _gapThisTurn = true;
                if (_prebufMs < AUDIO_PREBUFFER_MAX_MS) {
                    _prebufMs = _prebufMs * 2 > AUDIO_PREBUFFER_MAX_MS ? AUDIO_PREBUFFER_MAX_MS : _prebufMs * 2;
                }
            }
            _starved = false;
        }
        if (_playing && avail == 0) {  // turn finished (or network underrun)
            _playing = false;
            if (_flush) {
                endVoiceTurn();
            } else {
                _starved = true;  // a gap - unless the end-of-turn flag follows (then it was the end)
                _starvedAt = millis();
            }
        }
        // ---- radio: sounds once a second is queued, stops when the stream runs dry ----
        const size_t ravail = _radio.available();
        if (!_radioOn && ravail >= radioPrebufferBytes) _radioOn = true;
        if (_radioOn && ravail == 0) _radioOn = false;

        static uint32_t lastWrite = 0;
        if (!_playing && !_radioOn) {
            _playLevel *= 0.85f;
            lastWrite = 0;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        size_t nv = 0, nr = 0;
        if (_playing) {
            nv = _playback.read((uint8_t*)chunk, sizeof(chunk)) / 2;
            _playLevel = rms(chunk, nv);  // speaking animation: the assistant only
        } else {
            _playLevel *= 0.85f;
        }
        if (_radioOn) nr = _radio.read((uint8_t*)radio, sizeof(radio)) / 2;
        const size_t n = nv > nr ? nv : (nr ? nr : nv);
        for (size_t i = nv; i < n; i++) chunk[i] = 0;

        // radio gain ramps toward the target within the chunk (no clicks when ducking)
        const float target = _radioTarget * _radioVolume;  // the duck and the radio's own volume, ramped together
        const float step = (target - _radioGain) / (float)(n ? n : 1) * 0.25f;  // ~80 ms to settle
        float radioEnergy = 0.0f;
        for (size_t i = 0; i < n; i++) {
            int32_t v = chunk[i];
            if (i < nr) {
                _radioGain += step;
                const float r = radio[i] * _radioGain;
                radioEnergy += r * r;
                v += (int32_t)r;
            }
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            stereo[2 * i] = (int16_t)v;
            stereo[2 * i + 1] = (int16_t)v;
        }
        if (!nr) _radioGain = target;
        _radioLevel = nr ? sqrtf(radioEnergy / (float)nr) / 32768.0f : 0.0f;
        size_t written = 0;
        const uint32_t t = millis();
        if (lastWrite && t - lastWrite > _maxOutGapMs) _maxOutGapMs = t - lastWrite;  // loop held up?
        if (nv && lastWrite && t - lastWrite > _replyOutGap) _replyOutGap = t - lastWrite;
        i2s_channel_write(_tx, stereo, n * 4, &written, portMAX_DELAY);  // paced by the DMA
        lastWrite = millis();
        if (nv) _lastPlayMs = millis();  // "speaker busy" = the assistant, not the radio
    }
}

void AudioManager::inLoop() {
    static int16_t frames[kInChunkSamples * 2];  // interleaved L/R
    static int16_t mono[kInChunkSamples];
    static int16_t ref[kInChunkSamples];
    static int16_t out16[Downsampler24to16::maxOutput(kInChunkSamples)];
    static int16_t ref16[Downsampler24to16::maxOutput(kInChunkSamples)];
    static int16_t pairs[Downsampler24to16::maxOutput(kInChunkSamples) * 2];  // capture ring: mic, ref, mic, ref ...
    float energyL = 0.0f, energyR = 0.0f;  // slow averages to choose the mic slot

    for (;;) {
        size_t bytesRead = 0;
        if (i2s_channel_read(_rx, frames, sizeof(frames), &bytesRead, portMAX_DELAY) != ESP_OK || bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const size_t n = bytesRead / 4;
        float sl = 0.0f, sr = 0.0f;
        for (size_t i = 0; i < n; i++) {
            const float l = frames[2 * i] / 32768.0f;
            const float r = frames[2 * i + 1] / 32768.0f;
            sl += l * l;
            sr += r * r;
        }
        _rmsLeft = n ? sqrtf(sl / n) : 0.0f;
        _rmsRight = n ? sqrtf(sr / n) : 0.0f;
        // Choose the mic slot only while the speaker is quiet: if the other slot carries the DAC
        // loopback, playback would win the comparison and the first ~0.7 s the user says after
        // each reply would be read from the wrong slot.
        if (!_echoRef && msSincePlayback() > 1500 && !_radioOn) {  // the radio would win the comparison too
            energyL = energyL * 0.95f + _rmsLeft * 0.05f;
            energyR = energyR * 0.95f + _rmsRight * 0.05f;
            _micRight = energyR > energyL * 1.5f;
        }
        const size_t off = _micRight ? 1 : 0;
        for (size_t i = 0; i < n; i++) mono[i] = frames[2 * i + off];
        _inBlocks++;

        const size_t m = _resampler.process(mono, n, out16);
        if (_echoRef) {
            for (size_t i = 0; i < n; i++) ref[i] = frames[2 * i + 1];
            _refResampler.process(ref, n, ref16);  // same filter, same count: stays aligned with the mic
        }
        _micLevel = rms(out16, m);
        // whole pairs only: a partial write would shift every later mic/ref pair
        if (_captureOn && _capture.freeSpace() >= m * 4) {
            for (size_t i = 0; i < m; i++) {
                pairs[2 * i] = out16[i];
                pairs[2 * i + 1] = _echoRef ? ref16[i] : 0;
            }
            _capture.write((const uint8_t*)pairs, m * 4);
        }
    }
}

size_t AudioManager::readCapture(int16_t* mic, size_t n, float& refRms, int16_t* refOut) {
    constexpr size_t kMax = AUDIO_UPLOAD_RATE * AUDIO_MIC_CHUNK_MS / 1000;  // one agent chunk (640): internal RAM is scarce
    static int16_t pairs[2 * kMax];
    if (n > kMax) n = kMax;
    const size_t got = _capture.read((uint8_t*)pairs, n * 4) / 4;
    float acc = 0.0f;
    for (size_t i = 0; i < got; i++) {
        mic[i] = pairs[2 * i];
        if (refOut) refOut[i] = pairs[2 * i + 1];
        const float r = pairs[2 * i + 1] / 32768.0f;
        acc += r * r;
    }
    refRms = got ? sqrtf(acc / (float)got) : 0.0f;
    return got;
}

size_t AudioManager::writePlayback(const uint8_t* pcm, size_t bytes) {
    const size_t n = _playback.write(pcm, bytes);
    if (n < bytes) _droppedBytes += bytes - n;
    return n;
}

void AudioManager::endVoiceTurn() {
    _flush = false;
    _gapThisTurn = false;
}

void AudioManager::tunePrebuffer(uint32_t neededMs, bool hadGap) {
    uint32_t want = neededMs < AUDIO_PREBUFFER_MS ? AUDIO_PREBUFFER_MS : neededMs;
    if (want > AUDIO_PREBUFFER_MAX_MS) want = AUDIO_PREBUFFER_MAX_MS;
    if (want > _prebufMs) {
        _prebufMs = want;  // slow link: start the next reply further ahead
    } else if (!hadGap) {
        const uint32_t eased = _prebufMs * 4 / 5;  // fast enough again: give the latency back
        _prebufMs = eased > want ? eased : want;
    }
}

void AudioManager::clearPlayback() {
    _playback.clear();
    _flush = false;
    _starved = false;  // interrupted on purpose: not a gap
    // what is already in the TX DMA buffers (<= 80 ms) still plays out: the channel driver cannot zero them
    // in place, and stopping the TX channel would also stop the clocks the mic (RX) and the ES8311 run on
}

void AudioManager::setCaptureEnabled(bool on) {
    if (on == _captureOn) return;
    if (!on) _capture.clear();
    _captureOn = on;
}

bool AudioManager::setVolume(int volume) {
    _volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    const bool ok = _codec.setVolume(_volume);
    setRadioVolume(_radioVolumeSetting);  // the radio's gain is relative to the codec volume
    return ok;
}

void AudioManager::setRadioVolume(int volume) {
    _radioVolumeSetting = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    const float db = ES8311::volumeDb(_radioVolumeSetting) - ES8311::volumeDb(_volume);
    _radioVolume = _radioVolumeSetting == 0 ? 0.0f : (db >= 0.0f ? 1.0f : powf(10.0f, db / 20.0f));
}

void AudioManager::playTone(uint16_t freqHz, uint16_t ms, int16_t amplitude) {
    const size_t samples = (size_t)AUDIO_I2S_RATE * ms / 1000;
    int16_t block[240];
    size_t done = 0;
    while (done < samples) {
        const size_t n = (samples - done) < 240 ? (samples - done) : 240;
        for (size_t i = 0; i < n; i++) {
            const size_t k = done + i;
            // 5 ms fade in/out to avoid clicks
            const size_t fade = AUDIO_I2S_RATE / 200;
            float env = 1.0f;
            if (k < fade) env = (float)k / fade;
            else if (samples - k < fade) env = (float)(samples - k) / fade;
            block[i] = (int16_t)(amplitude * env * sinf(2.0f * (float)M_PI * freqHz * k / AUDIO_I2S_RATE));
        }
        writePlayback((const uint8_t*)block, n * 2);
        done += n;
    }
    flushPlayback();
}
