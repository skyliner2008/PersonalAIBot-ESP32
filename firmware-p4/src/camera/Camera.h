/**
 * @file Camera.h
 * @brief The board's OV02C10 camera (MIPI-CSI) -> one JPEG for the assistant to look at (T133)
 * @see ../../../wiki/19-ESP32-P4-JC4880P443C.md
 *
 * Pipeline: OV02C10 (lib/ov02c10, 1288x728 RAW10 1-lane) -> esp_video MIPI-CSI device "/dev/video0" (the P4's ISP
 * turns RAW into RGB565) -> PPA (hardware) crops to 1280x720 and halves it, mirrors it -> JPEG encoder (hardware)
 * 640x360 -> GeminiLiveClient::sendImage(). The sensor is found once at boot (SCCB 0x36 on the shared I2C bus);
 * frame buffers (~3.7 MB PSRAM) exist only while a picture is taken.
 *
 * Auto exposure / white balance need an IPA tuning for this sensor (the SDK has none for the OV02C10): a build
 * that links pab_ov02c10_ipa_config() (generated from the vendor JSON, see the wiki) gets them; without it the
 * ISP runs with its defaults and the picture may be too dark or tinted.
 * Agent task only (one picture at a time; GeminiLiveClient is single-caller).
 */

#pragma once

#include <Arduino.h>

class Camera {
public:
    /** Detect the sensor and create the video devices. False = no camera (it stays off, the tool is not declared). */
    bool begin();
    bool ready() const { return _ready; }
    const char* sensorName() const { return _name; }

    /**
     * Take one picture: `jpeg` points at it (valid until the next capture), `len` its size. nullptr = ok, else why
     * not. `settleFrames` frames are skipped first (exposure after the stream starts).
     */
    const char* capture(const uint8_t*& jpeg, size_t& len, int quality = 60, int settleFrames = 6);

    static constexpr int kOutW = 640, kOutH = 360;
    uint32_t pictures() const { return _pictures; }

private:
    bool _ready = false;
    bool _ipa = false;
    char _name[16] = "";
    int _srcW = 0, _srcH = 0;
    void* _ppa = nullptr;      // ppa_client_handle_t
    void* _jpegEnc = nullptr;  // jpeg_encoder_handle_t
    uint8_t* _scaled = nullptr;
    size_t _scaledCap = 0;
    uint8_t* _jpeg = nullptr;
    size_t _jpegCap = 0;
    uint32_t _pictures = 0;
};

extern Camera gCamera;
