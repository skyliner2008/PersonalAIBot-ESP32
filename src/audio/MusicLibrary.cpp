/**
 * @file MusicLibrary.cpp
 * @brief MP3 player song list (see MusicLibrary.h)
 */

#include "MusicLibrary.h"

#include <dirent.h>
#include <mbedtls/sha256.h>
#include <sys/stat.h>

#include <algorithm>

#include "../util/Id3.h"
#include "RadioPlayer.h"

MusicLibrary gMusic;

namespace {

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

bool isSong(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".aac"));
}

PsString lower(const char* s) {
    PsString o(s);
    for (char& c : o) c = (char)tolower((unsigned char)c);
    return o;
}

void copyOut(char* dst, size_t cap, const PsString& s) {
    if (!dst || !cap) return;
    const size_t n = utf8::clampToBoundary(s.c_str(), s.size(), cap - 1);
    memcpy(dst, s.c_str(), n);
    dst[n] = '\0';
}

}  // namespace

void MusicLibrary::begin(const char* root) {
    if (!_lock) _lock = xSemaphoreCreateMutex();
    snprintf(_root, sizeof(_root), "%s", root ? root : "");
    if (!available()) return;
    mkdir(_root, 0777);  // already there = EEXIST: fine (the user sees where songs go)
    RadioPlayer::onFileEnd = &MusicLibrary::onFileEnd;
    const uint32_t t0 = millis();
    const size_t n = scan();
    Serial.printf("[Music] %u song(s) in %s (%lu ms)\n", (unsigned)n, _root, (unsigned long)(millis() - t0));
}

void MusicLibrary::scanDir(const PsString& dir, int depth, PsVector<Track>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (out.size() < kMax) {
        const struct dirent* e = readdir(d);
        if (!e) break;
        if (e->d_name[0] == '.') continue;  // ., .., hidden / macOS "._x.mp3"
        PsString path = dir;
        path += '/';
        path += e->d_name;
        if (e->d_type == DT_DIR) {
            if (depth > 0) scanDir(path, depth - 1, out);
            continue;
        }
        if (!isSong(e->d_name)) continue;
        Track t;
        char name[160];  // a Thai song name runs to ~100 bytes
        id3::titleFromPath(e->d_name, name, sizeof(name));
        t.name = name[0] ? name : e->d_name;
        if (dir.size() > strlen(_root)) t.folder = dir.c_str() + strlen(_root) + 1;
        t.path = std::move(path);
        out.push_back(std::move(t));
    }
    closedir(d);
}

size_t MusicLibrary::scan() {
    if (!available()) return 0;
    PsVector<Track> list;
    scanDir(PsString(_root), 1, list);
    std::sort(list.begin(), list.end(), [](const Track& a, const Track& b) {
        return strcasecmp(a.path.c_str(), b.path.c_str()) < 0;
    });
    Guard g(_lock);
    PsString cur = _current >= 0 && _current < (int)_tracks.size() ? _tracks[_current].path : PsString();
    _tracks.swap(list);
    _current = -1;
    for (size_t i = 0; i < _tracks.size() && !cur.empty(); i++) {
        if (_tracks[i].path == cur) _current = (int)i;
    }
    if (_current < 0) _pausedAt = 0;
    return _tracks.size();
}

size_t MusicLibrary::count() const {
    if (!_lock) return 0;
    Guard g(_lock);
    return _tracks.size();
}

bool MusicLibrary::get(size_t i, char* name, size_t nameCap, char* folder, size_t folderCap) const {
    if (!_lock) return false;
    Guard g(_lock);
    if (i >= _tracks.size()) return false;
    copyOut(name, nameCap, _tracks[i].name);
    copyOut(folder, folderCap, _tracks[i].folder);
    return true;
}

int MusicLibrary::find(const char* query) const {
    if (!_lock || !query) return -1;
    while (*query == ' ') query++;
    if (!*query) return -1;
    Guard g(_lock);
    char* end = nullptr;
    const long n = strtol(query, &end, 10);
    if (end && end != query && *end == '\0') return n >= 1 && n <= (long)_tracks.size() ? (int)n - 1 : -1;
    const PsString q = lower(query);
    for (int pass = 0; pass < 2; pass++) {  // the song name first, then folder / file name
        for (size_t i = 0; i < _tracks.size(); i++) {
            const PsString hay = lower(pass == 0 ? _tracks[i].name.c_str() : _tracks[i].path.c_str());
            if (hay.find(q) != PsString::npos) return (int)i;
        }
    }
    return -1;
}

bool MusicLibrary::play(int i, uint32_t offset) {
    PsString path, name;
    {
        if (!_lock) return false;
        Guard g(_lock);
        const int n = (int)_tracks.size();
        if (n <= 0) return false;
        i = ((i % n) + n) % n;
        _current = i;
        _pausedAt = 0;
        path = _tracks[i].path;
        name = _tracks[i].name;
    }
    Serial.printf("[Music] Song %d/%u: %s%s\n", i + 1, (unsigned)count(), path.c_str(), offset ? " (resumed)" : "");
    return gRadio.playFile(path.c_str(), name.c_str(), offset);
}

bool MusicLibrary::step(int dir) {
    const int cur = current();
    if (cur < 0) return false;
    _failures = 0;
    return play(cur + dir);
}

void MusicLibrary::pause() {
    if (!playing()) return;
    const uint32_t at = gRadio.filePos();
    gRadio.stop();
    Guard g(_lock);
    _pausedAt = at;
}

bool MusicLibrary::resume() {
    const int cur = current();
    if (cur < 0) return false;
    uint32_t at;
    {
        Guard g(_lock);
        at = _pausedAt;
    }
    _failures = 0;
    return play(cur, at);
}

bool MusicLibrary::playing() const { return gRadio.active() && gRadio.isFile(); }

int MusicLibrary::current() const {
    if (!_lock) return -1;
    Guard g(_lock);
    if (_tracks.empty()) return -1;
    return _current >= 0 && _current < (int)_tracks.size() ? _current : 0;
}

// radio task: a song ended (or could not be read) - the next one, until a whole round failed
void MusicLibrary::onFileEnd(bool played) {
    MusicLibrary& m = gMusic;
    m._failures = played ? 0 : m._failures + 1;
    const int n = (int)m.count();
    if (n <= 0 || m._failures >= (n < 5 ? n : 5)) {
        Serial.printf("[Music] Stopped: %d song(s) in a row could not be played\n", m._failures);
        m._failures = 0;
        return;  // nothing started: the player stops and shows the last error
    }
    m.play(m.current() + 1);
}

// ============================================================
// Songs from ESP32-AI Studio (T123)
// ============================================================

namespace {
constexpr uint32_t kUploadStallMs = 20000;  // no chunk for this long: the PC is gone
constexpr size_t kUploadBuf = 16 * 1024;

// a plain file name the FAT card takes: no folder, no reserved characters, .mp3 / .aac
const char* checkName(const char* name) {
    if (!name || !*name) return "no file name";
    if (strlen(name) > 120) return "file name too long (max 120 bytes)";
    for (const char* p = name; *p; p++) {
        if ((unsigned char)*p < 0x20 || strchr("/\\:*?\"<>|", *p)) return "file name has a character the card cannot take";
    }
    if (name[0] == '.') return "file name starts with a dot";
    if (!isSong(name)) return "only .mp3 / .aac files";
    return nullptr;
}
}  // namespace

const char* MusicLibrary::uploadBegin(const char* name, uint32_t size, uint64_t freeBytes) {
    if (!available()) return "no SD card";
    if (const char* e = checkName(name)) return e;
    if (size == 0) return "empty file";
    if ((uint64_t)size + 1024 * 1024 > freeBytes) return "not enough space on the SD card";
    if (uploading()) uploadAbort("a new upload started");
    PsString path(_root);
    path += '/';
    path += name;
    {  // the song playing cannot be replaced under the decoder
        Guard g(_lock);
        if (_current >= 0 && _current < (int)_tracks.size() && gRadio.active() && gRadio.isFile() &&
            _tracks[_current].path == path) {
            return "this song is playing - stop it first";
        }
    }
    Upload u;
    u.path = path;
    u.part = path + ".part";
    u.f = fopen(u.part.c_str(), "wb");
    if (!u.f) return "cannot create the file on the SD card";
    u.buf = (char*)psramMalloc(kUploadBuf);  // (a FILE buffer from malloc is internal RAM, T119)
    if (u.buf) setvbuf(u.f, u.buf, _IOFBF, kUploadBuf);
    u.sha = psramMalloc(sizeof(mbedtls_sha256_context));
    if (!u.sha) {
        fclose(u.f);
        ::remove(u.part.c_str());
        psramFree(u.buf);
        return "out of memory";
    }
    mbedtls_sha256_init((mbedtls_sha256_context*)u.sha);
    mbedtls_sha256_starts((mbedtls_sha256_context*)u.sha, 0);
    u.size = size;
    u.lastAt = millis();
    _up = std::move(u);
    Serial.printf("[Music] Receiving %s (%lu bytes)\n", name, (unsigned long)size);
    return nullptr;
}

uint32_t MusicLibrary::uploadChunk(uint32_t offset, const uint8_t* data, size_t len) {
    if (!uploading()) return 0;
    _up.lastAt = millis();
    if (offset != _up.got || !len || _up.got + len > _up.size) return _up.got;  // resent / out of order: ask again
    if (fwrite(data, 1, len, _up.f) != len) {
        uploadAbort("SD card write failed");
        return 0;
    }
    mbedtls_sha256_update((mbedtls_sha256_context*)_up.sha, data, len);
    _up.got += (uint32_t)len;
    return _up.got;
}

const char* MusicLibrary::uploadEnd(const char* sha256hex) {
    if (!uploading()) return "no upload in progress";
    if (_up.got != _up.size) return "not all bytes received yet";
    uint8_t d[32];
    mbedtls_sha256_finish((mbedtls_sha256_context*)_up.sha, d);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    const bool same = sha256hex && strcasecmp(hex, sha256hex) == 0;
    const bool closed = fclose(_up.f) == 0;
    _up.f = nullptr;
    const PsString path = _up.path, part = _up.part;
    uploadAbort(nullptr);  // frees the buffers (the file is closed already)
    if (!closed || !same) {
        ::remove(part.c_str());
        return !closed ? "SD card write failed" : "checksum mismatch - send it again";
    }
    ::remove(path.c_str());  // replacing a song of the same name (rename does not overwrite on FAT)
    if (rename(part.c_str(), path.c_str()) != 0) {
        ::remove(part.c_str());
        return "cannot rename the file on the SD card";
    }
    Serial.printf("[Music] Saved %s\n", path.c_str());
    scan();
    return nullptr;
}

void MusicLibrary::uploadAbort(const char* why) {
    if (_up.f) {
        fclose(_up.f);
        ::remove(_up.part.c_str());
    }
    if (_up.sha) mbedtls_sha256_free((mbedtls_sha256_context*)_up.sha);
    psramFree(_up.sha);
    psramFree(_up.buf);
    if (why) Serial.printf("[Music] Upload dropped: %s\n", why);
    _up = Upload();
}

void MusicLibrary::uploadTick(uint32_t now) {
    if (uploading() && now - _up.lastAt > kUploadStallMs) {
        uploadAbort("nothing received for 20 s");
        Serial.println("MUSICPUT ERR timeout");
    }
}

const char* MusicLibrary::remove(size_t i) {
    PsString path;
    {
        if (!_lock) return "no SD card";
        Guard g(_lock);
        if (i >= _tracks.size()) return "no such song";
        path = _tracks[i].path;
        if ((int)i == _current && gRadio.active() && gRadio.isFile()) gRadio.stop();  // (stop() never takes our lock)
    }
    if (::remove(path.c_str()) != 0) return "cannot delete the file";
    Serial.printf("[Music] Deleted %s\n", path.c_str());
    scan();
    return nullptr;
}

void MusicLibrary::toJson(JsonObject out) const {
    out["available"] = available();
    out["root"] = (const char*)_root;
    out["max"] = (unsigned)kMax;
    if (!_lock) return;
    Guard g(_lock);
    JsonArray a = out["songs"].to<JsonArray>();
    for (const Track& t : _tracks) {
        JsonObject o = a.add<JsonObject>();
        o["name"] = t.name.c_str();
        const char* file = strrchr(t.path.c_str(), '/');
        o["file"] = file ? file + 1 : t.path.c_str();
        if (!t.folder.empty()) o["folder"] = t.folder.c_str();
    }
    const bool on = gRadio.active() && gRadio.isFile();
    out["playing"] = on && _current >= 0 ? _current + 1 : 0;  // 1-based, 0 = none
    out["current"] = _tracks.empty() ? 0 : (_current >= 0 ? _current : 0) + 1;
}
