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

#include "../util/WifiKnown.h"

enum class LiveModel : uint8_t { Live38 = 0, Live38Thinking = 1, FlashLive31 = 2, Count };
/** T116: the Live API takes no language code - the prompt locks it. Auto = the user's language (a short, quiet
 *  sentence was once heard as Spanish and answered in Spanish). */
enum class AiLanguage : uint8_t { Thai = 0, English = 1, Auto = 2, Count };

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
    uint8_t language = 0;          // AiLanguage: what the assistant speaks (T116)
    uint8_t volume = 70;           // the radio (menu, radio screen, Studio, set_volume) - the voice has aiVolume
    uint8_t aiVolume = 80;         // the assistant's voice + tones: locked here, the radio is turned down separately
    uint8_t brightness = 80;
    uint8_t sleepMinutes = 3;      // 0 = never
    uint8_t clockMinutes = 5;      // asleep this long -> power save: clock face + dimmed screen (0 = never)
    uint8_t dimBrightness = 8;     // screen brightness % while in power save
    bool financeTools = true;      // declare the market / finance tools (they cost tokens every turn)
    bool waterTools = true;        // declare the ThaiWater `water` tool (water level / rain / storms, T100)
    uint8_t micGain = 5;           // ES8311 ADC scale step 0-7 (x6 dB); 5 = 30 dB, chosen by the user (normal voice at ~40 cm)
    bool micMuted = false;         // mic off survives reboots: only the screen button (or Studio) turns it back on
    uint8_t wakeMode = 2;          // asleep, what wakes it by voice: 0 loudness (as before), 1 wake word (ESP-SR), 2 both
                                   // (no model partition = loudness only - wiki 14)
    uint8_t wakeWord = 1;          // which word: 0 "Jarvis" (TTS-trained: missed real Thai voices, T064), 1 "Hi ESP",
                                   // 2 both (one more WakeNet: ~16 KB internal RAM)
                                   // (trained on recorded people) - applied at boot
    uint8_t wakeThreshold = 50;    // WakeNet threshold x100 (40-99). The model's 0.63 missed every call in a Thai
                                   // accent; 0.50 woke 3/3 over the radio (T064)
    char radioName[64] = "";       // last Internet radio station (name in the station list)
    uint8_t rotation = 1;          // 1 or 3 (landscape, flipped)
    bool lcdInvert = true;         // IPS ILI9341V shows inverted colours unless INVON is sent
    bool consolidate = true;       // background memory consolidation with Flash Lite
    bool googleSearch = false;     // offer googleSearch to the live model: needs a key with a Search allowance
                                   // (free-tier projects have none - T048); off by default since v3.4
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
const char* aiLanguageId(uint8_t lang);      // "th" / "en" / "auto"
const char* aiLanguageLabel(uint8_t lang);   // "ไทย (ล็อก)", ...

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
    /** Wi-Fi password / API key are stored encrypted (no plain-text copy left in NVS). */
    bool secretsSealed() const { return _sealedValid && !_plainFound; }
    /** True on the boot that moved old plain-text secrets into encrypted storage (and wiped NVS). */
    bool migratedAtBoot() const { return _migrated; }

    /**
     * Call once the SD card is mounted (nullptr = no card -> secrets stay in NVS).
     * With a card, the Wi-Fi password / API key live sealed in "<root>/agent/secrets.json"
     * instead of NVS. A plain "<root>/ai-setup.txt" written on a PC is imported first, then wiped.
     */
    void attachSecretStore(const char* sdRoot);
    /** "sd" or "nvs" */
    const char* secretsStore() const { return _sdSecrets[0] ? "sd" : "nvs"; }
    /** True if ai-setup.txt was imported on this boot. */
    bool importedSetupFile() const { return _imported; }
    /** True if this boot moved secrets from NVS to the SD card. */
    bool movedFromNvs() const { return _movedFromNvs; }

    // ---- Wi-Fi networks connected to before (T099): passwords sealed, stored with the other secrets ----
    /** Connected to it: remember / refresh it (written only when something changed). */
    bool rememberWifi(const char* ssid, const char* pass);
    bool forgetWifi(const char* ssid);
    wifiknown::List knownWifi() const;  // with the passwords (the copy wipes them when dropped)
    size_t knownWifiCount() const;
    bool isKnownWifi(const char* ssid) const;

    /** Apply a persona preset (voice, gender, personality, eye color, agent name). */
    static bool applyProfile(SettingsData& d, const char* profileId);

private:
    SettingsData _data;
    mutable SemaphoreHandle_t _lock = nullptr;
    SemaphoreHandle_t _saveLock = nullptr;  // serialises save() (NVS + SD + the _sealed* caches)
    volatile uint32_t _version = 1;

    // Secrets are stored sealed (SecretBox); remember what is on flash to avoid re-sealing unchanged values
    char _sealedPass[sizeof(SettingsData::wifiPass)] = "";
    char _sealedKey[sizeof(SettingsData::apiKey)] = "";
    bool _sealedValid = false;
    bool _legacyFound = false;  // keys of the removed MT5 bridge still on flash / the SD card -> rewrite once
    bool _plainFound = false;  // pre-v3.2 plain-text secrets seen in NVS -> migrate()
    bool _migrated = false;
    bool _nvsHasSecrets = false;  // sealed secrets found in NVS by load()
    char _sdSecrets[64] = "";     // "<root>/agent/secrets.json" when secrets live on the SD card
    char _sdKnown[64] = "";       // "<root>/agent/wifi_known.json" (same place as the secrets)
    wifiknown::List _known;       // guarded by _lock
    bool _knownInNvs = false;     // load() found the list in NVS (moved to the card by attachSecretStore)
    void persistKnown(const wifiknown::List& list);
    void loadKnownFromSd();
    bool _imported = false;
    bool _movedFromNvs = false;

    bool saveSdSecrets(const SettingsData& d);
    bool loadSdSecrets(SettingsData& d);

    void load();
    void save(const SettingsData& d);
    void migrate();
};

extern Settings gSettings;
