/**
 * @file WakeWord.cpp
 * @brief ESP-SR AFE ("MR": AEC) + WakeNet - see WakeWord.h
 */

#include "WakeWord.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "../system/PsramTask.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

namespace {
constexpr size_t kMinInternalLeft = 50 * 1024;  // after the AFE is created (boot, before Wi-Fi / TLS)
constexpr uint32_t kRefractoryMs = 2000;          // detections closer than this are the same call
// AFE + feed + fetch tasks share core 1 with the agent (priority 4): at 5 they starved it for a steady
// 1.34 s on every wake (the AFE working through its ring at once) - the agent loop must come first
constexpr UBaseType_t kTaskPriority = 3;
const esp_afe_sr_iface_t* iface(const void* p) { return static_cast<const esp_afe_sr_iface_t*>(p); }
}  // namespace

bool WakeWord::begin(const char* prefer, bool ownAec) {
    if (_afe) return true;  // one AFE only: a second one took another ~30 KB of internal RAM (Wi-Fi then failed)
    srmodel_list_t* models = esp_srmodel_init("model");
    if (!models || models->num <= 0) {
        Serial.println("[Wake] No model partition - wake word off (loudness wakes it, as before)");
        return false;
    }
    // the chosen word (Settings::wakeWord); any other WakeNet in the partition still works
    char* name = esp_srmodel_filter(models, ESP_WN_PREFIX, prefer);
    if (!name) name = esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr);
    if (!name) {
        Serial.println("[Wake] The model partition has no WakeNet - wake word off");
        return false;
    }
    const size_t internal0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    // ownAec: the agent feeds the mic after its own echo removal (util/EchoCanceller.h: relearns the moment a
    // stream starts, 20-30 dB) - the AFE's AEC needed about a minute of radio before "Hi ESP" got through (T064).
    // Otherwise "MR": raw mic + DAC loopback, the AFE removes the speaker itself.
    _channels = ownAec ? 1 : 2;
    afe_config_t* cfg = afe_config_init(ownAec ? "M" : "MR", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!cfg) return false;
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = name;
    cfg->wakenet_model_name_2 = nullptr;
    cfg->aec_init = !ownAec;  // the speaker (radio, the assistant) out of the mic before WakeNet
    cfg->se_init = false;    // one mic: no beamforming
    cfg->ns_init = false;    // WakeNet is trained on unprocessed speech; NS costs CPU
    cfg->vad_init = false;   // the agent keeps its own VAD (wake only, phase 2a)
    cfg->agc_init = false;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;  // internal RAM is the scarce resource (wiki 14)
    cfg->afe_perferred_core = 1;  // asleep, core 1 is nearly idle; core 0 has the radio decoder + audio + Wi-Fi
    cfg->afe_perferred_priority = kTaskPriority;  // below the agent (4) on core 1, see kTaskPriority
    cfg = afe_config_check(cfg);
    const esp_afe_sr_iface_t* h = esp_afe_handle_from_config(cfg);
    esp_afe_sr_data_t* afe = h ? h->create_from_config(cfg) : nullptr;
    afe_config_free(cfg);
    if (!afe) {
        Serial.println("[Wake] AFE could not start (memory?) - wake word off");
        return false;
    }
    // Wi-Fi + 2-3 TLS links need ~30-40 KB of internal RAM at peak: never trade that for the wake word
    // (a duplicated AFE once left 7 KB - no Wi-Fi, blank screen)
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < kMinInternalLeft) {
        Serial.printf("[Wake] only %u KB internal RAM left with the AFE - wake word off\n",
                      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        h->destroy(afe);
        return false;
    }
    _chunk = h->get_feed_chunksize(afe);
    const int ch = h->get_feed_channel_num(afe);
    const size_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    _frame = (int16_t*)heap_caps_malloc((size_t)_chunk * 2 * sizeof(int16_t), caps);
    _inter = (int16_t*)heap_caps_malloc(kMaxIn * 2 * sizeof(int16_t), caps);
    uint8_t* qmem = (uint8_t*)heap_caps_malloc(kQueueBytes + 1, caps);
    if (qmem && _frame) {
        _queue = xStreamBufferCreateStatic(kQueueBytes, (size_t)_chunk * _channels * sizeof(int16_t), qmem, &_queueCtl);
    }
    if (ch != _channels || !_frame || !_inter || !_queue) {
        Serial.printf("[Wake] AFE feed: %d channels / no buffers - wake word off\n", ch);
        h->destroy(afe);
        return false;
    }
    strncpy(_model, name, sizeof(_model) - 1);
    _iface = h;
    _afe = afe;
    // both tasks only block (in the AFE / the queue) and never touch the flash: PSRAM stacks
    if (!startPsramTask(feedEntry, "wakefeed", 4096, this, kTaskPriority, nullptr, 1) ||
        !startPsramTask(fetchEntry, "wakeword", 6144, this, kTaskPriority, nullptr, 1)) {
        Serial.println("[Wake] no task");  // (a started task would use a freed AFE: never seen, keep it simple)
        _afe = nullptr;
        return false;
    }
    Serial.printf("[Wake] %s ready: AFE \"%s\" (%s), %d samples/frame, internal RAM %u KB\n", _model,
                  ownAec ? "M" : "MR", ownAec ? "WakeNet after our echo removal" : "AEC + WakeNet", _chunk,
                  (unsigned)((internal0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024));
    h->print_pipeline(afe);
    return true;
}

void WakeWord::feed(const int16_t* mic, const int16_t* ref, size_t n) {
    if (!_afe) return;
    if (n > kMaxIn) n = kMaxIn;
    if (_channels == 1) {
        memcpy(_inter, mic, n * sizeof(int16_t));
    } else {
        for (size_t i = 0; i < n; i++) {
            _inter[2 * i] = mic[i];
            _inter[2 * i + 1] = ref ? ref[i] : 0;
        }
    }
    const size_t bytes = n * _channels * sizeof(int16_t);
    // all or nothing (a partial write would shift mic/reference pairs); one writer, so the check holds
    if (xStreamBufferSpacesAvailable(_queue) < bytes) {
        _dropped = _dropped + n;
        return;
    }
    xStreamBufferSend(_queue, _inter, bytes, 0);
}

void WakeWord::reset() {
    if (!_afe) return;
    _flush = true;  // the feed task empties the queue and the AFE (a stream buffer cannot be reset while read)
    _detected = false;
}

float WakeWord::takePeakDb() {
    const float p = _peakVolume;
    _peakVolume = -100.0f;
    return p;
}

bool WakeWord::setThreshold(float t) {
    if (!_afe || t < 0.4f || t > 0.9999f) return false;
    if (iface(_iface)->set_wakenet_threshold(_afe, 1, t) != 1) return false;
    _threshold = t;
    return true;
}

bool WakeWord::takeDetection() {
    if (!_detected) return false;
    _detected = false;
    return true;
}

void WakeWord::feedEntry(void* self) { static_cast<WakeWord*>(self)->feedLoop(); }
void WakeWord::fetchEntry(void* self) { static_cast<WakeWord*>(self)->fetchLoop(); }

void WakeWord::feedLoop() {
    const size_t frameBytes = (size_t)_chunk * _channels * sizeof(int16_t);
    size_t got = 0;
    for (;;) {
        got += xStreamBufferReceive(_queue, (uint8_t*)_frame + got, frameBytes - got, pdMS_TO_TICKS(200));
        if (_flush) {
            _flush = false;
            uint8_t sink[256];
            while (xStreamBufferReceive(_queue, sink, sizeof(sink), 0) > 0) {
            }
            got = 0;
            iface(_iface)->reset_buffer(_afe);
            continue;
        }
        if (got < frameBytes) continue;
        got = 0;
        iface(_iface)->feed(_afe, _frame);  // may block while the AFE catches up - only this task waits
    }
}

void WakeWord::fetchLoop() {
    for (;;) {
        afe_fetch_result_t* r = iface(_iface)->fetch_with_delay(_afe, portMAX_DELAY);
        if (!r || r->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        _lastVolume = r->data_volume;
        if (r->data_volume > _peakVolume) _peakVolume = r->data_volume;
        if (r->wakeup_state == WAKENET_DETECTED) {
            // one call = one wake: at 0.5 the same "Hi ESP" fired up to 4 times (neighbouring frames + the
            // 0.5 s still queued) - ignore 2 s after a detection and drop what is queued
            const uint32_t now = millis();
            if (_detections && now - _lastDetect < kRefractoryMs) continue;
            _lastDetect = now;
            _flush = true;
            _detections = _detections + 1;
            _detected = true;
            Serial.printf("[Wake] heard \"%s\" (word %d, %.0f dBFS)\n", _model, r->wake_word_index, r->data_volume);
        }
    }
}
