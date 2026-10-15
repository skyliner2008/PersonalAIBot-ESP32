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
// in, and how the legacy driver maps ONLY_LEFT/ONLY_RIGHT, differ between setups).

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

    digitalWrite(PA_ENABLE, LOW);  // speaker ON
    Serial.printf("[Audio] Ready: I2S %d Hz, playback buffer %u KB, mic -> %d Hz\n", AUDIO_I2S_RATE,
                  (unsigned)(AUDIO_PLAYBACK_BUFFER / 1024), AUDIO_UPLOAD_RATE);
    return true;
}

bool AudioManager::initI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    cfg.sample_rate = AUDIO_I2S_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = kDmaBufCount;
    cfg.dma_buf_len = kDmaBufLen;
    cfg.use_apll = false;  // the ESP32-S3 has no APLL
    cfg.tx_desc_auto_clear = true;  // output silence on underrun
    cfg.fixed_mclk = 0;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // ES8311 coefficients assume 256*fs

    esp_err_t err = i2s_driver_install(kPort, &cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[Audio] i2s_driver_install failed: %d\n", err);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_MCLK;
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num = I2S_DIN;
    err = i2s_set_pin(kPort, &pins);
    if (err != ESP_OK) {
        Serial.printf("[Audio] i2s_set_pin failed: %d\n", err);
        return false;
    }
    i2s_zero_dma_buffer(kPort);
    return true;
}

bool AudioManager::setDataPins(int doutPin, int dinPin) {
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_MCLK;
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRCK;
    pins.data_out_num = doutPin;
    pins.data_in_num = dinPin;
    return i2s_set_pin(kPort, &pins) == ESP_OK;
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
        const float target = _radioTarget;
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
        i2s_write(kPort, stereo, n * 4, &written, portMAX_DELAY);  // paced by the DMA
        lastWrite = millis();
        if (nv) _lastPlayMs = millis();  // "speaker busy" = the assistant, not the radio
    }
}

void AudioManager::inLoop() {
    static int16_t frames[kInChunkSamples * 2];  // interleaved L/R
    static int16_t mono[kInChunkSamples];
    static int16_t out16[Downsampler24to16::maxOutput(kInChunkSamples)];
    float energyL = 0.0f, energyR = 0.0f;  // slow averages to choose the mic slot

    for (;;) {
        size_t bytesRead = 0;
        if (i2s_read(kPort, frames, sizeof(frames), &bytesRead, portMAX_DELAY) != ESP_OK || bytesRead == 0) {
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
        if (msSincePlayback() > 1500 && !_radioOn) {  // the radio would win the comparison too
            energyL = energyL * 0.95f + _rmsLeft * 0.05f;
            energyR = energyR * 0.95f + _rmsRight * 0.05f;
            _micRight = energyR > energyL * 1.5f;
        }
        const size_t off = _micRight ? 1 : 0;
        for (size_t i = 0; i < n; i++) mono[i] = frames[2 * i + off];
        _inBlocks++;

        const size_t m = _resampler.process(mono, n, out16);
        _micLevel = rms(out16, m);
        if (_captureOn) _capture.write((const uint8_t*)out16, m * 2);
    }
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
    i2s_zero_dma_buffer(kPort);
}

void AudioManager::setCaptureEnabled(bool on) {
    if (on == _captureOn) return;
    if (!on) _capture.clear();
    _captureOn = on;
}

bool AudioManager::setVolume(int volume) {
    _volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    return _codec.setVolume(_volume);
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
