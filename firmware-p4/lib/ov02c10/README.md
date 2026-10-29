# OV02C10 camera sensor driver (vendored)

Espressif's `esp_cam_sensor` driver for the OmniVision OV02C10 (the 2 MP MIPI-CSI camera of the Guition
JC4880P443C_I_W), **Apache-2.0** (`LICENSE`, SPDX headers in every file).

- **Source:** Guition vendor package `JC4880P443C_I_W` →
  `1-Demo/idf_examples/ESP-IDF_5.5.4/common_components/esp_cam_sensor/sensors/ov02c10/` (esp_cam_sensor 2.1.0,
  esp-video-components commit `70d2ce41c5ae6a5a9f4603ddd1ab93f8c8dcaa71`). OV02C10 is not in the published
  `esp_cam_sensor` (2.4.0 in the Arduino SDK, 2.6.0 latest).
- **Changes:** `ov02c10.h` declares `ov02c10_detect()` (the vendor copy declared `sc2336_detect`). Nothing else.
- **Why here:** the Arduino SDK's prebuilt `esp_cam_sensor` lists its sensors at build time (static detect array),
  so this driver is compiled by our project and `firmware-p4/src/camera/Camera.cpp` calls `ov02c10_detect()`
  itself, then hands the device to `esp_video` (`esp_video_create_csi_video_device`).
- **Kconfig values** (the SDK never compiled this sensor) come from `firmware-p4/platformio.ini` build flags:
  `CONFIG_CAMERA_OV02C10*` (formats 1288x728 1-lane + 1920x1080 2-lane RAW10, default 1288x728, gain limit).
