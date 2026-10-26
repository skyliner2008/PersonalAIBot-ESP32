/**
 * @file WifiKnown.h
 * @brief Wi-Fi networks the board connected to before (T099): remembered on every successful connection, the
 *        strongest visible one is joined when the configured network is not there. Portable (host tests).
 *
 * Passwords are kept in RAM here; Settings seals each one (SecretBox) before it reaches NVS or the SD card.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wifiknown {

constexpr size_t kMax = 6;  // the least recently used is forgotten when a 7th is added

struct Net {
    char ssid[33] = "";
    char pass[65] = "";  // "" = open network
    uint32_t used = 0;   // order of last use (a counter: no clock needed before NTP)
};

/** A network seen in a scan. */
struct Seen {
    const char* ssid;
    int rssi;
};

class List {
public:
    List() = default;
    List(const List&) = default;
    List& operator=(const List&) = default;
    /** Copies are handed around (menus, scans): each wipes its passwords when it goes out of scope. */
    ~List() {
        for (Net& n : _items) {
            volatile char* p = n.pass;  // volatile: not optimised away as a dead store
            for (size_t i = 0; i < sizeof(n.pass); i++) p[i] = 0;
        }
    }

    size_t size() const { return _count; }
    const Net& at(size_t i) const { return _items[i]; }

    int find(const char* ssid) const {
        for (size_t i = 0; i < _count; i++) {
            if (!strcmp(_items[i].ssid, ssid)) return (int)i;
        }
        return -1;
    }

    /** Connected to `ssid` with `pass`: add it, or update it and make it the most recent. true = changed. */
    bool remember(const char* ssid, const char* pass) {
        if (!ssid || !*ssid || strlen(ssid) >= sizeof(Net::ssid) || strlen(pass ? pass : "") >= sizeof(Net::pass)) {
            return false;
        }
        int i = find(ssid);
        const uint32_t top = maxUsed();
        if (i >= 0) {
            Net& n = _items[i];
            const bool samePass = !strcmp(n.pass, pass ? pass : "");
            if (samePass && n.used == top && top != 0) return false;  // already the latest: no flash write
            copy(n.pass, sizeof(n.pass), pass);
            n.used = top + 1;
            return true;
        }
        if (_count == kMax) {  // forget the least recently used
            size_t oldest = 0;
            for (size_t k = 1; k < _count; k++) {
                if (_items[k].used < _items[oldest].used) oldest = k;
            }
            removeAt(oldest);
        }
        Net& n = _items[_count++];
        copy(n.ssid, sizeof(n.ssid), ssid);
        copy(n.pass, sizeof(n.pass), pass);
        n.used = top + 1;
        return true;
    }

    /** Load path: entries as stored (keeps their order numbers). false = full or invalid. */
    bool add(const char* ssid, const char* pass, uint32_t used) {
        if (_count == kMax || !ssid || !*ssid || find(ssid) >= 0 || strlen(ssid) >= sizeof(Net::ssid) ||
            strlen(pass ? pass : "") >= sizeof(Net::pass)) {
            return false;
        }
        Net& n = _items[_count++];
        copy(n.ssid, sizeof(n.ssid), ssid);
        copy(n.pass, sizeof(n.pass), pass);
        n.used = used;
        return true;
    }

    bool forget(const char* ssid) {
        const int i = find(ssid);
        if (i < 0) return false;
        removeAt((size_t)i);
        return true;
    }

    /**
     * The known network to join from a scan: the strongest signal; within 8 dB the more recently used one
     * (a phone hotspot used yesterday beats the office Wi-Fi of last month at about the same strength).
     * -1 = none of them is in range.
     */
    int pick(const Seen* seen, size_t count) const {
        int strongest = -1000;  // 1. the strongest known network in range
        for (size_t s = 0; s < count; s++) {
            if (seen[s].ssid && find(seen[s].ssid) >= 0 && seen[s].rssi > strongest) strongest = seen[s].rssi;
        }
        int best = -1;  // 2. of those within 8 dB of it, the most recently used
        for (size_t s = 0; s < count; s++) {
            const int i = seen[s].ssid ? find(seen[s].ssid) : -1;
            if (i < 0 || seen[s].rssi < strongest - 8) continue;
            if (best < 0 || _items[i].used > _items[best].used) best = i;
        }
        return best;
    }

private:
    Net _items[kMax];
    size_t _count = 0;

    uint32_t maxUsed() const {
        uint32_t m = 0;
        for (size_t i = 0; i < _count; i++) m = _items[i].used > m ? _items[i].used : m;
        return m;
    }

    void removeAt(size_t i) {
        for (size_t k = i + 1; k < _count; k++) _items[k - 1] = _items[k];
        _count--;
        _items[_count] = Net();
    }

    static void copy(char* dst, size_t cap, const char* src) {
        strncpy(dst, src ? src : "", cap - 1);
        dst[cap - 1] = '\0';
    }
};

}  // namespace wifiknown
