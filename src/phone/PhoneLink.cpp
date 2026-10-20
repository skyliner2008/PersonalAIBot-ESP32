/**
 * @file PhoneLink.cpp
 * @brief Phone link: state from "phone {...}" lines, pairing in NVS, the Wi-Fi task (wiki 18)
 */

#include "PhoneLink.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>

#include "../app/AppConfig.h"
#include "../system/PsramTask.h"

using namespace phone;

PhoneLink gPhone;

void PhoneLink::begin() {
    Preferences p;
    if (p.begin("phone", true)) {
        p.getString("token", _token, sizeof(_token));
        p.end();
    }
    if (!validToken(_token)) _token[0] = '\0';
    _out = xQueueCreateWithCaps(6, kOutLen, MALLOC_CAP_SPIRAM);
    Serial.printf("[Phone] %s\n", _token[0] ? "paired - Wi-Fi link on" : "not paired (connect the phone app over USB once)");
    if (_token[0]) startTask();
}

void PhoneLink::savePairing(const char* token) {  // the console task: NVS needs an internal stack
    Preferences p;
    if (p.begin("phone", false)) {
        p.putString("token", token ? token : "");
        p.end();
    }
    portENTER_CRITICAL(&_mux);
    snprintf(_token, sizeof(_token), "%s", token ? token : "");
    portEXIT_CRITICAL(&_mux);
}

bool PhoneLink::paired() {
    portENTER_CRITICAL(&_mux);
    const bool p = _token[0] != '\0';
    portEXIT_CRITICAL(&_mux);
    return p;
}

void PhoneLink::unpair() {
    savePairing("");
    portENTER_CRITICAL(&_mux);
    _lastWifi = 0;
    portEXIT_CRITICAL(&_mux);
    Serial.println("[Phone] unpaired");
}

void PhoneLink::onLine(const char* json, Via via) {
    Msg m;
    const uint32_t now = millis();
    if (!parse(json, m, now)) return;
    if (m.kind == Kind::Pair) {
        if (via != Via::Usb) return;  // only the cable pairs
        if (!validToken(m.token)) {
            Serial.println("ERR phone pair: bad token");
            return;
        }
        const bool fresh = !paired();
        char cur[sizeof(_token)];
        portENTER_CRITICAL(&_mux);
        memcpy(cur, _token, sizeof(cur));
        portEXIT_CRITICAL(&_mux);
        if (strcmp(cur, m.token)) savePairing(m.token);
        Serial.println("OK phone paired");
        if (fresh) startTask();
    }
    if (m.kind == Kind::Hello) return;  // the Wi-Fi handshake (task); over USB nothing to prove
    portENTER_CRITICAL(&_mux);
    if (via == Via::Usb) _lastUsb = now;
    else _lastWifi = now;
    _via = via;
    switch (m.kind) {
        case Kind::Gps:
            _gps = m.gps;
            break;
        case Kind::Nav:
            if (!_nav.fresh(now)) _navStarted = true;
            _nav = m.nav;
            break;
        case Kind::NavEnd:
            if (_nav.active) _navEnded = true;
            _nav = Nav();
            break;
        case Kind::Place:
            memcpy(_place, m.text, sizeof(_place));
            break;
        case Kind::Result:
            if (!m.ok && m.text[0]) snprintf(_failure, sizeof(_failure), "%s", m.text);
            break;
        default:
            break;
    }
    portEXIT_CRITICAL(&_mux);
}

bool PhoneLink::connected() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&_mux);
    const bool usb = _lastUsb && (uint32_t)(now - _lastUsb) <= kLinkTimeoutMs;
    const bool wifi = _lastWifi && (uint32_t)(now - _lastWifi) <= kLinkTimeoutMs;
    portEXIT_CRITICAL(&_mux);
    return usb || wifi;
}

PhoneLink::Via PhoneLink::via() {
    if (!connected()) return Via::None;
    portENTER_CRITICAL(&_mux);
    const Via v = _via;
    portEXIT_CRITICAL(&_mux);
    return v;
}

Gps PhoneLink::gps() {
    portENTER_CRITICAL(&_mux);
    const Gps g = _gps;
    portEXIT_CRITICAL(&_mux);
    return g;
}

Nav PhoneLink::nav() {
    portENTER_CRITICAL(&_mux);
    const Nav n = _nav;
    portEXIT_CRITICAL(&_mux);
    return n;
}

String PhoneLink::place() {
    char p[sizeof(_place)];
    portENTER_CRITICAL(&_mux);
    memcpy(p, _place, sizeof(p));
    portEXIT_CRITICAL(&_mux);
    return String(p);
}

int PhoneLink::request(const char* what, const char* to) {
    if (!connected()) return 0;
    portENTER_CRITICAL(&_mux);
    const int id = _nextId++;
    portEXIT_CRITICAL(&_mux);
    JsonDocument d;
    d["id"] = id;
    d["do"] = what;
    char dest[120];
    if (to && *to) {
        copyText(dest, sizeof(dest), to);
        d["to"] = dest;
    }
    char buf[kOutLen];
    if (serializeJson(d, buf, sizeof(buf)) >= sizeof(buf) - 1) return 0;
    sendLine(buf);
    Serial.printf("[Phone] -> %s %s\n", what, to ? to : "");
    return id;
}

void PhoneLink::sendLine(const char* json) {
    if (via() == Via::Usb) {
        String line = "PHONE ";
        line += json;
        line += '\n';
        Serial.write((const uint8_t*)line.c_str(), line.length());  // one write: never split by another task
    } else if (_out) {
        char buf[kOutLen];
        snprintf(buf, sizeof(buf), "%s", json);
        xQueueSend(_out, buf, 0);
    }
}

bool PhoneLink::takeSay(String& msg, bool& turn, uint32_t now) {
    char failure[sizeof(_failure)];
    portENTER_CRITICAL(&_mux);
    memcpy(failure, _failure, sizeof(failure));
    _failure[0] = '\0';
    if (_nav.active && !_nav.fresh(now)) {  // Maps went quiet (phone gone, route over): navigation ended
        _nav = Nav();
        _navEnded = true;
    }
    const Nav nav = _nav;
    const Gps g = _gps;
    portEXIT_CRITICAL(&_mux);
    if (failure[0]) {
        msg = failure;
        turn = false;
        return true;
    }
    char out[256];
    if (_turns.update(nav, g.fresh(now) ? g.speedKmh : 0, now, out, sizeof(out))) {
        msg = out;
        turn = true;
        return true;
    }
    return false;
}

void PhoneLink::describe(JsonObject out) {
    const uint32_t now = millis();
    out["paired"] = paired();
    out["connected"] = connected();
    out["via"] = viaName(via());
    const Gps g = gps();
    if (g.fresh(now)) {
        out["gps"] = true;
        out["speed_kmh"] = (int)roundf(g.speedKmh);
        out["gps_accuracy_m"] = (int)roundf(g.accuracy);
    } else {
        out["gps"] = false;
    }
    const String p = place();
    if (p.length()) out["place"] = p;
    const Nav n = nav();
    out["navigating"] = n.fresh(now);
    if (n.fresh(now)) {
        JsonObject o = out["next_step"].to<JsonObject>();
        o["instruction"] = n.text[0] ? n.text : phone::TurnAnnouncer::iconTh(n.icon);
        if (n.distM >= 0) o["in_m"] = n.distM;
        if (n.eta[0]) out["eta"] = n.eta;
        if (n.leftKm >= 0) out["left_km"] = roundf(n.leftKm * 10) / 10;
        if (n.leftMin >= 0) out["left_min"] = n.leftMin;
    }
}

// ---------------------------------------------------------------- Wi-Fi

void PhoneLink::startTask() {
    if (_task) return;
    if (!startPsramTask(taskEntry, "phone", 4096, this, 2, &_task, 0)) {
        _task = nullptr;
        Serial.println("[Phone] no task");
    }
}

void PhoneLink::taskEntry(void* self) { static_cast<PhoneLink*>(self)->loop(); }

void PhoneLink::loop() {
    WiFiClient client;
    WiFiUDP udp;
    bool udpOn = false, proven = false;
    IPAddress announced;
    uint32_t announcedAt = 0, nextTry = 0, lastRx = 0;
    char nonce[33] = "", token[sizeof(_token)];
    char line[512];
    size_t len = 0;
    char out[kOutLen];
    auto drop = [&](const char* why) {
        if (client.connected()) Serial.printf("[Phone] Wi-Fi: %s\n", why);
        client.stop();
        proven = false;
        len = 0;
    };
    for (;;) {
        portENTER_CRITICAL(&_mux);
        memcpy(token, _token, sizeof(token));
        portEXIT_CRITICAL(&_mux);
        if (!token[0] || WiFi.status() != WL_CONNECTED) {
            drop("off");
            if (udpOn) udp.stop();
            udpOn = false;
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        if (!udpOn) udpOn = udp.begin(kUdpPort);
        // on a shared Wi-Fi the app says where it is: "PABPHONE 47800"
        while (udpOn && udp.parsePacket() > 0) {
            char b[24];
            const int r = udp.read(b, sizeof(b) - 1);
            b[r > 0 ? r : 0] = '\0';
            if (!strncmp(b, "PABPHONE", 8)) {
                announced = udp.remoteIP();
                announcedAt = millis();
            }
        }
        const uint32_t now = millis();
        if (!client.connected()) {
            if (proven) drop("closed");
            if ((int32_t)(now - nextTry) >= 0) {
                nextTry = now + 4000;
                // the phone's hotspot: the phone is the gateway; else the address it announced
                const IPAddress ip = announcedAt && now - announcedAt < 30000 ? announced : WiFi.gatewayIP();
                if (ip != IPAddress(0, 0, 0, 0) && client.connect(ip, kTcpPort, 1500)) {
                    client.setNoDelay(true);
                    snprintf(nonce, sizeof(nonce), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
                    char hello[96];
                    snprintf(hello, sizeof(hello), "hello {\"nonce\":\"%s\",\"fw\":\"%s\"}\n", nonce, FIRMWARE_VERSION);
                    client.print(hello);
                    lastRx = now;
                    len = 0;
                    Serial.printf("[Phone] Wi-Fi: connected to %s, proving the pairing\n", ip.toString().c_str());
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        while (client.available()) {
            const int c = client.read();
            if (c < 0) break;
            if (c == '\n') {
                line[len] = '\0';
                len = 0;
                if (strncmp(line, "phone ", 6)) continue;  // only phone messages over the air
                const char* json = line + 6;
                lastRx = millis();
                if (!proven) {
                    Msg m;
                    if (!parse(json, m, lastRx) || m.kind != Kind::Hello || !proofOk(token, "phone", nonce, m.proof) ||
                        !validNonce(m.nonce)) {
                        drop("the phone did not prove the pairing - pair it again over USB");
                        nextTry = millis() + 15000;
                        break;
                    }
                    char mine[65], auth[96];
                    proof(token, "board", m.nonce, mine);
                    snprintf(auth, sizeof(auth), "auth {\"proof\":\"%s\"}\n", mine);
                    client.print(auth);
                    proven = true;
                    Serial.printf("[Phone] Wi-Fi: phone app %s paired and connected\n", m.app);
                    onLine("{\"t\":\"ping\"}", Via::Wifi);
                    continue;
                }
                onLine(json, Via::Wifi);
            } else if (c != '\r') {
                if (len + 1 < sizeof(line)) line[len++] = (char)c;
                else len = 0;  // longer than any phone message: drop it
            }
        }
        while (proven && xQueueReceive(_out, out, 0) == pdTRUE) {
            client.print("PHONE ");
            client.print(out);
            client.print("\n");
        }
        if ((uint32_t)(millis() - lastRx) > kLinkTimeoutMs) {
            drop("phone silent - reconnecting");
            nextTry = millis() + 2000;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
