/**
 * @file VehicleLog.cpp
 * @brief The OBD log on the SD card (see VehicleLog.h)
 */

#include "VehicleLog.h"

#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

#include "../app/AppConfig.h"
#include "../system/PsramTask.h"
#include "../util/PsramAlloc.h"
#include "ObdLog.h"

VehicleLog gVehicleLog;

namespace {

constexpr size_t kBufBytes = 32 * 1024;  // per buffer: ~15 s of a busy CAN bus
constexpr uint32_t kSyncEveryMs = 2000;

bool isLog(const char* n) {
    const size_t l = strlen(n);
    return l > 4 && !strcmp(n + l - 4, ".log");
}

bool plainName(const char* n) {
    return n && *n && strlen(n) < sizeof(VehicleLog::FileInfo::name) && !strchr(n, '/') && !strchr(n, '\\') &&
           !strstr(n, "..") && isLog(n);
}

}  // namespace

void VehicleLog::begin(const char* agentDir) {
    _sd = agentDir && !strncmp(agentDir, "/sdcard", 7);
    if (!_sd) return;
    snprintf(_dir, sizeof(_dir), "%s/obdlog", agentDir);
    mkdir(_dir, 0775);  // already there: fine
    _buf[0] = (char*)psramMalloc(kBufBytes);
    _buf[1] = (char*)psramMalloc(kBufBytes);
    // the SD card only - no flash writes - so the writer's stack can be PSRAM (system/PsramTask.h)
    if (!_buf[0] || !_buf[1] || !startPsramTask(taskEntry, "obdlog", 4096, this, 1, &_task, 1)) {
        _sd = false;
        Serial.println("[OBDLOG] no memory for the log writer - off");
    }
}

void VehicleLog::setEnabled(bool on) {
    _enabled = on;
    if (!on) stop();
}

void VehicleLog::request(Cmd c) {
    if (!_task) return;
    _cmd = c;
    xTaskNotifyGive(_task);
    for (int i = 0; i < 300 && _cmd != kCmdNone; i++) vTaskDelay(pdMS_TO_TICKS(10));  // up to 3 s (a slow card)
}

void VehicleLog::start(const char* source) {
    if (!_sd || !_enabled || !_task) return;
    if (_active) stop();
    snprintf(_source, sizeof(_source), "%s", source ? source : "obd");
    request(kCmdOpen);
    if (!_f) return;
    _dropped = 0;
    _active = true;
}

void VehicleLog::stop() {
    if (!_active) return;
    _active = false;
    request(kCmdClose);
}

void VehicleLog::add(uint32_t now, char tag, const char* text) {
    if (!_active) return;
    char line[720];  // a reply is at most 320 characters (Elm327::_reply); longer is cut
    const uint32_t t = (int32_t)(now - _t0) < 0 ? 0 : now - _t0;
    const size_t n = vehicle::obdlog::formatRecord(line, sizeof(line), t, tag, text);
    if (!n) return;
    portENTER_CRITICAL(&_mux);
    if (_len + n <= kBufBytes) {
        memcpy(_buf[_cur] + _len, line, n);
        _len += n;
    } else {
        _dropped = _dropped + 1;  // the card fell behind: counted, written into the next file header line
    }
    portEXIT_CRITICAL(&_mux);
}

void VehicleLog::recordf(char tag, const char* fmt, ...) {
    if (!_active) return;
    char text[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    add(millis(), tag, text);
}

// ---------------------------------------------------------------- the writer task

void VehicleLog::taskEntry(void* self) { static_cast<VehicleLog*>(self)->loop(); }

void VehicleLog::writeOut() {
    portENTER_CRITICAL(&_mux);
    const uint8_t full = _cur;
    const size_t n = _len;
    _cur ^= 1;
    _len = 0;
    portEXIT_CRITICAL(&_mux);
    if (!n || !_f) return;
    if (fwrite(_buf[full], 1, n, _f) == n) {
        _fileBytes = _fileBytes + n;
    } else {
        Serial.printf("[OBDLOG] write failed (%s) - closed\n", _name);
        fclose(_f);
        _f = nullptr;
        _active = false;
    }
}

void VehicleLog::loop() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        writeOut();
        const uint32_t now = millis();
        if (_f && (uint32_t)(now - _lastSync) >= kSyncEveryMs) {  // the car's power may go any moment
            fflush(_f);
            fsync(fileno(_f));
            _lastSync = now;
        }
        if (_f && _active && _fileBytes >= kMaxFileBytes) {  // a new file: replay loads one whole into PSRAM
            closeFile();
            openFile();
        }
        const uint8_t c = _cmd;
        if (c == kCmdOpen) {
            closeFile();
            openFile();
            _cmd = kCmdNone;
        } else if (c == kCmdClose) {
            writeOut();
            closeFile();
            _cmd = kCmdNone;
        }
    }
}

bool VehicleLog::openFile() {
    const time_t now = time(nullptr);
    const bool clock = now > 1700000000;  // set by NTP (else the board's own time since boot)
    char stamp[24], iso[32] = "unknown";
    if (clock) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S%z", &tm);
    } else {
        snprintf(stamp, sizeof(stamp), "nt%08lu", (unsigned long)millis());
    }
    snprintf(_name, sizeof(_name), "%s-%s.log", stamp, _source);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", _dir, _name);
    _f = fopen(path, "ab");
    if (!_f) {
        Serial.printf("[OBDLOG] cannot open %s\n", path);
        _name[0] = '\0';
        return false;
    }
    _t0 = millis();
    _lastSync = _t0;
    _fileBytes = (uint32_t)fprintf(_f, "#OBDLOG %lu fw=%s source=%s start=%s epoch=%lu dropped_before=%lu\n",
                                   (unsigned long)vehicle::obdlog::kVersion, FIRMWARE_VERSION, _source, iso,
                                   (unsigned long)(clock ? now : 0), (unsigned long)_dropped);
    Serial.printf("[OBDLOG] %s\n", path);
    cleanup();
    return true;
}

void VehicleLog::closeFile() {
    if (!_f) return;
    fflush(_f);
    fsync(fileno(_f));
    fclose(_f);
    _f = nullptr;
    Serial.printf("[OBDLOG] %s closed (%lu KB)\n", _name, (unsigned long)(_fileBytes / 1024));
}

// the oldest go past 200 files / 256 MB (names start with the date: name order = time order)
void VehicleLog::cleanup() {
    PsVector<FileInfo> files;
    DIR* d = opendir(_dir);
    if (!d) return;
    uint64_t total = 0;
    while (dirent* e = readdir(d)) {
        if (!isLog(e->d_name) || strlen(e->d_name) >= sizeof(FileInfo::name)) continue;
        FileInfo fi;
        snprintf(fi.name, sizeof(fi.name), "%s", e->d_name);
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", _dir, e->d_name);
        struct stat st;
        fi.bytes = stat(path, &st) == 0 ? (uint32_t)st.st_size : 0;
        total += fi.bytes;
        files.push_back(fi);
    }
    closedir(d);
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) { return strcmp(a.name, b.name) < 0; });
    size_t count = files.size();
    for (const FileInfo& fi : files) {
        if (count <= kMaxFiles && total <= kMaxTotalBytes) break;
        if (!strcmp(fi.name, _name)) continue;
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", _dir, fi.name);
        if (unlink(path) == 0) {
            count--;
            total -= fi.bytes;
        }
    }
}

// ---------------------------------------------------------------- files

size_t VehicleLog::list(FileInfo* out, size_t cap) {
    if (!_sd || !cap) return 0;
    PsVector<FileInfo> files;
    DIR* d = opendir(_dir);
    if (!d) return 0;
    while (dirent* e = readdir(d)) {
        if (!isLog(e->d_name) || strlen(e->d_name) >= sizeof(FileInfo::name)) continue;
        FileInfo fi;
        snprintf(fi.name, sizeof(fi.name), "%s", e->d_name);
        fi.bytes = 0;
        files.push_back(fi);
    }
    closedir(d);
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) { return strcmp(a.name, b.name) > 0; });
    const size_t n = files.size() < cap ? files.size() : cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = files[i];
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", _dir, out[i].name);
        struct stat st;
        out[i].bytes = stat(path, &st) == 0 ? (uint32_t)st.st_size : 0;
    }
    return n;
}

bool VehicleLog::path(const char* name, char* out, size_t cap) {
    if (!_sd) return false;
    FileInfo newest;
    if (!name || !*name || !strcmp(name, "latest")) {
        if (!list(&newest, 1)) return false;
        name = newest.name;
    }
    if (!plainName(name)) return false;
    snprintf(out, cap, "%s/%s", _dir, name);
    struct stat st;
    return stat(out, &st) == 0;
}

int VehicleLog::remove(const char* name) {
    if (!_sd || !name) return 0;
    if (strcmp(name, "all")) {
        char p[128];
        if (!path(name, p, sizeof(p)) || (_active && !strcmp(name, _name))) return 0;
        return unlink(p) == 0 ? 1 : 0;
    }
    constexpr size_t kCap = kMaxFiles + 8;
    FileInfo* files = (FileInfo*)psramMalloc(kCap * sizeof(FileInfo));  // 10 KB: not on a stack, not internal RAM
    if (!files) return 0;
    const size_t n = list(files, kCap);
    int removed = 0;
    for (size_t i = 0; i < n; i++) {
        if (_active && !strcmp(files[i].name, _name)) continue;
        char p[128];
        snprintf(p, sizeof(p), "%s/%s", _dir, files[i].name);
        removed += unlink(p) == 0 ? 1 : 0;
    }
    psramFree(files);
    return removed;
}

void VehicleLog::describe(JsonObject out) {
    out["sd_card"] = _sd;
    out["enabled"] = (bool)_enabled;
    out["recording"] = (bool)_active;
    if (_active) {
        out["file"] = (const char*)_name;
        out["kb"] = _fileBytes / 1024;
        if (_dropped) out["dropped"] = _dropped;
    }
    if (_sd) out["dir"] = (const char*)_dir;
}
