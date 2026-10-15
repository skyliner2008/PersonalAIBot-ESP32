/**
 * @file Storage.cpp
 * @brief SD_MMC / LittleFS mounting for the long-term memory files
 */

#include "Storage.h"

#include <LittleFS.h>
#include <SD_MMC.h>

#include "../app/AppConfig.h"
#include "pins_config.h"

bool Storage::makeDirs(fs::FS& fs, const char* mount) {
    char rel[48];
    snprintf(rel, sizeof(rel), "/%s", MEMORY_DIR_NAME);
    if (!fs.exists(rel) && !fs.mkdir(rel)) return false;
    char ep[64];
    snprintf(ep, sizeof(ep), "/%s/episodes", MEMORY_DIR_NAME);
    if (!fs.exists(ep) && !fs.mkdir(ep)) return false;
    snprintf(_agentDir, sizeof(_agentDir), "%s/%s", mount, MEMORY_DIR_NAME);
    snprintf(_episodesDir, sizeof(_episodesDir), "%s/%s/episodes", mount, MEMORY_DIR_NAME);
    return true;
}

bool Storage::mountSd() {
    // ESP32-S3 routes SDMMC through the GPIO matrix: CLK 38, CMD 40, D0-D3 39/41/48/47
    if (SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3) &&
        SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT)) {
        Serial.println("[Storage] SD card mounted (4-bit)");
    } else {
        SD_MMC.end();
        if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0) || !SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
            SD_MMC.end();
            return false;
        }
        Serial.println("[Storage] SD card mounted (1-bit fallback)");
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        return false;
    }
    Serial.printf("[Storage] SD size %llu MB, used %llu MB\n", SD_MMC.cardSize() / (1024 * 1024),
                  SD_MMC.usedBytes() / (1024 * 1024));
    return makeDirs(SD_MMC, "/sdcard");
}

bool Storage::mountFlash() {
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) return false;
    Serial.printf("[Storage] LittleFS mounted (%u KB total)\n", (unsigned)(LittleFS.totalBytes() / 1024));
    return makeDirs(LittleFS, "/littlefs");
}

bool Storage::begin() {
    if (mountSd()) {
        _kind = StorageKind::SdCard;
    } else if (mountFlash()) {
        Serial.println("[Storage] No SD card - using internal flash (smaller, 30-day episode log)");
        _kind = StorageKind::Flash;
    } else {
        Serial.println("[Storage] ERROR: no storage - memory will not persist");
        _kind = StorageKind::None;
        return false;
    }
    Serial.printf("[Storage] Agent data in %s\n", _agentDir);
    return true;
}

const char* Storage::kindName() const {
    switch (_kind) {
        case StorageKind::SdCard: return "sd_card";
        case StorageKind::Flash: return "internal_flash";
        default: return "none";
    }
}

uint64_t Storage::totalBytes() const {
    if (_kind == StorageKind::SdCard) return SD_MMC.totalBytes();
    if (_kind == StorageKind::Flash) return LittleFS.totalBytes();
    return 0;
}

uint64_t Storage::usedBytes() const {
    if (_kind == StorageKind::SdCard) return SD_MMC.usedBytes();
    if (_kind == StorageKind::Flash) return LittleFS.usedBytes();
    return 0;
}
