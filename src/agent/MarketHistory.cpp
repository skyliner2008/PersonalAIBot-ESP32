/**
 * @file MarketHistory.cpp
 * @brief Candle history for market_chart (T142): the TradingView chart websocket, Yahoo Finance as a fallback
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * TradingView: wss://data.tradingview.com/socket.io/websocket (Origin www.tradingview.com, no login - the
 * same requests as the PersonalAIBot app's TvHistoryBridge). One short-lived connection per tool call with
 * one chart session per symbol (the main one, the candidates of a bare ticker, the ones to compare):
 * set_auth_token, then chart_create_session -> resolve_symbol -> create_series for each, read until every
 * series is completed or failed (~1 s for 6 symbols, ~115 B per bar). Messages are "~m~<len>~m~<json>"
 * (util/MarketMath.h); "~h~<n>" heartbeats are echoed.
 * Yahoo: query1.finance.yahoo.com/v8/finance/chart (no key) when TradingView cannot be reached.
 * Both run in the webTools task; buffers and parsed JSON live in PSRAM.
 */

#include <esp_random.h>

#include "../util/Base64.h"
#include "MarketHistory.h"
#include "WebTools.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr const char* kTvHost = "data.tradingview.com";
constexpr uint32_t kTvTimeoutMs = 10000;
constexpr size_t kMaxMessage = 512 * 1024;  // a 300-bar reply is ~35 KB; several sessions can share a frame

bool readExact(WiFiClientSecure& tls, uint8_t* dst, size_t n, uint32_t deadline) {
    size_t got = 0;
    while (got < n) {
        if ((int32_t)(millis() - deadline) > 0 || !tls.connected()) return false;
        const int avail = tls.available();
        if (avail <= 0) {
            delay(5);
            continue;
        }
        const int r = tls.read(dst + got, n - got < (size_t)avail ? n - got : (size_t)avail);
        if (r > 0) got += r;
    }
    return true;
}

/** One masked client frame (text = 0x1, pong = 0xA, close = 0x8). */
bool sendFrame(WiFiClientSecure& tls, uint8_t opcode, const char* data, size_t n) {
    uint8_t head[14];
    size_t h = 0;
    head[h++] = 0x80 | opcode;
    if (n < 126) {
        head[h++] = 0x80 | (uint8_t)n;
    } else if (n < 65536) {
        head[h++] = 0x80 | 126;
        head[h++] = n >> 8;
        head[h++] = n & 0xFF;
    } else {
        return false;  // our requests are a few hundred bytes
    }
    const uint32_t mask = esp_random();
    memcpy(head + h, &mask, 4);
    const uint8_t* m = head + h;
    h += 4;
    uint8_t* buf = (uint8_t*)psramMalloc(h + n);
    if (!buf) return false;
    memcpy(buf, head, h);
    for (size_t i = 0; i < n; i++) buf[h + i] = (uint8_t)data[i] ^ m[i & 3];
    const bool ok = tls.write(buf, h + n) == h + n;
    psramFree(buf);
    return ok;
}

bool sendTv(WiFiClientSecure& tls, const std::string& json) {
    const std::string framed = mkt::tvWrap(json);
    return sendFrame(tls, 0x1, framed.data(), framed.size());
}

/** Next complete message (text / binary, continuation frames joined); answers pings. False = closed / timeout. */
bool readMessage(WiFiClientSecure& tls, PsString& msg, uint32_t deadline) {
    msg.clear();
    for (;;) {
        uint8_t h[2];
        if (!readExact(tls, h, 2, deadline)) return false;
        const uint8_t op = h[0] & 0x0F;
        const bool fin = h[0] & 0x80, masked = h[1] & 0x80;
        uint64_t len = h[1] & 0x7F;
        if (len == 126) {
            uint8_t e[2];
            if (!readExact(tls, e, 2, deadline)) return false;
            len = (e[0] << 8) | e[1];
        } else if (len == 127) {
            uint8_t e[8];
            if (!readExact(tls, e, 8, deadline)) return false;
            len = 0;
            for (uint8_t b : e) len = (len << 8) | b;
        }
        uint8_t key[4] = {0, 0, 0, 0};
        if (masked && !readExact(tls, key, 4, deadline)) return false;
        if (len > kMaxMessage || msg.size() + len > kMaxMessage) return false;
        const bool data = op == 0x0 || op == 0x1 || op == 0x2;
        PsString ctl;
        PsString& dst = data ? msg : ctl;
        const size_t at = data ? msg.size() : 0;
        dst.resize(at + (size_t)len);
        if (len && !readExact(tls, (uint8_t*)&dst[at], (size_t)len, deadline)) return false;
        if (masked) {
            for (size_t i = 0; i < len; i++) dst[at + i] ^= key[i & 3];
        }
        if (op == 0x8) return false;
        if (op == 0x9) {
            sendFrame(tls, 0xA, ctl.data(), ctl.size());
            continue;
        }
        if (op == 0xA) continue;
        if (fin) return true;
    }
}

/** {"m": method, "p": [params...]} with `number` inserted at index `numberAt` (-1 = none). */
std::string jsonCmd(const char* method, std::initializer_list<const char*> params, int number = -1, int numberAt = -1) {
    JsonDocument d;
    d["m"] = method;
    JsonArray p = d["p"].to<JsonArray>();
    int i = 0;
    for (const char* s : params) {
        if (i++ == numberAt) p.add(number);
        p.add(s);
    }
    if (numberAt >= i) p.add(number);
    std::string out;
    serializeJson(d, out);
    return out;
}

/** The same history from Yahoo's chart API (4-hour bars merged from 1-hour ones). */
bool fetchYahooBars(const String& tvSymbol, const mkt::Timeframe& tf, mkt::Bars& out, BarsInfo& info, String& err) {
    const std::string ysym = mkt::yahooSymbol(tvSymbol.c_str());
    if (ysym.empty()) {
        err = "no Yahoo symbol";
        return false;
    }
    const bool fourHours = !strcmp(tf.key, "4h");
    String url = String("https://query1.finance.yahoo.com/v8/finance/chart/") + urlEncode(ysym.c_str()) +
                 "?interval=" + tf.yInterval + "&range=" + tf.yRange + "&includePrePost=false";
    String body;
    setStage("market_chart: yahoo");
    const int code = httpGet(url, body);
    if (code != 200) {
        err = String("Yahoo HTTP ") + code;
        return false;
    }
    JsonDocument filter;
    JsonObject f = filter["chart"]["result"][0].to<JsonObject>();
    f["meta"]["currency"] = true;
    f["meta"]["longName"] = true;
    f["meta"]["shortName"] = true;
    f["timestamp"] = true;
    JsonObject fq = f["indicators"]["quote"][0].to<JsonObject>();
    for (const char* k : {"open", "high", "low", "close", "volume"}) fq[k] = true;
    JsonDocument doc(JsonPsramAllocator::instance());
    setStage("market_chart: yahoo parse");
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        err = "Yahoo JSON";
        return false;
    }
    body = String();
    JsonObjectConst r = doc["chart"]["result"][0];
    JsonArrayConst ts = r["timestamp"];
    JsonObjectConst q = r["indicators"]["quote"][0];
    JsonArrayConst o = q["open"], hi = q["high"], lo = q["low"], cl = q["close"], vo = q["volume"];
    out.clear();
    out.reserve(ts.size());
    for (size_t i = 0; i < ts.size(); i++) {
        if (cl[i].isNull()) continue;  // a gap in the session
        mkt::Bar b;
        b.t = ts[i] | 0u;
        b.c = cl[i].as<double>();
        b.o = o[i] | b.c;
        b.h = hi[i] | b.c;
        b.l = lo[i] | b.c;
        b.v = vo[i] | 0.0;
        out.push_back(b);
    }
    if (fourHours) mkt::mergeHours(out, 4);
    info.currency = r["meta"]["currency"] | "";
    info.name = r["meta"]["longName"] | (r["meta"]["shortName"] | "");
    info.source = "Yahoo Finance";
    info.delayMin = 0;
    if (out.empty()) err = "Yahoo: no bars";
    return !out.empty();
}

}  // namespace

bool fetchTradingView(std::vector<TvJob>& jobs, String& err) {
    if (jobs.empty()) return true;
    if (WiFi.status() != WL_CONNECTED) {
        err = "no Wi-Fi";
        return false;
    }
    const uint32_t t0 = millis();
    setStage("market_chart: connect");
    WiFiClientSecure tls;
    tls.setCACert(WEB_ROOT_CA_PEM);
    tls.setHandshakeTimeout(kTlsHandshakeSec);
    if (!tls.connect(kTvHost, 443, (int32_t)kTvTimeoutMs)) {
        err = "TradingView connect failed";
        Serial.printf("[Chart] %s\n", err.c_str());
        return false;
    }
    uint8_t rnd[16];
    for (int i = 0; i < 16; i += 4) {
        const uint32_t r = esp_random();
        memcpy(rnd + i, &r, 4);
    }
    char key[28];
    key[b64::encode(rnd, 16, key)] = '\0';
    String req = String("GET /socket.io/websocket?from=chart%2F HTTP/1.1\r\nHost: ") + kTvHost +
                 "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                 key + "\r\nOrigin: https://www.tradingview.com\r\nUser-Agent: Mozilla/5.0\r\n\r\n";
    tls.write((const uint8_t*)req.c_str(), req.length());
    const uint32_t deadline = millis() + kTvTimeoutMs;
    // the upgrade reply: headers up to the blank line
    String head;
    while (head.indexOf("\r\n\r\n") < 0) {
        uint8_t ch;
        if (!readExact(tls, &ch, 1, deadline) || head.length() > 4096) {
            err = "TradingView: no websocket reply";
            tls.stop();
            return false;
        }
        head += (char)ch;
    }
    if (head.indexOf(" 101 ") < 0) {
        err = "TradingView: " + head.substring(0, head.indexOf('\r'));
        tls.stop();
        return false;
    }
    // one chart session per symbol, all on this connection: the replies come together
    setStage("market_chart: request");
    const uint32_t tag = esp_random() & 0xFFFFFF;
    std::vector<std::string> sessions;
    sendTv(tls, jsonCmd("set_auth_token", {"unauthorized_user_token"}));
    for (size_t i = 0; i < jobs.size(); i++) {
        char session[20];
        snprintf(session, sizeof(session), "cs_%06lx_%u", (unsigned long)tag, (unsigned)i);
        sessions.push_back(session);
        JsonDocument sym;
        sym["symbol"] = jobs[i].symbol.c_str();
        sym["adjustment"] = "splits";
        sym["session"] = "regular";
        std::string symBody;
        serializeJson(sym, symBody);
        const std::string symJson = "=" + symBody;
        jobs[i].out->clear();
        jobs[i].info = BarsInfo();
        jobs[i].info.source = "TradingView";
        sendTv(tls, jsonCmd("chart_create_session", {session, ""}));
        sendTv(tls, jsonCmd("resolve_symbol", {session, "sds_sym_1", symJson.c_str()}));
        // create_series(session, "sds_1", "s1", "sds_sym_1", resolution, bars, "")
        sendTv(tls, jsonCmd("create_series", {session, "sds_1", "s1", "sds_sym_1", jobs[i].tf->tv, ""},
                            jobs[i].bars, 5));
    }

    setStage("market_chart: read");
    PsString msg;
    size_t open = jobs.size();
    while (open && readMessage(tls, msg, deadline)) {
        size_t pos = 0, start = 0, n = 0;
        while (mkt::nextTvMessage(msg.data(), msg.size(), pos, start, n)) {
            const char* p = msg.data() + start;
            if (n >= 3 && !strncmp(p, "~h~", 3)) {  // heartbeat: echo it
                sendTv(tls, std::string(p, n));
                continue;
            }
            if (n < 8 || *p != '{') continue;
            JsonDocument d(JsonPsramAllocator::instance());
            if (deserializeJson(d, p, n)) continue;
            const char* m = d["m"] | "";
            const char* sid = d["p"][0] | "";
            TvJob* job = nullptr;
            for (size_t i = 0; i < jobs.size() && !job; i++) {
                if (sessions[i] == sid) job = &jobs[i];
            }
            if (!job) {
                if (!strcmp(m, "critical_error") || !strcmp(m, "protocol_error")) {
                    err = String("TradingView ") + m;
                    open = 0;
                }
                continue;
            }
            if (job->done) continue;
            if (!strcmp(m, "symbol_resolved")) {
                JsonObjectConst s = d["p"][2];
                job->info.name = s["description"] | "";
                job->info.currency = s["currency_code"] | "";
            } else if (!strcmp(m, "timescale_update")) {
                JsonArrayConst bars = d["p"][1]["sds_1"]["s"];
                job->out->reserve(bars.size());
                for (JsonObjectConst b : bars) {
                    JsonArrayConst v = b["v"];
                    if (v.size() < 5) continue;
                    mkt::Bar x;
                    x.t = (uint32_t)(v[0] | 0.0);
                    x.o = v[1] | 0.0;
                    x.h = v[2] | 0.0;
                    x.l = v[3] | 0.0;
                    x.c = v[4] | 0.0;
                    x.v = v[5] | 0.0;
                    job->out->push_back(x);
                }
            } else if (!strcmp(m, "series_completed")) {
                const char* mode = d["p"][3] | "";
                if (!strncmp(mode, "delayed_streaming_", 18)) job->info.delayMin = atoi(mode + 18) / 60;
                job->done = true;
                open--;
            } else if (!strcmp(m, "symbol_error") || !strcmp(m, "series_error")) {
                job->badSymbol = !strcmp(m, "symbol_error");
                job->err = String("TradingView ") + m;
                job->done = true;
                open--;
            }
        }
    }
    sendFrame(tls, 0x8, "", 0);
    tls.stop();
    String line;
    for (TvJob& j : jobs) {
        line += ' ';
        line += j.symbol;
        line += j.badSymbol ? "=unknown" : (j.out->empty() ? "=none" : "=" + String((unsigned)j.out->size()));
        if (!j.badSymbol && j.out->empty() && j.err.isEmpty()) j.err = j.done ? "TradingView: no bars" : "TradingView: timeout";
    }
    Serial.printf("[Chart] %s:%s in %lu ms%s%s\n", jobs[0].tf->key, line.c_str(), (unsigned long)(millis() - t0),
                  err.length() ? " - " : "", err.c_str());
    return err.isEmpty();
}

bool fetchYahoo(const String& tvSymbol, const mkt::Timeframe& tf, uint16_t bars, mkt::Bars& out, BarsInfo& info,
                String& err) {
    if (!fetchYahooBars(tvSymbol, tf, out, info, err)) return false;
    if (out.size() > bars) out.erase(out.begin(), out.end() - bars);
    Serial.printf("[Chart] %s: Yahoo fallback, %u bars\n", tvSymbol.c_str(), (unsigned)out.size());
    return true;
}

}  // namespace webtools
