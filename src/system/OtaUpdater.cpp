/**
 * @file OtaUpdater.cpp
 * @brief GitHub Releases OTA: check, download into the other slot, verify, switch, confirm or roll back
 * @see OtaUpdater.h, wiki/13-OTA-Update.md
 */

#include "OtaUpdater.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <stdarg.h>

#include "../ai/GoogleRootCA.h"
#include "../app/AppConfig.h"
#include "../app/OtaPublicKey.h"
#include "../util/Deadline.h"
#include "../util/OtaVerify.h"
#include "../util/Utf8.h"

OtaUpdater gOta;

// The bootloader rolls back a new image that resets before it is marked valid
// (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE in the Arduino SDK), but Arduino's initArduino() marks every
// image valid right at boot unless this says otherwise: confirmIfHealthy() decides instead.
extern "C" bool verifyRollbackLater() { return true; }

namespace {

// In every image: an update must carry this, with the version of the release it came from
const char kFirmwareMarker[] = "PAB-ESP32-FW:" FIRMWARE_VERSION;

constexpr uint32_t kConfirmAfterMs = 60000;              // run this long (agent alive) = the update is good
constexpr uint32_t kFirstCheckMs = 2 * 60 * 1000;        // the daily check: first one 2 min after boot
constexpr uint32_t kCheckEveryMs = 24u * 60 * 60 * 1000;
constexpr uint32_t kStallMs = 20000;                     // no data this long while downloading = failed
constexpr size_t kChunk = 4096;
constexpr char kUserAgent[] = "PersonalAIBot-ESP32/" FIRMWARE_VERSION;
// a release key is built in: every update must carry a .sig that verifies against it (T074)
constexpr bool kSignedUpdates = sizeof(OTA_SIGNING_PUBKEY_PEM) > 1;

// GET with redirects (release downloads go github.com -> release-assets.githubusercontent.com),
// TLS verified against the GitHub / Let's Encrypt roots. Returns the final HTTP status.
int openGet(HTTPClient& http, WiFiClientSecure& tls, const char* url, bool githubJson) {
    static const char* kHeaders[] = {"Location"};
    String u = url;
    for (int hop = 0; hop < 5; hop++) {
        if (!u.startsWith("https://")) return -3;
        if (!http.begin(tls, u)) return -2;
        http.setUserAgent(kUserAgent);
        http.setTimeout(15000);
        http.setConnectTimeout(15000);
        http.collectHeaders(kHeaders, 1);
        if (githubJson) http.addHeader("Accept", "application/vnd.github+json");
        const int code = http.GET();
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            u = http.header("Location");
            http.end();
            continue;
        }
        return code;
    }
    return -4;
}

void toHex(const uint8_t* d, size_t n, char* out) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = kHex[d[i] >> 4];
        out[2 * i + 1] = kHex[d[i] & 15];
    }
    out[2 * n] = '\0';
}

// Writes an image from `read` (bytes, 0 = none yet, -1 = broken) into the next OTA slot - or only
// hashes it (dryRun). Every check before the boot slot is switched; the old firmware stays intact.
struct ImageWriter {
    const char* error = nullptr;
    char sha[65] = "";
    std::function<void(uint8_t)> onProgress;
    // checked after the whole image was hashed, before esp_ota_end / the boot switch: nullptr = accepted
    std::function<const char*(const uint8_t digest[32])> verify;

    bool run(const std::function<int(uint8_t*, size_t)>& read, size_t len, const char* expectVersion,
             const char* expectSha, bool dryRun) {
        const esp_partition_t* part = dryRun ? nullptr : esp_ota_get_next_update_partition(nullptr);
        if (!dryRun && !part) return fail("ไม่มีช่องสำหรับเฟิร์มแวร์ใหม่ (partition)");
        if (!dryRun && len > part->size) return fail("ไฟล์ใหญ่กว่าช่องเฟิร์มแวร์");
        uint8_t* buf = (uint8_t*)heap_caps_malloc(kChunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf) return fail("หน่วยความจำไม่พอ");
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        ota::MarkerScanner marker;
        esp_ota_handle_t h = 0;
        bool begun = false, ok = false;
        size_t done = 0;
        uint32_t lastData = millis();
        uint8_t lastPct = 255;
        while (done < len) {
            // the first chunk must hold the image header (36 bytes) before anything is written
            size_t fill = 0;
            const size_t want = len - done < kChunk ? len - done : kChunk;
            const size_t need = done == 0 ? (want < 36 ? want : 36) : 1;
            while (fill < need) {
                const int n = read(buf + fill, want - fill);
                if (n < 0) {
                    fail("การเชื่อมต่อขาดระหว่างดาวน์โหลด");
                    goto out;
                }
                if (n == 0) {
                    if (millis() - lastData > kStallMs) {
                        fail("ดาวน์โหลดค้าง (เน็ตช้าหรือหลุด)");
                        goto out;
                    }
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                fill += (size_t)n;
                lastData = millis();
            }
            if (done == 0 && !dryRun) {
                if (const char* e = ota::checkImageHeader(buf, fill, ESP_CHIP_ID_ESP32S3)) {
                    fail(e);
                    goto out;
                }
                if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
                    fail("เริ่มเขียนเฟิร์มแวร์ไม่ได้ (esp_ota_begin)");
                    goto out;
                }
                begun = true;
            }
            mbedtls_sha256_update(&ctx, buf, fill);
            marker.feed(buf, fill);
            if (begun && esp_ota_write(h, buf, fill) != ESP_OK) {
                fail("เขียนแฟลชไม่สำเร็จ");
                goto out;
            }
            done += fill;
            const uint8_t pct = (uint8_t)(done * 100 / len);
            if (pct != lastPct && onProgress) onProgress(pct);
            lastPct = pct;
            vTaskDelay(1);  // let the idle tasks run (task watchdog)
        }
        uint8_t digest[32];
        mbedtls_sha256_finish(&ctx, digest);
        toHex(digest, sizeof(digest), sha);
        if (!dryRun) {
            if (!marker.found() || strcmp(marker.version(), expectVersion) != 0) {
                fail(marker.found() ? "เวอร์ชันในไฟล์ไม่ตรงกับ release" : "ไม่ใช่เฟิร์มแวร์ของโปรเจคนี้");
                goto out;
            }
            if (expectSha && strcmp(sha, expectSha) != 0) {
                fail("sha256 ไม่ตรงกับที่ประกาศไว้ (ไฟล์เสีย)");
                goto out;
            }
            if (verify) {
                if (const char* e = verify(digest)) {
                    fail(e);
                    goto out;
                }
            }
            begun = false;
            if (esp_ota_end(h) != ESP_OK) {  // validates the whole image (segments + appended hash)
                fail("ตรวจไฟล์เฟิร์มแวร์ไม่ผ่าน (esp_ota_end)");
                goto out;
            }
            if (esp_ota_set_boot_partition(part) != ESP_OK) {
                fail("สลับช่องบูตไม่ได้");
                goto out;
            }
        }
        ok = true;
    out:
        if (begun) esp_ota_abort(h);
        mbedtls_sha256_free(&ctx);
        heap_caps_free(buf);
        return ok;
    }

    bool fail(const char* e) {
        if (!error) error = e;
        return false;
    }
};

}  // namespace

// ============================================================
// Boot, requests, status
// ============================================================

void OtaUpdater::begin() {
    _lock = xSemaphoreCreateMutex();
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    _pendingVerify = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;

    Preferences p;
    if (p.begin("ota", false)) {
        const String tried = p.getString("try", "");
        const String slot = p.getString("part", "");
        if (tried.length()) {
            if (run && slot == run->label) {
                Serial.printf("[OTA] Running the update v%s%s\n", tried.c_str(),
                              _pendingVerify ? " - confirmed after 60 s of normal running" : "");
                if (!_pendingVerify) {
                    p.remove("try");
                    p.remove("part");
                }
            } else {  // the bootloader went back: the new image reset before it was confirmed
                snprintf(_error, sizeof(_error), "อัปเดต v%s ไม่สำเร็จ กลับมาใช้ v%s แล้ว", tried.c_str(), FIRMWARE_VERSION);
                _error[utf8::clampToBoundary(_error, strlen(_error), sizeof(_error) - 1)] = '\0';
                _state = State::Error;
                Serial.printf("[OTA] ROLLED BACK: v%s did not confirm itself -> back on v%s (%s)\n", tried.c_str(),
                              FIRMWARE_VERSION, run ? run->label : "?");
                p.remove("try");
                p.remove("part");
            }
        }
        p.end();
    }
    Serial.printf("[OTA] Firmware %s in %s%s - %s\n", kFirmwareMarker + sizeof(ota::kMarkerPrefix) - 1,
                  run ? run->label : "?", _pendingVerify ? " (pending verification)" : "",
                  kSignedUpdates ? "updates must be signed by the release key"
                                 : "no release key built in: updates are not signature-checked");
    xTaskCreatePinnedToCore(taskEntry, "ota", 10240, this, 1, &_task, 1);  // TLS handshakes need the stack
}

bool OtaUpdater::requestCheck() {
    if (!_task || busy() || _job != JobNone) return false;
    _silentCheck = false;
    _job = JobCheck;
    xTaskNotifyGive(_task);
    return true;
}

bool OtaUpdater::requestInstall() {
    if (!_task || _state != State::Available || _job != JobNone) return false;
    _job = JobInstall;
    xTaskNotifyGive(_task);
    return true;
}

bool OtaUpdater::requestFetch(const char* url) {
    if (!_task || busy() || _job != JobNone || !url || strncmp(url, "https://", 8) != 0) return false;
    strlcpy(_fetchUrl, url, sizeof(_fetchUrl));
    _job = JobFetch;
    xTaskNotifyGive(_task);
    return true;
}

bool OtaUpdater::requestSelfTest() {
    if (!_task || busy() || _job != JobNone || _pendingVerify) return false;
    _job = JobSelfTest;
    xTaskNotifyGive(_task);
    return true;
}

bool OtaUpdater::rollbackNow(String& why) {
    if (busy()) {
        why = "กำลังอัปเดตอยู่";
        return false;
    }
    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    esp_app_desc_t desc;
    if (!other || esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        why = "อีกช่องไม่มีเฟิร์มแวร์";
        return false;
    }
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(other, &st) == ESP_OK && (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED)) {
        why = "เฟิร์มแวร์ในอีกช่องเคยบูตไม่ผ่าน";
        return false;
    }
    if (esp_ota_set_boot_partition(other) != ESP_OK) {
        why = "สลับช่องบูตไม่ได้";
        return false;
    }
    why = other->label;
    Serial.printf("[OTA] Rollback requested: next boot from %s\n", other->label);
    return true;
}

const char* OtaUpdater::stateName() const {
    switch (_state) {
        case State::Checking: return "checking";
        case State::UpToDate: return "up_to_date";
        case State::Available: return "available";
        case State::Downloading: return "downloading";
        case State::Ready: return "ready";
        case State::Error: return "error";
        default: return "idle";
    }
}

String OtaUpdater::latest() const {
    xSemaphoreTake(_lock, portMAX_DELAY);
    String s = _release.tag[0] == 'v' ? _release.tag + 1 : _release.tag;
    xSemaphoreGive(_lock);
    return s;
}

String OtaUpdater::error() const {
    xSemaphoreTake(_lock, portMAX_DELAY);
    String s = _error;
    xSemaphoreGive(_lock);
    return s;
}

void OtaUpdater::describe(JsonObject out) const {
    const esp_partition_t* run = esp_ota_get_running_partition();
    out["current"] = FIRMWARE_VERSION;
    out["state"] = stateName();
    out["progress"] = _progress;
    out["latest"] = latest();
    out["error"] = error();
    out["pending_verify"] = _pendingVerify;
    out["partition"] = run ? run->label : "?";
    out["repo"] = OTA_REPO;
    out["signed_updates"] = kSignedUpdates;
}

void OtaUpdater::setRelease(const ota::Release& r) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _release = r;
    xSemaphoreGive(_lock);
}

void OtaUpdater::fail(const char* fmt, ...) {
    char buf[sizeof(_error)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[utf8::clampToBoundary(buf, strlen(buf), sizeof(buf) - 1)] = '\0';  // a cut never splits a character
    xSemaphoreTake(_lock, portMAX_DELAY);
    strlcpy(_error, buf, sizeof(_error));
    xSemaphoreGive(_lock);
    _state = State::Error;
    Serial.printf("[OTA] Failed: %s\n", buf);
}

void OtaUpdater::notice(const char* text, uint32_t holdMs) {
    char safe[192];  // never a half character on the screen / in the log, whatever cut the text
    const size_t n = utf8::completeLength(text, utf8::clampToBoundary(text, strlen(text), sizeof(safe) - 1));
    memcpy(safe, text, n);
    safe[n] = '\0';
    Serial.printf("[OTA] %s\n", safe);
    if (onNotice) onNotice(safe, holdMs);
}

// ============================================================
// Task
// ============================================================

void OtaUpdater::taskEntry(void* self) { static_cast<OtaUpdater*>(self)->loop(); }

void OtaUpdater::loop() {
    bool bootNoticeDone = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        confirmIfHealthy();
        if (!bootNoticeDone && millis() > 8000) {  // after the boot screens: say what happened to an update
            bootNoticeDone = true;
            if (_state == State::Error && _error[0]) notice(_error, 15000);
        }
        const Job job = _job;
        _job = JobNone;
        switch (job) {
            case JobCheck: runCheck(); break;
            case JobInstall: runInstall(); break;
            case JobFetch: runFetch(); break;
            case JobSelfTest: runSelfTest(); break;
            default: dailyCheck(); break;
        }
    }
}

void OtaUpdater::confirmIfHealthy() {
    if (!_pendingVerify || millis() < kConfirmAfterMs) return;
    if (deadline::elapsed(millis(), _heartbeat) > 5000) return;  // agent loop stuck: no confirmation
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return;
    _pendingVerify = false;
    Preferences p;
    if (p.begin("ota", false)) {
        p.remove("try");
        p.remove("part");
        p.end();
    }
    char msg[192];  // Thai text: 3 bytes a character
    snprintf(msg, sizeof(msg), "อัปเดตเป็น v%s เรียบร้อย", FIRMWARE_VERSION);
    _state = State::Idle;
    notice(msg, 6000);
}

void OtaUpdater::dailyCheck() {
    if (busy() || _state == State::Available || WiFi.status() != WL_CONNECTED || millis() < kFirstCheckMs) return;
    if (_checkedOnce && deadline::elapsed(millis(), _lastCheck) < kCheckEveryMs) return;
    _silentCheck = true;
    runCheck();
}

bool OtaUpdater::fetchText(const char* url, String& out, int& code) {
    WiFiClientSecure tls;
    tls.setCACert(OTA_ROOT_CA_PEM);
    tls.setTimeout(15);
    HTTPClient http;
    code = openGet(http, tls, url, strstr(url, "api.github.com") != nullptr);
    if (code == 200) out = http.getString();
    http.end();
    return code == 200 && out.length();
}

void OtaUpdater::runCheck() {
    const bool silent = _silentCheck;
    _silentCheck = false;
    _checkedOnce = true;
    _lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
        if (!silent) fail("ยังไม่ได้ต่อ Wi-Fi");
        return;
    }
    _state = State::Checking;
    String body;
    int code = 0;
    if (!fetchText("https://api.github.com/repos/" OTA_REPO "/releases/latest", body, code)) {
        fail(code == 404 ? "ยังไม่มี release บน GitHub" : "ติดต่อ GitHub ไม่ได้ (HTTP %d)", code);
        return;
    }
    ota::Release r;
    const bool complete = ota::parseRelease(body.c_str(), body.length(), r);
    if (!r.tag[0]) {
        fail("อ่านข้อมูล release ไม่ได้");
        return;
    }
    setRelease(r);
    const int cmp = ota::compareVersions(r.tag, FIRMWARE_VERSION);
    if (cmp <= 0) {
        _state = State::UpToDate;
        Serial.printf("[OTA] Up to date (v%s, latest release %s)\n", FIRMWARE_VERSION, r.tag);
        return;
    }
    if (!complete) {
        fail("%s ไม่มีไฟล์สำหรับอัปเดตผ่าน Wi-Fi (-app.bin)", r.tag);
        return;
    }
    if (kSignedUpdates && !r.sigUrl[0]) {  // whoever published it does not hold the release key
        fail("%s ไม่มีลายเซ็น (.sig) - ไม่ติดตั้ง", r.tag);
        return;
    }
    _state = State::Available;
    char msg[192];  // Thai text: 3 bytes a character
    snprintf(msg, sizeof(msg), "มีเฟิร์มแวร์ใหม่ %s • เมนูตั้งค่า > อัปเดตเฟิร์มแวร์", r.tag);
    Serial.printf("[OTA] Update available: %s (%lu KB)\n", r.tag, (unsigned long)(r.appSize / 1024));
    if (silent) notice(msg, 15000);
}

void OtaUpdater::runInstall() {
    ota::Release r;
    xSemaphoreTake(_lock, portMAX_DELAY);
    r = _release;
    xSemaphoreGive(_lock);
    const char* ver = r.tag[0] == 'v' ? r.tag + 1 : r.tag;
    _state = State::Downloading;
    _progress = 0;
    if (onPrepare) onPrepare();  // radio off, Live session closed: the whole link and RAM for this
    vTaskDelay(pdMS_TO_TICKS(2500));

    String shaText;
    int code = 0;
    char expect[65];
    if (!fetchText(r.shaUrl, shaText, code) || !ota::parseSha256(shaText.c_str(), shaText.length(), expect)) {
        fail("โหลด sha256 ไม่ได้ (HTTP %d)", code);
        return;
    }
    // the sha256 comes from the same release as the image: it catches a damaged download, not a forged
    // release. The signature does - only the holder of the private key can make one (T074)
    uint8_t sig[80];
    size_t sigLen = 0;
    if (kSignedUpdates) {
        String sigText;
        if (!r.sigUrl[0] || !fetchText(r.sigUrl, sigText, code) ||
            !(sigLen = ota::parseSignature(sigText.c_str(), sigText.length(), sig, sizeof(sig)))) {
            fail("โหลดลายเซ็นไม่ได้ (HTTP %d)", code);
            return;
        }
    }
    WiFiClientSecure tls;
    tls.setCACert(OTA_ROOT_CA_PEM);
    tls.setTimeout(15);
    HTTPClient http;
    code = openGet(http, tls, r.appUrl, false);
    const int len = code == 200 ? http.getSize() : -1;
    if (code != 200 || len <= 0 || (r.appSize && (uint32_t)len != r.appSize)) {
        http.end();
        fail("ดาวน์โหลดไม่ได้ (HTTP %d, %d bytes)", code, len);
        return;
    }
    char msg[192];  // Thai text: 3 bytes a character
    snprintf(msg, sizeof(msg), "กำลังอัปเดตเป็น v%s ... ห้ามปิดเครื่อง", ver);
    notice(msg, 0);
    Serial.printf("[OTA] Downloading %s (%d bytes)\n", r.tag, len);
    WiFiClient* stream = http.getStreamPtr();
    ImageWriter w;
    w.onProgress = [this](uint8_t p) {
        _progress = p;
        if (p % 10 == 0) Serial.printf("[OTA] %u%%\n", (unsigned)p);
    };
    if (kSignedUpdates) {
        w.verify = [&](const uint8_t digest[32]) -> const char* {
            if (const char* e = ota::verifySignature(OTA_SIGNING_PUBKEY_PEM, digest, sig, sigLen)) {
                Serial.printf("[OTA] Signature check failed: %s\n", e);
                return "ลายเซ็นไม่ถูกต้อง (ไม่ใช่ release ของเจ้าของเฟิร์มแวร์) - ไม่ติดตั้ง";
            }
            Serial.println("[OTA] Signature OK (release key)");
            return nullptr;
        };
    }
    const uint32_t t0 = millis();
    const bool ok = w.run(
        [&](uint8_t* d, size_t n) -> int {
            const int a = stream->available();
            if (a <= 0) return http.connected() ? 0 : -1;
            return stream->readBytes(d, n < (size_t)a ? n : (size_t)a);
        },
        (size_t)len, ver, expect, false);
    http.end();
    if (!ok) {
        fail("%s", w.error ? w.error : "อัปเดตไม่สำเร็จ");
        notice(_error, 15000);
        return;
    }
    Serial.printf("[OTA] Verified v%s (sha256 %s) in %lu s -> restarting\n", ver, w.sha, (unsigned long)((millis() - t0) / 1000));
    Preferences p;
    if (p.begin("ota", false)) {  // the next boot tells a confirmed update from a rollback
        p.putString("try", ver);
        const esp_partition_t* next = esp_ota_get_boot_partition();
        p.putString("part", next ? next->label : "");
        p.end();
    }
    _state = State::Ready;
    snprintf(msg, sizeof(msg), "ติดตั้ง v%s แล้ว กำลังรีสตาร์ท...", ver);
    notice(msg, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (onReboot) onReboot();  // clean Live close + memory flush, then restart
    vTaskDelay(pdMS_TO_TICKS(15000));
    ESP.restart();  // in case the agent did not
}

void OtaUpdater::runFetch() {
    _state = State::Checking;
    WiFiClientSecure tls;
    tls.setCACert(OTA_ROOT_CA_PEM);
    tls.setTimeout(15);
    HTTPClient http;
    const int code = openGet(http, tls, _fetchUrl, false);
    const int len = code == 200 ? http.getSize() : -1;
    if (code != 200 || len <= 0) {
        http.end();
        fail("fetch: HTTP %d, %d bytes", code, len);
        return;
    }
    WiFiClient* stream = http.getStreamPtr();
    ImageWriter w;
    w.onProgress = [this](uint8_t p) { _progress = p; };
    const uint32_t t0 = millis();
    const bool ok = w.run(
        [&](uint8_t* d, size_t n) -> int {
            const int a = stream->available();
            if (a <= 0) return http.connected() ? 0 : -1;
            return stream->readBytes(d, n < (size_t)a ? n : (size_t)a);
        },
        (size_t)len, "", nullptr, true);
    http.end();
    const uint32_t ms = millis() - t0;
    if (!ok) {
        fail("fetch: %s", w.error ? w.error : "failed");
        return;
    }
    _state = State::Idle;
    Serial.printf("[OTA] fetch OK: %d bytes in %lu ms (%lu KB/s) sha256 %s\n", len, (unsigned long)ms,
                  (unsigned long)(ms ? (uint32_t)len / ms : 0), w.sha);
}

void OtaUpdater::runSelfTest() {
    const esp_partition_t* run = esp_ota_get_running_partition();
    const uint32_t len = ESP.getSketchSize();
    if (!run || !len) {
        fail("selftest: running image unknown");
        return;
    }
    _state = State::Downloading;
    _progress = 0;
    if (onPrepare) onPrepare();
    vTaskDelay(pdMS_TO_TICKS(2500));
    Serial.printf("[OTA] Self-test: copying %s (%lu bytes) into the other slot\n", run->label, (unsigned long)len);
    size_t off = 0;
    ImageWriter w;
    w.onProgress = [this](uint8_t p) { _progress = p; };
    const bool ok = w.run(
        [&](uint8_t* d, size_t n) -> int {
            if (esp_partition_read(run, off, d, n) != ESP_OK) return -1;
            off += n;
            return (int)n;
        },
        len, FIRMWARE_VERSION, nullptr, false);
    if (!ok) {
        fail("selftest: %s", w.error ? w.error : "failed");
        return;
    }
    Preferences p;
    if (p.begin("ota", false)) {
        p.putString("try", FIRMWARE_VERSION);
        const esp_partition_t* next = esp_ota_get_boot_partition();
        p.putString("part", next ? next->label : "");
        p.end();
    }
    _state = State::Ready;
    Serial.printf("[OTA] Self-test image written and verified -> restarting into %s (reset within 60 s = rollback)\n",
                  esp_ota_get_boot_partition() ? esp_ota_get_boot_partition()->label : "?");
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (onReboot) onReboot();
    vTaskDelay(pdMS_TO_TICKS(15000));
    ESP.restart();
}
