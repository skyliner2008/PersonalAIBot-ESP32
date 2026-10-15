/**
 * @file Settings.h
 * @brief User settings stored in NVS (editable from the touch menu, voice tools and serial console)
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 *
 * Defaults come from include/secrets.h and AppConfig.h; anything changed on the
 * device is saved in the "cfg" NVS namespace and wins over the defaults.
 * Readers get a copy (get()) so they never see a half-written struct.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <functional>

enum class LiveModel : uint8_t { Live38 = 0, Live38Thinking = 1, FlashLive31 = 2, Count };

struct SettingsData {
    char wifiSsid[33] = "";
    char wifiPass[65] = "";
    char apiKey[128] = "";
    char agentName[48] = "";
    char userName[48] = "";
    char voice[24] = "Aoede";
    char profile[16] = "khaimuk";  // preset the fields came from ("custom" after edits)
    uint8_t gender = 0;            // persona::Gender
    uint8_t personality = 0;       // index in persona::kPersonalities
    uint8_t eyeColor = 0;          // index in persona::kEyeColors
    uint8_t model = 0;             // LiveModel
    uint8_t thinking = 0;          // extended-thinking level: 0 low, 1 medium, 2 high
    uint8_t volume = 70;
    uint8_t brightness = 80;
    uint8_t sleepMinutes = 3;      // 0 = never
    // MT5 bridge (mt5-core-server, same API as the PersonalAIBot app): read-only unless mt5Trade
    char mt5Url[100] = "";         // e.g. http://192.168.1.50:8090  (/api/mt5 is appended)
    char mt5Token[100] = "";       // Bearer token (never printed)
    bool mt5Trade = false;         // allow voice orders (always confirmed first)
    float mt5MaxLot = 0.10f;       // largest volume a voice order may use
    bool financeTools = true;      // declare the market / finance tools (they cost tokens every turn)
    uint8_t micGain = 5;           // ES8311 ADC scale step 0-7 (x6 dB); 5 = 30 dB, chosen by the user (normal voice at ~40 cm)
    uint8_t rotation = 1;          // 1 or 3 (landscape, flipped)
    bool lcdInvert = true;         // IPS ILI9341V shows inverted colours unless INVON is sent
    bool consolidate = true;       // background memory consolidation with Flash Lite
    bool googleSearch = true;      // offer the googleSearch tool to the live model (has its own quota)
    uint8_t textModel = 0;         // 0 = gemini-3.5-flash-lite, 1 = gemini-3.1-flash-lite
    uint8_t touchMap = 0;          // touch axis correction (UiManager::setTouchTransform)
    bool touchCal = false;         // calibration below is valid
    uint8_t calXAxis = 1, calYAxis = 0, calRot = 1;
    float calXs = 1, calXo = 0, calYs = 1, calYo = 0;
};

const char* liveModelId(uint8_t model);      // "gemini-3.8-live", ...
const char* liveModelLabel(uint8_t model);   // "Gemini 3.8 Live", ...
const char* textModelId(uint8_t model);      // "gemini-3.5-flash-lite", ...
const char* thinkingLevelId(uint8_t level);  // "low" / "medium" / "high"

class Settings {
public:
    bool begin();

    SettingsData get() const;
    /** Modify under the lock, persist, bump the version. */
    void update(const std::function<void(SettingsData&)>& fn);
    /** Increments on every change; consumers compare to detect updates. */
    uint32_t version() const { return _version; }

    bool hasWifi() const;
    bool hasApiKey() const;
    /** Wi-Fi password / API key / MT5 token are stored encrypted (no plain-text copy left in NVS). */
    bool secretsSealed() const { return _sealedValid && !_plainFound; }
    /** True on the boot that moved old plain-text secrets into encrypted storage (and wiped NVS). */
    bool migratedAtBoot() const { return _migrated; }

    /**
     * Call once the SD card is mounted (nullptr = no card -> secrets stay in NVS).
     * With a card, the Wi-Fi password / API key / MT5 token live sealed in "<root>/agent/secrets.json"
     * instead of NVS. A plain "<root>/ai-setup.txt" written on a PC is imported first, then wiped.
     */
    void attachSecretStore(const char* sdRoot);
    /** "sd" or "nvs" */
    const char* secretsStore() const { return _sdSecrets[0] ? "sd" : "nvs"; }
    /** True if ai-setup.txt was imported on this boot. */
    bool importedSetupFile() const { return _imported; }
    /** True if this boot moved secrets from NVS to the SD card. */
    bool movedFromNvs() const { return _movedFromNvs; }

    /** Apply a persona preset (voice, gender, personality, eye color, agent name). */
    static bool applyProfile(SettingsData& d, const char* profileId);

private:
    SettingsData _data;
    mutable SemaphoreHandle_t _lock = nullptr;
    volatile uint32_t _version = 1;

    // Secrets are stored sealed (SecretBox); remember what is on flash to avoid re-sealing unchanged values
    char _sealedPass[sizeof(SettingsData::wifiPass)] = "";
    char _sealedKey[sizeof(SettingsData::apiKey)] = "";
    char _sealedTok[sizeof(SettingsData::mt5Token)] = "";
    bool _sealedValid = false;
    bool _plainFound = false;  // pre-v3.2 plain-text secrets seen in NVS -> migrate()
    bool _migrated = false;
    bool _nvsHasSecrets = false;  // sealed secrets found in NVS by load()
    char _sdSecrets[64] = "";     // "<root>/agent/secrets.json" when secrets live on the SD card
    bool _imported = false;
    bool _movedFromNvs = false;

    bool saveSdSecrets(const SettingsData& d);
    bool loadSdSecrets(SettingsData& d);

    void load();
    void save(const SettingsData& d);
    void migrate();
};

extern Settings gSettings;
