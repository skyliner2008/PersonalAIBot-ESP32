/**
 * @file Camera.cpp
 * @brief OV02C10 -> esp_video -> PPA -> hardware JPEG (see Camera.h)
 */

#include "Camera.h"

#include <esp32-hal-i2c.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "esp_cam_sensor.h"
#include "esp_ipa.h"
extern "C" {
#include "esp_sccb_i2c.h"  // (no extern "C" of its own)
}
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include "ov02c10.h"
#include "pins_config.h"
#include "sys/mman.h"

Camera gCamera;

extern "C" {
// esp_video 2.4.1 (the SDK's) private API - exported by the prebuilt library, declared here with the same layout:
// the SDK's sensor list is fixed at build time (no OV02C10), so the device is created from our own sensor
typedef struct {
    bool dont_init_ldo;
} pab_csi_device_config_t;  // = esp_video_csi_device_config_t (private_include/esp_video_device_internal.h)
esp_err_t esp_video_create_csi_video_device(esp_cam_sensor_device_t* cam_dev, const pab_csi_device_config_t* config);

typedef struct {
    const char* isp_dev;
    const char* cam_dev;
    const esp_ipa_config_t* ipa_config;
} pab_isp_config_t;  // = esp_video_isp_config_t (private_include/esp_video_pipeline_isp.h)
esp_err_t esp_video_isp_pipeline_init(const pab_isp_config_t* config);

// The OV02C10 IPA tuning (auto exposure / white balance / colour), generated from the vendor's
// ov02c10_default_p4_eco4/eco5.json with esp_ipa's tools/config/esp_ipa_config.py - a strong definition in another
// file replaces this one (see the wiki). nullptr = the ISP's defaults only.
__attribute__((weak)) const esp_ipa_config_t* pab_ov02c10_ipa_config(void) { return nullptr; }
}

namespace {
constexpr int kCropW = 1280, kCropH = 720;     // 1288x728 -> 1280x720 (whole 16:9) -> halved = 640x360
constexpr size_t kAlign = 128;                 // PSRAM DMA buffers: L1 + L2 cache line
constexpr size_t kJpegCap = 160 * 1024;        // 640x360 at quality 60 is ~25-45 KB
constexpr int kBufCount = 2;

size_t alignUp(size_t n) { return (n + kAlign - 1) / kAlign * kAlign; }
}  // namespace

bool Camera::begin() {
    if (_ready) return true;
    // ISP video device first (as esp_video_init does): the pipeline controller talks to it. No sensor listed in
    // the config: the SDK's detect array cannot find the OV02C10 anyway
    esp_video_init_config_t vcfg = {};
    if (esp_video_init_with_flags(&vcfg, ESP_VIDEO_INIT_FLAGS_ISP) != ESP_OK) Serial.println("[Camera] ISP video device not created");

    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2cBusHandle(0);  // Wire: ES8311 + GT911 share it
    if (!bus) {
        Serial.println("[Camera] no I2C bus (Wire not started)");
        return false;
    }
    sccb_i2c_config_t sc = {};
    sc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    sc.device_address = OV02C10_SCCB_ADDR;  // 0x36
    sc.scl_speed_hz = 100000;                // the vendor examples' SCCB speed (the bus itself runs at 400 kHz)
    esp_sccb_io_handle_t io = nullptr;
    if (sccb_new_i2c_io(bus, &sc, &io) != ESP_OK) {
        Serial.println("[Camera] SCCB device not created");
        return false;
    }
    esp_cam_sensor_config_t cc = {};
    cc.sccb_handle = io;
    cc.reset_pin = (gpio_num_t)CAM_RESET;  // none on this board (CSI_IO0 pulled up on the FPC)
    cc.pwdn_pin = (gpio_num_t)CAM_PWDN;
    cc.xclk_pin = GPIO_NUM_NC;              // the module has its own 24 MHz
    cc.sensor_port = ESP_CAM_SENSOR_MIPI_CSI;
    esp_cam_sensor_device_t* dev = ov02c10_detect(&cc);
    if (!dev) {
        Serial.println("[Camera] OV02C10 not found on SCCB 0x36 (camera ribbon? PID 0x5602)");
        esp_sccb_del_i2c_io(io);
        return false;
    }
    if (esp_cam_sensor_set_format(dev, nullptr) != ESP_OK) Serial.println("[Camera] default format not set");
    // dont_init_ldo: the MIPI D-PHY LDO (channel 3, 2.5 V) is already on - the DSI display took it first
    const pab_csi_device_config_t csi = {true};
    if (esp_video_create_csi_video_device(dev, &csi) != ESP_OK) {
        Serial.println("[Camera] MIPI-CSI video device not created");
        esp_cam_sensor_del_dev(dev);
        esp_sccb_del_i2c_io(io);
        return false;
    }
    snprintf(_name, sizeof(_name), "%s", OV02C10_SENSOR_NAME);
    if (const esp_ipa_config_t* ipa = pab_ov02c10_ipa_config()) {
        const pab_isp_config_t ic = {ESP_VIDEO_ISP1_DEVICE_NAME, ESP_VIDEO_MIPI_CSI_DEVICE_NAME, ipa};
        _ipa = esp_video_isp_pipeline_init(&ic) == ESP_OK;
    }

    // what the sensor gives (1288x728 by default)
    const int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) {
        Serial.println("[Camera] cannot open " ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return false;
    }
    struct v4l2_format f = {};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &f) == 0) {
        _srcW = (int)f.fmt.pix.width;
        _srcH = (int)f.fmt.pix.height;
    }
    close(fd);
    if (_srcW < kCropW || _srcH < kCropH) {
        Serial.printf("[Camera] unexpected sensor size %dx%d\n", _srcW, _srcH);
        return false;
    }

    ppa_client_config_t pc = {};
    pc.oper_type = PPA_OPERATION_SRM;
    ppa_client_handle_t ppa = nullptr;
    jpeg_encode_engine_cfg_t ec = {};
    ec.timeout_ms = 300;
    jpeg_encoder_handle_t enc = nullptr;
    if (ppa_register_client(&pc, &ppa) != ESP_OK || jpeg_new_encoder_engine(&ec, &enc) != ESP_OK) {
        Serial.println("[Camera] PPA / JPEG engine not available");
        return false;
    }
    _ppa = ppa;
    _jpegEnc = enc;
    _scaledCap = alignUp((size_t)kOutW * kOutH * 2);
    _scaled = (uint8_t*)heap_caps_aligned_calloc(kAlign, 1, _scaledCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    jpeg_encode_memory_alloc_cfg_t mc = {};
    mc.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    _jpeg = (uint8_t*)jpeg_alloc_encoder_mem(kJpegCap, &mc, &_jpegCap);
    if (!_scaled || !_jpeg) {
        Serial.println("[Camera] no PSRAM for the picture buffers");
        return false;
    }
    _ready = true;
    Serial.printf("[Camera] %s %dx%d -> JPEG %dx%d, auto exposure / white balance: %s\n", _name, _srcW, _srcH, kOutW,
                  kOutH, _ipa ? "IPA" : "off (no tuning built in - ISP defaults)");
    return true;
}

const char* Camera::capture(const uint8_t*& jpeg, size_t& len, int quality, int settleFrames) {
    jpeg = nullptr;
    len = 0;
    if (!_ready) return "camera not available";
    const int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) return "cannot open the camera";
    const char* err = nullptr;
    void* maps[kBufCount] = {};
    size_t mapLen[kBufCount] = {};
    int mapped = 0;
    bool streaming = false;

    struct v4l2_format f = {};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_G_FMT, &f);
    f.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;  // the ISP demosaics the RAW10
    struct v4l2_control hflip = {V4L2_CID_HFLIP, 1};  // as the vendor's video_lcd_display example (HFLIP=y)
    struct v4l2_requestbuffers req = {};
    if (ioctl(fd, VIDIOC_S_FMT, &f) != 0) {
        err = "camera format not accepted";
        goto done;
    }
    ioctl(fd, VIDIOC_S_CTRL, &hflip);
    req.count = kBufCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        err = "no memory for camera frames";
        goto done;
    }
    for (int i = 0; i < kBufCount; i++) {
        struct v4l2_buffer b = {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0) {
            err = "camera buffer query failed";
            goto done;
        }
        maps[i] = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        mapLen[i] = b.length;
        mapped++;
        if (!maps[i] || ioctl(fd, VIDIOC_QBUF, &b) != 0) {
            err = "camera buffer setup failed";
            goto done;
        }
    }
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
            err = "camera stream did not start";
            goto done;
        }
        streaming = true;
    }
    for (int frame = 0; frame <= settleFrames; frame++) {
        struct v4l2_buffer b = {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &b) != 0) {
            err = "no frame from the camera";
            goto done;
        }
        if (frame == settleFrames) {
            // hardware crop + half size: 1288x728 -> centre 1280x720 -> 640x360 RGB565
            ppa_srm_oper_config_t op = {};
            op.in.buffer = maps[b.index];
            op.in.pic_w = (uint32_t)_srcW;
            op.in.pic_h = (uint32_t)_srcH;
            op.in.block_w = kCropW;
            op.in.block_h = kCropH;
            op.in.block_offset_x = (uint32_t)(_srcW - kCropW) / 2;
            op.in.block_offset_y = (uint32_t)(_srcH - kCropH) / 2;
            op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
            op.out.buffer = _scaled;
            op.out.buffer_size = _scaledCap;
            op.out.pic_w = kOutW;
            op.out.pic_h = kOutH;
            op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
            op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
            op.scale_x = (float)kOutW / kCropW;
            op.scale_y = (float)kOutH / kCropH;
            op.mode = PPA_TRANS_MODE_BLOCKING;
            const bool scaled = ppa_do_scale_rotate_mirror((ppa_client_handle_t)_ppa, &op) == ESP_OK;
            ioctl(fd, VIDIOC_QBUF, &b);
            if (!scaled) {
                err = "picture scaling failed";
                goto done;
            }
            jpeg_encode_cfg_t jc = {};
            jc.width = kOutW;
            jc.height = kOutH;
            jc.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
            jc.sub_sample = JPEG_DOWN_SAMPLING_YUV422;  // 16x8 blocks: 360 lines are whole blocks
            jc.image_quality = (uint32_t)(quality < 10 ? 10 : quality > 95 ? 95 : quality);
            uint32_t out = 0;
            if (jpeg_encoder_process((jpeg_encoder_handle_t)_jpegEnc, &jc, _scaled, (uint32_t)kOutW * kOutH * 2, _jpeg,
                                     (uint32_t)_jpegCap, &out) != ESP_OK || out == 0) {
                err = "JPEG encoding failed";
                goto done;
            }
            jpeg = _jpeg;
            len = out;
            _pictures++;
        } else {
            ioctl(fd, VIDIOC_QBUF, &b);
        }
    }

done:
    if (streaming) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < mapped; i++) {
        if (maps[i]) munmap(maps[i], mapLen[i]);
    }
    close(fd);  // the frame buffers go with it
    return err;
}
