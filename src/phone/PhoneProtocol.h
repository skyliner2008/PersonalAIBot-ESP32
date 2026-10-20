/**
 * @file PhoneProtocol.h
 * @brief Messages between the phone app (ESP32-AI Flasher) and the board, and when to say the next turn
 *        (header-only, host tested)
 * @see wiki/18-Phone-Link-Navigation-Plan.md
 *
 * The same one-line messages over every transport (USB console, Wi-Fi TCP):
 *   phone -> board   phone {"t":"gps","lat":13.75,"lon":100.5,"spd":42.5,"hdg":270,"acc":4}
 *                    phone {"t":"nav","icon":"right","text":"เลี้ยวขวาเข้า ถ.พระราม 4","dist_m":350,"eta":"16:40","left_km":5.4,"left_min":12}
 *                    phone {"t":"nav","end":true} · {"t":"place","name":"..."} · {"t":"result","id":3,"ok":false,"text":"..."}
 *                    phone {"t":"pair","token":"..."} (USB only: plugging the cable in pairs) · {"t":"ping"}
 *   board -> phone   PHONE {"id":3,"do":"navigate","to":"เซ็นทรัลเวิลด์"} · {"id":4,"do":"stop"}
 * Wi-Fi handshake (the token never crosses the air - anyone on the same Wi-Fi could read it):
 *   board  hello {"nonce":"<board nonce>","fw":"4.2.0"}
 *   phone  phone {"t":"hello","app":"1.2","nonce":"<phone nonce>","proof":hmac(token, "phone|<board nonce>")}
 *   board  auth {"proof":hmac(token, "board|<phone nonce>")}
 * Neither side takes anything else before the other one proved the token.
 * Google Maps on the phone works out the route; the app reads its navigation notification.
 */

#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../util/Sha256.h"
#include "../util/Utf8.h"

namespace phone {

constexpr uint16_t kTcpPort = 47800;        // the app listens; the board connects (to the hotspot gateway)
constexpr uint16_t kUdpPort = 47801;        // the app announces itself on a shared Wi-Fi
constexpr uint32_t kGpsStaleMs = 5000;      // the app sends a fix every second
constexpr uint32_t kNavStaleMs = 30000;     // Maps refreshes its notification as the distance changes
constexpr uint32_t kLinkTimeoutMs = 10000;  // nothing (gps / ping) for this long: the phone is gone

/** Manoeuvre of the next step (order: append only, the app sends the key). */
enum class Icon : uint8_t {
    None, Straight, Left, Right, SlightLeft, SlightRight, SharpLeft, SharpRight, UTurn,
    KeepLeft, KeepRight, Roundabout, Merge, Arrive, Depart, Count
};

inline const char* iconKey(Icon i) {
    static const char* const k[] = {"none", "straight", "left", "right", "slight_left", "slight_right", "sharp_left",
                                    "sharp_right", "uturn", "keep_left", "keep_right", "roundabout", "merge", "arrive",
                                    "depart"};
    return (uint8_t)i < (uint8_t)Icon::Count ? k[(uint8_t)i] : "none";
}

inline Icon parseIcon(const char* s) {
    if (!s) return Icon::None;
    for (uint8_t i = 0; i < (uint8_t)Icon::Count; i++) {
        if (!strcmp(s, iconKey((Icon)i))) return (Icon)i;
    }
    return Icon::None;
}

struct Gps {
    double lat = 0, lon = 0;
    float speedKmh = 0;
    float heading = -1;  // degrees, -1 = unknown (standing still)
    float accuracy = 0;  // metres
    uint32_t at = 0;
    bool valid = false;
    bool fresh(uint32_t now) const { return valid && (uint32_t)(now - at) <= kGpsStaleMs; }
};

struct Nav {
    bool active = false;
    Icon icon = Icon::None;
    char text[192] = "";  // what Maps says for the next step, e.g. "เลี้ยวขวาเข้า ถ.พระราม 4" (Thai: 3 bytes a letter)
    int32_t distM = -1;   // to the next step, -1 = unknown
    char eta[8] = "";     // "16:40"
    float leftKm = -1;
    int leftMin = -1;
    uint32_t at = 0;
    bool fresh(uint32_t now) const { return active && (uint32_t)(now - at) <= kNavStaleMs; }
};

enum class Kind : uint8_t { Unknown, Hello, Pair, Gps, Nav, NavEnd, Place, Result, Ping };

struct Msg {
    Kind kind = Kind::Unknown;
    Gps gps;
    Nav nav;
    char text[128] = "";   // place name / result text
    char token[48] = "";   // pair
    char nonce[40] = "";   // hello
    char proof[72] = "";   // hello
    char app[16] = "";
    int id = 0;
    bool ok = false;
};

inline void copyText(char* dst, size_t n, const char* src) {
    if (!src) src = "";
    const size_t len = utf8::clampToBoundary(src, strlen(src), n - 1);  // never half a Thai character
    memcpy(dst, src, len);
    dst[len] = '\0';
    for (size_t i = 0; i < len; i++) {
        if (dst[i] == '\n' || dst[i] == '\r') dst[i] = ' ';  // one line, always
    }
}

/** The JSON after "phone ". False = not a message we know (ignored). */
inline bool parse(const char* json, Msg& m, uint32_t now) {
    m = Msg();
    if (!json) return false;
    JsonDocument d;
    if (deserializeJson(d, json) || !d.is<JsonObject>()) return false;
    const char* t = d["t"] | "";
    if (!strcmp(t, "gps")) {
        if (!d["lat"].is<double>() || !d["lon"].is<double>()) return false;
        const double lat = d["lat"], lon = d["lon"];
        if (!(lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180)) return false;
        m.kind = Kind::Gps;
        m.gps.lat = lat;
        m.gps.lon = lon;
        const float spd = d["spd"] | 0.0f;
        m.gps.speedKmh = spd >= 0 && spd < 400 ? spd : 0;
        m.gps.heading = d["hdg"] | -1.0f;
        m.gps.accuracy = d["acc"] | 0.0f;
        m.gps.at = now;
        m.gps.valid = true;
        return true;
    }
    if (!strcmp(t, "nav")) {
        if (d["end"] | false) return (m.kind = Kind::NavEnd), true;
        m.kind = Kind::Nav;
        m.nav.active = true;
        m.nav.icon = parseIcon(d["icon"] | "");
        copyText(m.nav.text, sizeof(m.nav.text), d["text"] | "");
        m.nav.distM = d["dist_m"].is<int>() ? (int32_t)d["dist_m"] : -1;
        if (m.nav.distM < -1 || m.nav.distM > 2000000) m.nav.distM = -1;
        copyText(m.nav.eta, sizeof(m.nav.eta), d["eta"] | "");
        m.nav.leftKm = d["left_km"] | -1.0f;
        m.nav.leftMin = d["left_min"] | -1;
        m.nav.at = now;
        return true;
    }
    if (!strcmp(t, "place")) {
        m.kind = Kind::Place;
        copyText(m.text, sizeof(m.text), d["name"] | "");
        return m.text[0] != '\0';
    }
    if (!strcmp(t, "result")) {
        m.kind = Kind::Result;
        m.id = d["id"] | 0;
        m.ok = d["ok"] | false;
        copyText(m.text, sizeof(m.text), d["text"] | "");
        return true;
    }
    if (!strcmp(t, "hello") || !strcmp(t, "pair")) {
        m.kind = !strcmp(t, "hello") ? Kind::Hello : Kind::Pair;
        copyText(m.token, sizeof(m.token), d["token"] | "");
        copyText(m.nonce, sizeof(m.nonce), d["nonce"] | "");
        copyText(m.proof, sizeof(m.proof), d["proof"] | "");
        copyText(m.app, sizeof(m.app), d["app"] | "");
        return true;
    }
    if (!strcmp(t, "ping")) return (m.kind = Kind::Ping), true;
    return false;
}

/** A pairing token from the app: 16-64 characters [0-9a-zA-Z_-]. */
inline bool validToken(const char* t) {
    const size_t n = t ? strlen(t) : 0;
    if (n < 16 || n > 47) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = t[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
    }
    return true;
}

/** Same length, every byte compared (no early exit on the first difference). */
inline bool sameToken(const char* a, const char* b) {
    const size_t n = strlen(a);
    if (!n || n != strlen(b)) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/** A handshake nonce: 16-32 hex characters. */
inline bool validNonce(const char* n) {
    const size_t len = n ? strlen(n) : 0;
    if (len < 16 || len > 32) return false;
    for (size_t i = 0; i < len; i++) {
        const char c = n[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

/** Proof that `role` ("phone" / "board") holds the paired token: hex(HMAC-SHA256(token, role + "|" + nonce)). */
inline void proof(const char* token, const char* role, const char* nonce, char out[65]) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s|%s", role, nonce);
    uint8_t mac[32];
    sha256::hmac(token, strlen(token), msg, strlen(msg), mac);
    sha256::toHex(mac, out);
}

/** The other side's proof for our nonce (constant time). */
inline bool proofOk(const char* token, const char* role, const char* nonce, const char* got) {
    if (!token[0] || !validNonce(nonce) || !got || strlen(got) != 64) return false;
    char want[65];
    proof(token, role, nonce, want);
    return sameToken(want, got);
}

/** Spoken distance: "350 เมตร", "1.2 กิโลเมตร" (short = "350 ม.", "1.2 กม." for the screen). */
inline void formatDistance(int32_t m, char* out, size_t n, bool shortUnits = false) {
    if (m < 0) {
        snprintf(out, n, "-");
    } else if (m < 1000) {
        const int r = m >= 100 ? (int)((m + 25) / 50) * 50 : (int)((m + 5) / 10) * 10;  // as Maps rounds it
        if (r >= 1000) snprintf(out, n, shortUnits ? "1 กม." : "1 กิโลเมตร");
        else snprintf(out, n, shortUnits ? "%d ม." : "%d เมตร", r);
    } else {
        const float km = m / 1000.0f;
        if (km < 10) snprintf(out, n, shortUnits ? "%.1f กม." : "%.1f กิโลเมตร", km);
        else snprintf(out, n, shortUnits ? "%.0f กม." : "%.0f กิโลเมตร", km);
    }
}

inline uint32_t hashText(const char* s) {  // FNV-1a
    uint32_t h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/**
 * When the assistant should say the next turn: once per step, ~20 s before it (at least 200 m, at most 800 m
 * ahead), never closer than 30 m (too late), and not twice within 8 s. Arrival is said once near the end.
 */
class TurnAnnouncer {
public:
    static constexpr int kMinLeadM = 200, kMaxLeadM = 800, kTooLateM = 30;
    static constexpr uint32_t kGapMs = 8000;

    static int leadMetres(float speedKmh) {
        const int lead = (int)(speedKmh / 3.6f * 20.0f);
        return lead < kMinLeadM ? kMinLeadM : lead > kMaxLeadM ? kMaxLeadM : lead;
    }

    /** True = say `out` now (Thai, one sentence). */
    bool update(const Nav& nav, float speedKmh, uint32_t now, char* out, size_t n) {
        if (!nav.active) {
            reset();
            return false;
        }
        if (!nav.fresh(now) || nav.distM < 0) return false;
        char key[224];
        snprintf(key, sizeof(key), "%s|%s", iconKey(nav.icon), nav.text);
        const uint32_t step = hashText(key);
        if (step != _step) {
            _step = step;
            _said = false;
        }
        if (_said) return false;
        const bool arrive = nav.icon == Icon::Arrive;
        if (nav.distM < kTooLateM && !arrive) {
            _said = true;  // passed it already: the next step is what matters
            return false;
        }
        if (nav.distM > (arrive ? 150 : leadMetres(speedKmh))) return false;
        if (_lastAt && (uint32_t)(now - _lastAt) < kGapMs) return false;
        char dist[32];
        formatDistance(nav.distM, dist, sizeof(dist));
        const char* what = nav.text[0] ? nav.text : iconTh(nav.icon);
        if (arrive && nav.distM < 50) snprintf(out, n, "%s", nav.text[0] ? nav.text : "ถึงจุดหมายแล้ว");
        else snprintf(out, n, "อีก %s %s", dist, what);
        _said = true;
        _lastAt = now ? now : 1;
        return true;
    }

    void reset() {
        _step = 0;
        _said = false;
    }

    static const char* iconTh(Icon i) {
        switch (i) {
            case Icon::Straight: return "ตรงไป";
            case Icon::Left: return "เลี้ยวซ้าย";
            case Icon::Right: return "เลี้ยวขวา";
            case Icon::SlightLeft: return "เบี่ยงซ้าย";
            case Icon::SlightRight: return "เบี่ยงขวา";
            case Icon::SharpLeft: return "หักซ้าย";
            case Icon::SharpRight: return "หักขวา";
            case Icon::UTurn: return "กลับรถ";
            case Icon::KeepLeft: return "ชิดซ้าย";
            case Icon::KeepRight: return "ชิดขวา";
            case Icon::Roundabout: return "เข้าวงเวียน";
            case Icon::Merge: return "เข้าทางหลัก";
            case Icon::Arrive: return "ถึงจุดหมาย";
            case Icon::Depart: return "ออกเดินทาง";
            default: return "";
        }
    }

private:
    uint32_t _step = 0;
    bool _said = false;
    uint32_t _lastAt = 0;
};

}  // namespace phone
