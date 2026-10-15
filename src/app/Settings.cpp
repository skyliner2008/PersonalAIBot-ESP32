/**
 * @file Settings.cpp
 * @brief NVS-backed settings with secrets.h / AppConfig defaults
 */

#include "Settings.h"

#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <ArduinoJson.h>
#include <stdio.h>

#include "../agent/Personas.h"
#include "../util/Base64.h"
#include "../util/JsonFile.h"
#include "../util/SetupFile.h"
#include "AppConfig.h"
#include "SecretBox.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif

Settings gSettings;

namespace {

const char* const kLiveIds[] = {"gemini-3.8-live", "gemini-3.8-live-extended-thinking",
                                "gemini-3.1-flash-live-preview"};
const char* const kLiveLabels[] = {"Gemini 3.8 Live", "3.8 Live Extended Thinking", "Gemini 3.1 Flash Live"};
const char* const kTextIds[] = {"gemini-3.5-flash-lite", "gemini-3.1-flash-lite"};
const char* const kThinking[] = {"low", "medium", "high"};

// Template values from secrets.example.h are treated as "not configured"
bool isPlaceholder(const char* v) {
    return !v || !*v || !strcmp(v, "YourWiFiName") || !strcmp(v, "YourWiFiPassword") ||
           !strcmp(v, "your-gemini-api-key-here");
}

void copyStr(char* dst, size_t cap, const char* src) {
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

// ---- secrets: sealed blob under "<name>_e"; "<name>" is the pre-v3.2 plain-text copy ----
constexpr size_t kBlobCap = 192;  // longest secret (API key, 127 chars) + SecretBox::kOverhead

// SD-card copy: {"v":1,"pass_e":"<base64 blob>","key_e":"...","mt5tok_e":"..."}
bool sealToJson(JsonDocument& doc, const char* name, const char* value) {
    if (!*value) return true;
    uint8_t blob[kBlobCap];
    size_t n = 0;
    if (!secretbox::seal(name, value, blob, sizeof(blob), n)) return false;
    char text[b64::encodedLength(kBlobCap) + 1];
    b64::encode(blob, n, text);
    doc[name] = text;
    return true;
}

bool openFromJson(const JsonDocument& doc, const char* name, char* dst, size_t cap) {
    const char* text = doc[name] | "";
    if (!*text) {
        dst[0] = '\0';
        return true;
    }
    uint8_t blob[kBlobCap + 4];
    if (b64::decodedMaxLength(strlen(text)) > sizeof(blob)) return false;
    const size_t n = b64::decode(text, strlen(text), blob);
    char tmp[kBlobCap];
    if (n == b64::kError || !secretbox::open(name, blob, n, tmp, sizeof(tmp)) || strlen(tmp) >= cap) return false;
    strcpy(dst, tmp);
    memset(tmp, 0, sizeof(tmp));
    return true;
}

bool readSecret(Preferences& p, const char* plainKey, const char* sealedKey, char* dst, size_t cap,
                bool& plainFound) {
    if (p.isKey(sealedKey)) {
        uint8_t blob[kBlobCap];
        const size_t n = p.getBytes(sealedKey, blob, sizeof(blob));
        char tmp[kBlobCap];
        if (secretbox::open(sealedKey, blob, n, tmp, sizeof(tmp)) && strlen(tmp) < cap) {
            strcpy(dst, tmp);
            memset(tmp, 0, sizeof(tmp));
            return true;
        }
        Serial.printf("[Settings] ERROR: %s cannot be decrypted (NVS from another board?) - set it again\n", plainKey);
        return false;
    }
    if (p.isKey(plainKey)) {
        p.getString(plainKey, dst, cap);
        plainFound = true;
    }
    return false;
}

// The Wi-Fi driver keeps its own plain copy of SSID + password when WiFi.persistent(true) (Arduino default)
// (only a stored, non-empty password counts: the driver also writes harmless entries there)
bool driverKeepsWifiCopy() {
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t pswd[65] = {0};
    size_t len = sizeof(pswd);
    const bool stored = nvs_get_blob(h, "sta.pswd", pswd, &len) == ESP_OK && len > 0 && pswd[0] != 0;
    memset(pswd, 0, sizeof(pswd));
    nvs_close(h);
    return stored;
}

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

}  // namespace

const char* liveModelId(uint8_t m) { return kLiveIds[m < (uint8_t)LiveModel::Count ? m : 0]; }
const char* liveModelLabel(uint8_t m) { return kLiveLabels[m < (uint8_t)LiveModel::Count ? m : 0]; }
const char* textModelId(uint8_t m) { return kTextIds[m < 2 ? m : 0]; }
const char* thinkingLevelId(uint8_t l) { return kThinking[l < 3 ? l : 0]; }

bool Settings::applyProfile(SettingsData& d, const char* profileId) {
    const persona::Profile* p = persona::findProfile(profileId);
    if (!p) return false;
    copyStr(d.profile, sizeof(d.profile), p->id);
    copyStr(d.agentName, sizeof(d.agentName), p->agentName);
    copyStr(d.voice, sizeof(d.voice), p->voice);
    d.gender = p->gender;
    const int pi = persona::findPersonality(p->personality);
    d.personality = (uint8_t)(pi < 0 ? 0 : pi);
    d.eyeColor = p->eyeColor;
    return true;
}

bool Settings::begin() {
    _lock = xSemaphoreCreateMutex();
    load();
    if (_plainFound || driverKeepsWifiCopy()) migrate();
    Serial.printf("[Settings] agent \"%s\", voice %s, model %s, Wi-Fi %s, API key %s\n", _data.agentName,
                  _data.voice, liveModelId(_data.model), hasWifi() ? _data.wifiSsid : "(not set)",
                  hasApiKey() ? "set" : "(not set)");
    return true;
}

void Settings::load() {
    SettingsData d;
    applyProfile(d, "khaimuk");
    if (!isPlaceholder(WIFI_SSID)) copyStr(d.wifiSsid, sizeof(d.wifiSsid), WIFI_SSID);
    if (!isPlaceholder(WIFI_PASSWORD)) copyStr(d.wifiPass, sizeof(d.wifiPass), WIFI_PASSWORD);
    if (!isPlaceholder(GEMINI_API_KEY)) copyStr(d.apiKey, sizeof(d.apiKey), GEMINI_API_KEY);
    d.volume = AUDIO_DEFAULT_VOLUME;

    Preferences p;
    p.begin("cfg", false);  // read-write so the namespace exists on first boot (no NOT_FOUND error)
    if (p.isKey("ssid")) p.getString("ssid", d.wifiSsid, sizeof(d.wifiSsid));
    _plainFound = false;
    _nvsHasSecrets = p.isKey("pass_e") || p.isKey("key_e") || p.isKey("mt5tok_e");
    readSecret(p, "pass", "pass_e", d.wifiPass, sizeof(d.wifiPass), _plainFound);
    readSecret(p, "key", "key_e", d.apiKey, sizeof(d.apiKey), _plainFound);
    if (p.isKey("agent")) p.getString("agent", d.agentName, sizeof(d.agentName));
    if (p.isKey("user")) p.getString("user", d.userName, sizeof(d.userName));
    if (p.isKey("voice")) p.getString("voice", d.voice, sizeof(d.voice));
    if (p.isKey("prof")) p.getString("prof", d.profile, sizeof(d.profile));
    d.gender = p.getUChar("gender", d.gender);
    d.personality = p.getUChar("pers", d.personality);
    d.eyeColor = p.getUChar("eye", d.eyeColor);
    d.model = p.getUChar("model", d.model);
    d.thinking = p.getUChar("think", d.thinking);
    const bool hasVolume = p.isKey("vol");
    d.volume = p.getUChar("vol", d.volume);
    d.brightness = p.getUChar("bri", d.brightness);
    d.sleepMinutes = p.getUChar("sleep", d.sleepMinutes);
    d.clockMinutes = p.getUChar("clockmin", d.clockMinutes);
    d.dimBrightness = p.getUChar("dimbri", d.dimBrightness);
    if (d.clockMinutes > 120) d.clockMinutes = 5;
    if (d.dimBrightness < 1 || d.dimBrightness > 60) d.dimBrightness = 8;
    d.micGain = p.getUChar("mgain", d.micGain);
    if (p.isKey("mt5url")) p.getString("mt5url", d.mt5Url, sizeof(d.mt5Url));  // isKey: no NOT_FOUND log
    readSecret(p, "mt5tok", "mt5tok_e", d.mt5Token, sizeof(d.mt5Token), _plainFound);
    d.mt5Trade = p.getBool("mt5trade", d.mt5Trade);
    d.mt5MaxLot = p.getFloat("mt5lot", d.mt5MaxLot);
    d.financeTools = p.getBool("fintools", d.financeTools);
    if (d.micGain > 7) d.micGain = 7;
    d.rotation = p.getUChar("rot", d.rotation);
    d.lcdInvert = p.getBool("inv", d.lcdInvert);
    d.consolidate = p.getBool("consol", d.consolidate);
    d.googleSearch = p.getBool("gsearch", d.googleSearch);
    d.textModel = p.getUChar("tmodel", d.textModel);
    d.touchMap = p.getUChar("touch", d.touchMap);
    d.touchCal = p.getBool("tcal", false);
    d.calXAxis = p.getUChar("tcxa", 1);
    d.calYAxis = p.getUChar("tcya", 0);
    d.calRot = p.getUChar("tcrot", 1);
    d.calXs = p.getFloat("tcxs", 1);
    d.calXo = p.getFloat("tcxo", 0);
    d.calYs = p.getFloat("tcys", 1);
    d.calYo = p.getFloat("tcyo", 0);
    p.end();

    // Migrate the v2 volume key ("agent"/"vol") on first boot of v3
    Preferences old;
    if (!hasVolume && old.begin("agent", true)) {
        if (old.isKey("vol")) d.volume = (uint8_t)old.getInt("vol", d.volume);
        old.end();
    }

    // Sanitise
    if (!persona::findVoice(d.voice)) copyStr(d.voice, sizeof(d.voice), "Aoede");
    if (d.gender >= persona::kGenderCount) d.gender = persona::kNeutral;
    if (d.personality >= persona::kPersonalityCount) d.personality = 0;
    if (d.eyeColor >= persona::kEyeColorCount) d.eyeColor = 0;
    if (d.model >= (uint8_t)LiveModel::Count) d.model = 0;
    if (d.thinking > 2) d.thinking = 0;
    if (d.volume > 100) d.volume = AUDIO_DEFAULT_VOLUME;
    if (d.brightness < 5 || d.brightness > 100) d.brightness = 80;
    if (d.rotation != 1 && d.rotation != 3) d.rotation = 1;
    if (d.textModel > 1) d.textModel = 0;
    _data = d;
    // what is on flash now (a plain-text copy forces the next save to seal it)
    copyStr(_sealedPass, sizeof(_sealedPass), d.wifiPass);
    copyStr(_sealedKey, sizeof(_sealedKey), d.apiKey);
    copyStr(_sealedTok, sizeof(_sealedTok), d.mt5Token);
    _sealedValid = !_plainFound;
}

// One-time move of pre-v3.2 plain-text secrets into sealed blobs. Removing an NVS key only marks the
// entry erased (the text stays in flash until the page is recycled), so the whole partition is erased
// and rewritten - that also drops the Wi-Fi driver's own plain copy (nvs.net80211).
void Settings::migrate() {
    const SettingsData d = _data;
    uint8_t blob[kBlobCap];
    size_t n = 0;
    char back[kBlobCap];
    const char* probe = d.apiKey[0] ? d.apiKey : "probe";
    if (!secretbox::seal("selftest", probe, blob, sizeof(blob), n) ||
        !secretbox::open("selftest", blob, n, back, sizeof(back)) || strcmp(back, probe) != 0) {
        Serial.println("[Settings] ERROR: encryption self-test failed - secrets left as they are");
        return;
    }
    memset(back, 0, sizeof(back));
    Serial.println("[Settings] Encrypting Wi-Fi password / API key / MT5 token and wiping the old plain-text copies...");
    nvs_flash_deinit();
    const esp_err_t er = nvs_flash_erase();
    nvs_flash_init();
    _sealedValid = false;
    save(d);
    load();
    const bool same = !strcmp(_data.wifiPass, d.wifiPass) && !strcmp(_data.apiKey, d.apiKey) &&
                      !strcmp(_data.mt5Token, d.mt5Token) && !strcmp(_data.wifiSsid, d.wifiSsid);
    if (same && !_plainFound) {
        _migrated = true;
        Serial.printf("[Settings] Secrets encrypted on flash (NVS wipe %s)\n", er == ESP_OK ? "ok" : "failed");
        return;
    }
    // never lose the user's credentials: fall back to the old plain-text format
    Serial.println("[Settings] ERROR: encrypted read-back failed - restored plain-text secrets");
    _data = d;
    Preferences p;
    p.begin("cfg", false);
    p.putString("pass", d.wifiPass);
    p.putString("key", d.apiKey);
    p.putString("mt5tok", d.mt5Token);
    p.end();
}

void Settings::save(const SettingsData& d) {
    if (_sdSecrets[0]) saveSdSecrets(d);
    Preferences p;
    p.begin("cfg", false);
    p.putString("ssid", d.wifiSsid);
    // secrets: sealed, and only rewritten when they changed (a new random IV every time = flash wear)
    auto putSecret = [&](const char* plainKey, const char* sealedKey, const char* value, char* cache,
                         size_t cacheCap) {
        if (_sdSecrets[0]) {  // secrets live on the SD card: make sure NVS holds no copy
            if (p.isKey(sealedKey)) p.remove(sealedKey);
            if (p.isKey(plainKey)) p.remove(plainKey);
            return;
        }
        if (_sealedValid && !strcmp(cache, value) && !p.isKey(plainKey)) return;
        if (!*value) {
            if (p.isKey(sealedKey)) p.remove(sealedKey);
        } else {
            uint8_t blob[kBlobCap];
            size_t n = 0;
            if (!secretbox::seal(sealedKey, value, blob, sizeof(blob), n)) {
                Serial.printf("[Settings] ERROR: could not encrypt %s - not saved\n", plainKey);
                return;
            }
            p.putBytes(sealedKey, blob, n);
        }
        if (p.isKey(plainKey)) p.remove(plainKey);
        copyStr(cache, cacheCap, value);
    };
    putSecret("pass", "pass_e", d.wifiPass, _sealedPass, sizeof(_sealedPass));
    putSecret("key", "key_e", d.apiKey, _sealedKey, sizeof(_sealedKey));
    putSecret("mt5tok", "mt5tok_e", d.mt5Token, _sealedTok, sizeof(_sealedTok));
    p.putString("agent", d.agentName);
    p.putString("user", d.userName);
    p.putString("voice", d.voice);
    p.putString("prof", d.profile);
    p.putUChar("gender", d.gender);
    p.putUChar("pers", d.personality);
    p.putUChar("eye", d.eyeColor);
    p.putUChar("model", d.model);
    p.putUChar("think", d.thinking);
    p.putUChar("vol", d.volume);
    p.putUChar("bri", d.brightness);
    p.putUChar("sleep", d.sleepMinutes);
    p.putUChar("clockmin", d.clockMinutes);
    p.putUChar("dimbri", d.dimBrightness);
    p.putUChar("mgain", d.micGain);
    p.putString("mt5url", d.mt5Url);
    p.putBool("mt5trade", d.mt5Trade);
    p.putFloat("mt5lot", d.mt5MaxLot);
    p.putBool("fintools", d.financeTools);
    p.putUChar("rot", d.rotation);
    p.putBool("inv", d.lcdInvert);
    p.putBool("consol", d.consolidate);
    p.putBool("gsearch", d.googleSearch);
    p.putUChar("tmodel", d.textModel);
    p.putUChar("touch", d.touchMap);
    p.putBool("tcal", d.touchCal);
    p.putUChar("tcxa", d.calXAxis);
    p.putUChar("tcya", d.calYAxis);
    p.putUChar("tcrot", d.calRot);
    p.putFloat("tcxs", d.calXs);
    p.putFloat("tcxo", d.calXo);
    p.putFloat("tcys", d.calYs);
    p.putFloat("tcyo", d.calYo);
    p.end();
    _sealedValid = true;
}

SettingsData Settings::get() const {
    Guard g(_lock);
    return _data;
}

void Settings::update(const std::function<void(SettingsData&)>& fn) {
    SettingsData copy;
    {
        Guard g(_lock);
        fn(_data);
        copy = _data;
        _version = _version + 1;
    }
    save(copy);  // NVS write outside the lock (can take a few ms)
}

// ============================================================
// Secrets on the SD card
// ============================================================

bool Settings::saveSdSecrets(const SettingsData& d) {
    if (_sealedValid && !strcmp(_sealedPass, d.wifiPass) && !strcmp(_sealedKey, d.apiKey) &&
        !strcmp(_sealedTok, d.mt5Token)) {
        return true;  // unchanged: no rewrite (new random IVs would change the file every time)
    }
    JsonDocument doc;
    doc["v"] = 1;
    doc["note"] = "Encrypted for one board only. To set up again, put ai-setup.txt next to the agent folder.";
    if (!sealToJson(doc, "pass_e", d.wifiPass) || !sealToJson(doc, "key_e", d.apiKey) ||
        !sealToJson(doc, "mt5tok_e", d.mt5Token) || !jsonfile::save(_sdSecrets, doc)) {
        Serial.println("[Settings] ERROR: could not write secrets to the SD card");
        return false;
    }
    copyStr(_sealedPass, sizeof(_sealedPass), d.wifiPass);
    copyStr(_sealedKey, sizeof(_sealedKey), d.apiKey);
    copyStr(_sealedTok, sizeof(_sealedTok), d.mt5Token);
    _sealedValid = true;
    return true;
}

bool Settings::loadSdSecrets(SettingsData& d) {
    JsonDocument doc;
    if (!jsonfile::load(_sdSecrets, doc)) return false;
    SettingsData t = d;
    if (!openFromJson(doc, "pass_e", t.wifiPass, sizeof(t.wifiPass)) ||
        !openFromJson(doc, "key_e", t.apiKey, sizeof(t.apiKey)) ||
        !openFromJson(doc, "mt5tok_e", t.mt5Token, sizeof(t.mt5Token))) {
        Serial.println("[Settings] secrets.json on this SD card was made by another board - "
                       "set Wi-Fi / API key again (ai-setup.txt or the menu)");
        return false;
    }
    copyStr(d.wifiPass, sizeof(d.wifiPass), t.wifiPass);
    copyStr(d.apiKey, sizeof(d.apiKey), t.apiKey);
    copyStr(d.mt5Token, sizeof(d.mt5Token), t.mt5Token);
    memset(&t, 0, sizeof(t));
    return true;
}

void Settings::attachSecretStore(const char* sdRoot) {
    if (!sdRoot || !*sdRoot) {
        Serial.println("[Settings] Secrets kept in NVS (no SD card)");
        return;
    }
    char setupPath[64];
    snprintf(setupPath, sizeof(setupPath), "%s/ai-setup.txt", sdRoot);
    char sdPath[sizeof(_sdSecrets)];
    snprintf(sdPath, sizeof(sdPath), "%s/%s/secrets.json", sdRoot, MEMORY_DIR_NAME);

    SettingsData d;
    {
        Guard g(_lock);
        d = _data;
    }
    // 1. Who holds the newest secrets? NVS only gets them while no card is attached, so NVS wins.
    copyStr(_sdSecrets, sizeof(_sdSecrets), sdPath);
    const bool fromNvs = _nvsHasSecrets;
    bool sdOk = false;
    if (!fromNvs) {
        sdOk = loadSdSecrets(d);
        if (sdOk) {  // what is on the card now: no rewrite unless something changes
            copyStr(_sealedPass, sizeof(_sealedPass), d.wifiPass);
            copyStr(_sealedKey, sizeof(_sealedKey), d.apiKey);
            copyStr(_sealedTok, sizeof(_sealedTok), d.mt5Token);
            _sealedValid = true;
        } else {
            _sealedValid = false;
        }
    } else {
        _sealedValid = false;  // force writing the card
    }

    // 2. A plain setup file written on a PC overrides everything, then disappears
    setupfile::Values s;
    if (setupfile::parse(setupPath, s)) {
        if (s.ssid[0]) copyStr(d.wifiSsid, sizeof(d.wifiSsid), s.ssid);
        if (s.pass[0] || s.ssid[0]) copyStr(d.wifiPass, sizeof(d.wifiPass), s.pass);  // open network: empty
        if (s.key[0]) copyStr(d.apiKey, sizeof(d.apiKey), s.key);
        if (s.mt5Url[0]) copyStr(d.mt5Url, sizeof(d.mt5Url), s.mt5Url);
        if (s.mt5Token[0]) copyStr(d.mt5Token, sizeof(d.mt5Token), s.mt5Token);
        _imported = true;
    }
    memset(&s, 0, sizeof(s));

    // 3. Write the card (sealed) and drop the NVS copies. A file sealed by another board is left alone
    //    until this board actually has something to store (moving a card back must still work there).
    {
        Guard g(_lock);
        _data = d;
        _version = _version + 1;
    }
    FILE* existing = fopen(sdPath, "rb");
    const bool foreign = !fromNvs && !sdOk && existing != nullptr;
    if (existing) fclose(existing);
    if (foreign && !_imported) {
        Serial.printf("[Settings] Secrets on the SD card (%s) - waiting for this board's Wi-Fi / API key\n", sdPath);
        return;
    }
    if (!saveSdSecrets(d)) {
        _sdSecrets[0] = '\0';  // card not writable: stay on NVS
        _sealedValid = false;
        save(d);
        Serial.println("[Settings] SD card not writable - secrets kept in NVS");
        return;
    }
    save(d);  // non-secret fields (SSID, MT5 URL) + removes pass_e / key_e / mt5tok_e from NVS
    _movedFromNvs = fromNvs;
    _nvsHasSecrets = false;
    if (_imported) {
        setupfile::wipe(setupPath);
        Serial.println("[Settings] ai-setup.txt imported, encrypted to agent/secrets.json and deleted");
    }
    Serial.printf("[Settings] Secrets on the SD card (%s)%s\n", sdPath,
                  fromNvs ? " - moved from NVS" : (sdOk || _imported ? "" : " - none yet"));
    memset(&d, 0, sizeof(d));
}

bool Settings::hasWifi() const {
    Guard g(_lock);
    return _data.wifiSsid[0] != '\0';
}

bool Settings::hasApiKey() const {
    Guard g(_lock);
    return strlen(_data.apiKey) > 20;
}
