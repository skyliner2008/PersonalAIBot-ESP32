/**
 * @file ObdLog.cpp
 * @brief OBD log records and replay (see ObdLog.h)
 */

#include "ObdLog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vehicle {
namespace obdlog {

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool isAt(const char* cmd) { return (cmd[0] == 'A' || cmd[0] == 'a') && (cmd[1] == 'T' || cmd[1] == 't'); }

}  // namespace

size_t escape(const char* text, char* out, size_t cap, bool replyLines) {
    size_t o = 0;
    if (!cap) return 0;
    for (const unsigned char* p = (const unsigned char*)(text ? text : ""); *p; p++) {
        char seq[5];
        size_t k = 0;
        const unsigned char c = *p;
        if (replyLines && c == '\n') seq[k++] = '|';
        else if (c == '\\') seq[k++] = '\\', seq[k++] = '\\';
        else if (c == '\t') seq[k++] = '\\', seq[k++] = 't';
        else if (c == '\n') seq[k++] = '\\', seq[k++] = 'n';
        else if (c == '\r') seq[k++] = '\\', seq[k++] = 'r';
        else if (c == '|') seq[k++] = '\\', seq[k++] = '|';
        else if (c < 0x20 || c == 0x7F) k = (size_t)snprintf(seq, sizeof(seq), "\\x%02X", c);
        else seq[k++] = (char)c;
        if (o + k >= cap) break;  // never half an escape
        memcpy(out + o, seq, k);
        o += k;
    }
    out[o] = '\0';
    return o;
}

size_t unescape(const char* text, size_t n, char* out, size_t cap, bool replyLines) {
    size_t o = 0;
    if (!cap) return 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = text[i];
        if (c == '|' && replyLines) {
            c = '\n';
        } else if (c == '\\' && i + 1 < n) {
            const char e = text[++i];
            if (e == 't') c = '\t';
            else if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 'x' && i + 2 < n && hexVal(text[i + 1]) >= 0 && hexVal(text[i + 2]) >= 0) {
                c = (char)(hexVal(text[i + 1]) << 4 | hexVal(text[i + 2]));
                i += 2;
            } else {
                c = e;  // \\ and \|
            }
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return o;
}

size_t formatRecord(char* out, size_t cap, uint32_t t, char tag, const char* text) {
    const int head = snprintf(out, cap, "%lu\t%c\t", (unsigned long)t, tag);
    if (head < 0 || (size_t)head + 2 > cap) return 0;
    size_t n = (size_t)head + escape(text, out + head, cap - head - 1, tag == '<');
    out[n++] = '\n';
    out[n] = '\0';
    return n;
}

bool parseRecord(const char* line, size_t n, Record& r) {
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
    if (!n || line[0] == '#' || !isdigit((unsigned char)line[0])) return false;
    size_t i = 0;
    uint32_t t = 0;
    while (i < n && isdigit((unsigned char)line[i])) t = t * 10 + (uint32_t)(line[i++] - '0');
    if (i + 1 >= n || line[i] != '\t') return false;  // "<t>\t<tag>" at least
    if (i + 2 < n && line[i + 2] != '\t') return false;
    r.t = t;
    r.tag = line[i + 1];
    r.text = i + 3 <= n ? line + i + 3 : line + n;
    r.len = i + 3 <= n ? n - (i + 3) : 0;
    return true;
}

size_t formatValues(const Snapshot& s, uint32_t now, char* out, size_t cap) {
    size_t o = 0;
    if (cap) out[0] = '\0';
    for (int i = 0; i < kChannelCount; i++) {
        const Channel c = (Channel)i;
        if (!s.fresh(c, now)) continue;
        const int n = snprintf(out + o, cap - o, "%s%s=%.*f", o ? " " : "", channelKey(c), channelDecimals(c) + (c == kBattery),
                               s.value(c));
        if (n < 0 || o + (size_t)n >= cap) break;
        o += (size_t)n;
    }
    return o;
}

int parseValues(const char* text, size_t n, Snapshot& out, uint32_t now, uint32_t ttlMs) {
    int set = 0;
    size_t i = 0;
    while (i < n) {
        while (i < n && text[i] == ' ') i++;
        const size_t k = i;
        while (i < n && text[i] != '=' && text[i] != ' ') i++;
        if (i >= n || text[i] != '=') continue;
        const size_t klen = i - k;
        i++;
        char num[24];
        size_t m = 0;
        while (i < n && text[i] != ' ' && m + 1 < sizeof(num)) num[m++] = text[i++];
        while (i < n && text[i] != ' ') i++;
        num[m] = '\0';
        char* end = nullptr;
        const float v = strtof(num, &end);
        if (end == num) continue;
        for (int c = 0; c < kChannelCount; c++) {
            const char* key = channelKey((Channel)c);
            if (strlen(key) == klen && !strncmp(key, text + k, klen)) {
                out.set((Channel)c, v, now, ttlMs);
                set++;
                break;
            }
        }
    }
    return set;
}

// ---------------------------------------------------------------- replay

Replay::Command* Replay::find(const char* cmd) {
    for (Command& c : _cmds) {
        if (c.cmd == cmd) return &c;
    }
    return nullptr;
}

const Replay::Command* Replay::find(const char* cmd) const {
    for (const Command& c : _cmds) {
        if (c.cmd == cmd) return &c;
    }
    return nullptr;
}

bool Replay::load(const char* data, size_t n) {
    _data = data;
    _n = n;
    _cmds.clear();
    _values.clear();
    _notes.clear();
    _records = 0;
    _first = _duration = 0;
    _headerLen = 0;
    uint32_t last = 0, firstCmd = 0;
    bool haveCmd = false;
    int pendingCmd = -1;  // the command waiting for its reply (the driver sends one at a time)
    for (size_t pos = 0; pos < n;) {
        const char* nl = (const char*)memchr(data + pos, '\n', n - pos);
        const size_t len = nl ? (size_t)(nl - (data + pos)) : n - pos;
        const char* line = data + pos;
        pos += len + 1;
        if (!_headerLen && len > 7 && !strncmp(line, "#OBDLOG", 7)) {
            _headerLen = len;
            continue;
        }
        Record r;
        if (!parseRecord(line, len, r)) continue;
        if (!_records++) _first = r.t;
        if (r.t > last) last = r.t;
        switch (r.tag) {
            case '>': {
                if (!haveCmd) {
                    haveCmd = true;
                    firstCmd = r.t;
                }
                char cmd[24];
                unescape(r.text, r.len, cmd, sizeof(cmd));
                Command* c = find(cmd);
                if (!c) {
                    _cmds.push_back(Command());
                    c = &_cmds.back();
                    c->cmd = cmd;
                }
                Entry e;
                e.t = r.t;
                e.silent = true;  // until its reply turns up
                c->entries.push_back(e);
                pendingCmd = (int)(c - _cmds.data());
                break;
            }
            case '<':
                if (pendingCmd >= 0) {
                    Entry& e = _cmds[pendingCmd].entries.back();
                    e.reply = r.t;
                    e.off = (uint32_t)(r.text - data);
                    e.len = (uint32_t)r.len;
                    e.silent = false;
                }
                pendingCmd = -1;
                break;
            case '!':
                pendingCmd = -1;  // stays silent: the adapter did not answer that one
                break;
            case 'D':
                _values.push_back(r);
                break;
            case 'S': case 'E': case 'C': case 'H':
                _notes.push_back(r);
                break;
            default:
                break;
        }
    }
    // an adapter replay starts at the first command (not at the Bluetooth connect before it)
    if (haveCmd) _first = firstCmd;
    _duration = last > _first ? last - _first : 0;
    return _records > 0;
}

const char* Replay::header(const char* key, char* out, size_t cap) const {
    if (cap) out[0] = '\0';
    if (!_headerLen || !cap) return out;
    const size_t klen = strlen(key);
    for (size_t i = 0; i < _headerLen;) {
        while (i < _headerLen && _data[i] == ' ') i++;
        const size_t s = i;
        while (i < _headerLen && _data[i] != ' ') i++;
        if (i - s > klen && !strncmp(_data + s, key, klen) && _data[s + klen] == '=') {
            const size_t vlen = i - s - klen - 1 < cap - 1 ? i - s - klen - 1 : cap - 1;
            memcpy(out, _data + s + klen + 1, vlen);
            out[vlen] = '\0';
            break;
        }
    }
    return out;
}

Replay::Answer Replay::answer(const char* cmd, uint32_t t) const {
    Answer a;
    const Command* c = find(cmd);
    if (!c || c->entries.empty()) {  // never sent while recording (another driver version)
        const char* text = !isAt(cmd) ? "NO DATA" : (!strcmp(cmd, "ATZ") || !strcmp(cmd, "ATWS")) ? "ELM327 v1.5" : "OK";
        snprintf(a.text, sizeof(a.text), "%s", text);
        return a;
    }
    const PsVector<Entry>& es = c->entries;
    size_t lo = 0, hi = es.size();  // the first entry after t
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (es[mid].t <= t) lo = mid + 1;
        else hi = mid;
    }
    const Entry& e = es[lo ? lo - 1 : 0];
    a.known = true;
    a.silent = e.silent;
    a.latencyMs = e.silent ? 0 : e.reply - e.t;
    if (!e.silent) unescape(_data + e.off, e.len, a.text, sizeof(a.text), true);
    return a;
}

size_t Replay::stream(const Answer& a, char* out, size_t cap) {
    if (a.silent || cap < 3) return 0;
    size_t o = 0;
    for (const char* p = a.text; *p;) {
        const char* nl = strchr(p, '\n');
        const size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len && o + len + 1 < cap - 2) {
            memcpy(out + o, p, len);
            o += len;
            out[o++] = '\r';
        }
        p += len + (nl ? 1 : 0);
    }
    out[o++] = '\r';
    out[o++] = '>';
    return o;
}

bool Replay::values(uint32_t t, Snapshot& out, uint32_t now) const {
    if (_values.empty()) return false;
    size_t lo = 0, hi = _values.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (_values[mid].t <= t) lo = mid + 1;
        else hi = mid;
    }
    const Record& r = _values[lo ? lo - 1 : 0];
    return parseValues(r.text, r.len, out, now, 3000) > 0;
}

bool Replay::nextNote(uint32_t a, uint32_t b, size_t& cursor, Record& r) const {
    while (cursor < _notes.size()) {
        const Record& n = _notes[cursor];
        if (n.t > b) return false;
        cursor++;
        if (n.t > a) {
            r = n;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- the recorded adapter

void ReplayAdapter::begin(const Replay* r, uint32_t now, float speed) {
    _r = r;
    _began = now;
    _speed = speed > 0.05f ? speed : 1.0f;
    _cmdLen = _outLen = _outPos = 0;
    _asked = _unknown = _silent = 0;
}

uint32_t ReplayAdapter::logTime(uint32_t now) const {
    if (!_r) return 0;
    const uint64_t elapsed = (uint64_t)((float)(uint32_t)(now - _began) * _speed);
    const uint64_t round = (uint64_t)_r->duration() + 1000;  // a second's pause between the end and the start
    return _r->start() + (uint32_t)(elapsed % round);
}

uint32_t ReplayAdapter::loops(uint32_t now) const {
    if (!_r) return 0;
    const uint64_t elapsed = (uint64_t)((float)(uint32_t)(now - _began) * _speed);
    return (uint32_t)(elapsed / ((uint64_t)_r->duration() + 1000));
}

void ReplayAdapter::write(const char* data, size_t n, uint32_t now) {
    for (size_t i = 0; i < n; i++) {
        const char c = data[i];
        if (c != '\r' && c != '\n') {
            if (_cmdLen + 1 < sizeof(_cmd)) _cmd[_cmdLen++] = c;
            continue;
        }
        if (!_cmdLen || !_r) continue;
        _cmd[_cmdLen] = '\0';
        _cmdLen = 0;
        const Replay::Answer a = _r->answer(_cmd, logTime(now));
        _asked++;
        if (!a.known) _unknown++;
        if (a.silent) _silent++;
        _outLen = Replay::stream(a, _out, sizeof(_out));  // a new command drops an unread old answer, like an ELM327
        _outPos = 0;
        // as slow as the car was (a search takes seconds), under the driver's longest wait (12 s)
        const uint32_t latency = a.latencyMs < 11000 ? a.latencyMs : 11000;
        _due = now + (uint32_t)((float)latency / _speed);
    }
}

size_t ReplayAdapter::read(char* out, size_t cap, uint32_t now) {
    if (_outPos >= _outLen || (int32_t)(now - _due) < 0) return 0;
    const size_t n = _outLen - _outPos < cap ? _outLen - _outPos : cap;
    memcpy(out, _out + _outPos, n);
    _outPos += n;
    return n;
}

}  // namespace obdlog
}  // namespace vehicle
