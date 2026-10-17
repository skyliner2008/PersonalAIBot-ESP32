/**
 * @file WakeWord.h
 * @brief Offline wake word with ESP-SR: AFE (AEC on the DAC loopback) + WakeNet (wiki 14, phase 2)
 * @see wiki/14-AEC-WakeWord-Plan.md
 *
 * Input "MR": the mic and the ES8311 DAC loopback (reg 0x44 = 0x50), sample-aligned, 16 kHz - exactly the
 * pairs AudioManager already captures. The AFE removes the speaker (radio, the assistant's voice) and
 * WakeNet listens for the word ("Jarvis" if the model partition has it, else the first WakeNet found).
 *
 * Threads: feed() (agent task, asleep only) never blocks - it queues into a PSRAM stream buffer and drops
 * audio when the AFE falls behind. A `wakefeed` task hands frames to the AFE (whose feed() blocks when its
 * ring is full: in the agent task that held the agent loop 1.3 s), a `wakeword` task blocks in fetch().
 * Needs the "model" partition (partitions_sr.csv + srmodels.bin): without it begin() returns false and
 * the assistant wakes on loudness as before.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

struct esp_afe_sr_data_t;  // esp-sr (opaque)

class WakeWord {
public:
    /** Load the models and start the AFE. False = no model partition / no WakeNet / out of memory. */
    // prefer: model name part ("hiesp", "jarvis"; any WakeNet if missing). ownAec: feed() gets the mic after
    // the agent's echo removal (input "M"); false: raw mic + reference (input "MR", the AFE's own AEC)
    // second: another word listened for at the same time (nullptr = one word)
    bool begin(const char* prefer = "hiesp", const char* second = nullptr, bool ownAec = true);
    /** The model that made the last detection ("wn9_hiesp", "wn9_jarvis_tts"). */
    const char* lastWord() const { return _lastWord; }
    /** Level of the frame that made the last detection, dBFS. */
    float lastDetectDb() const { return _lastDetectDb; }
    /** Two words are listened for. */
    bool twoWords() const { return _names[1][0] != '\0'; }
    /** feed() expects the echo already removed (ref ignored). */
    bool ownAec() const { return _channels == 1; }
    bool ready() const { return _afe != nullptr; }
    /** Name of the wake word model in use ("wn9_jarvis_tts", "wn9_hiesp", ...). */
    const char* model() const { return _model; }

    /** n mic samples + the n reference samples played meanwhile (16 kHz). Never blocks. */
    void feed(const int16_t* mic, const int16_t* ref, size_t n);
    /** Drop what is queued (call when it starts listening again, so old audio cannot trigger). */
    void reset();
    /** True once per detection. */
    bool takeDetection();

    // diagnostics
    uint32_t detections() const { return _detections; }
    float lastVolumeDb() const { return _lastVolume; }
    /** Samples dropped because the AFE could not keep up (should stay 0). */
    uint32_t dropped() const { return _dropped; }
    /** Loudest frame WakeNet got since the last call, dBFS (speech near the board: about -35 to -20). */
    float takePeakDb();
    /** WakeNet detection threshold 0.4-0.9999 (model default ~0.63; lower = wakes more easily). */
    bool setThreshold(float t);
    float threshold() const { return _threshold; }

private:
    static void fetchEntry(void* self);
    static void feedEntry(void* self);
    void fetchLoop();
    void feedLoop();

    static constexpr size_t kMaxIn = 1024;          // samples per feed() call (the agent sends 640)
    static constexpr size_t kQueueBytes = 32 * 1024;  // 0.5 s of mic + reference

    esp_afe_sr_data_t* _afe = nullptr;
    const void* _iface = nullptr;  // const esp_afe_sr_iface_t* (an anonymous struct: no forward declaration)
    char _names[2][24] = {"", ""};  // WakeNet models: [0] always, [1] when two words are on
    char _model[52] = "";           // "wn9_hiesp" or "wn9_hiesp+wn9_jarvis_tts"
    char _lastWord[24] = "";      // written by the fetch task, read after takeDetection()
    float _lastDetectDb = -100.0f;
    int16_t* _frame = nullptr;     // interleaved M,R x _chunk (feed task)
    int16_t* _inter = nullptr;     // interleaved M,R x kMaxIn (agent task)
    int _chunk = 0;                // samples per channel per AFE feed
    int _channels = 2;             // 1 = "M" (our echo removal), 2 = "MR"
    StreamBufferHandle_t _queue = nullptr;
    StaticStreamBuffer_t _queueCtl{};
    volatile bool _flush = false;
    volatile bool _detected = false;
    volatile uint32_t _detections = 0;
    uint32_t _lastDetect = 0;  // fetch task only
    volatile uint32_t _dropped = 0;
    volatile float _lastVolume = 0.0f;
    volatile float _peakVolume = -100.0f;
    float _threshold = 0.0f;  // 0 = the model's own
};
