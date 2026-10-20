/**
 * @file test_phone.cpp
 * @brief Host tests: phone link messages (GPS, Google Maps steps, pairing) and when the assistant says a turn
 */

#include <math.h>
#include <string.h>

#include <string>

#include "phone/PhoneProtocol.h"
#include "test_framework.h"

using namespace phone;

#define CHECK_STR(a, b)                                                               \
    do {                                                                              \
        g_checks++;                                                                   \
        const std::string _sa = (a), _sb = (b);                                       \
        if (_sa != _sb) {                                                             \
            g_failures++;                                                             \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _sa.c_str(), _sb.c_str()); \
        }                                                                             \
    } while (0)

TEST(phone_parse_gps_nav_place_result) {
    Msg m;
    CHECK(parse("{\"t\":\"gps\",\"lat\":13.7563,\"lon\":100.5018,\"spd\":42.5,\"hdg\":270,\"acc\":4}", m, 1000));
    CHECK(m.kind == Kind::Gps);
    // ArduinoJson 7.4 keeps a short decimal as float: ~1e-6 degrees = 10 cm, plenty for a car
    CHECK(fabs(m.gps.lat - 13.7563) < 1e-5 && fabs(m.gps.lon - 100.5018) < 1e-5);
    CHECK(fabsf(m.gps.speedKmh - 42.5f) < 0.01f && fabsf(m.gps.heading - 270) < 0.01f);
    CHECK(m.gps.fresh(1000 + kGpsStaleMs) && !m.gps.fresh(1001 + kGpsStaleMs));
    CHECK(parse("{\"t\":\"gps\",\"lat\":13,\"lon\":100}", m, 5));  // whole numbers are numbers too
    CHECK(m.kind == Kind::Gps && m.gps.heading == -1 && m.gps.speedKmh == 0);
    CHECK(!parse("{\"t\":\"gps\",\"lat\":95,\"lon\":100}", m, 5));  // off the planet
    CHECK(!parse("{\"t\":\"gps\",\"lat\":\"x\",\"lon\":100}", m, 5));
    CHECK(parse("{\"t\":\"gps\",\"lat\":1,\"lon\":2,\"spd\":-3}", m, 5) && m.gps.speedKmh == 0);

    CHECK(parse("{\"t\":\"nav\",\"icon\":\"right\",\"text\":\"เลี้ยวขวาเข้า ถ.พระราม 4\",\"dist_m\":350,"
                "\"eta\":\"16:40\",\"left_km\":5.4,\"left_min\":12}", m, 2000));
    CHECK(m.kind == Kind::Nav && m.nav.active && m.nav.icon == Icon::Right);
    CHECK_STR(m.nav.text, "เลี้ยวขวาเข้า ถ.พระราม 4");
    CHECK_EQ(m.nav.distM, 350);
    CHECK_STR(m.nav.eta, "16:40");
    CHECK(fabsf(m.nav.leftKm - 5.4f) < 0.01f && m.nav.leftMin == 12);
    CHECK(parse("{\"t\":\"nav\",\"icon\":\"warp\",\"text\":\"x\"}", m, 1) && m.nav.icon == Icon::None && m.nav.distM == -1);
    CHECK(parse("{\"t\":\"nav\",\"end\":true}", m, 1) && m.kind == Kind::NavEnd);

    CHECK(parse("{\"t\":\"place\",\"name\":\"ถ.พระราม 4 ปทุมวัน\"}", m, 1) && m.kind == Kind::Place);
    CHECK(!parse("{\"t\":\"place\",\"name\":\"\"}", m, 1));
    CHECK(parse("{\"t\":\"result\",\"id\":7,\"ok\":false,\"text\":\"เปิด Maps ไม่ได้\"}", m, 1));
    CHECK(m.kind == Kind::Result && m.id == 7 && !m.ok);
    CHECK(parse("{\"t\":\"ping\"}", m, 1) && m.kind == Kind::Ping);
    CHECK(!parse("{\"t\":\"teleport\"}", m, 1));
    CHECK(!parse("not json", m, 1));
    CHECK(!parse("[1,2]", m, 1));
    CHECK(!parse(nullptr, m, 1));
}

TEST(phone_text_is_one_line_and_whole_characters) {
    Msg m;
    std::string longThai = "{\"t\":\"nav\",\"text\":\"";
    for (int i = 0; i < 80; i++) longThai += "ก";  // 240 bytes of Thai into a 192-byte field
    longThai += "\\nreboot\"}";
    CHECK(parse(longThai.c_str(), m, 1));
    const size_t n = strlen(m.nav.text);
    CHECK(n < sizeof(m.nav.text) && n % 3 == 0);  // cut between characters, never inside one
    CHECK(!strchr(m.nav.text, '\n'));
    CHECK(parse("{\"t\":\"place\",\"name\":\"a\\nset key x\"}", m, 1));
    CHECK_STR(m.text, "a set key x");
}

TEST(phone_sha256_hmac_vectors) {
    char hex[65];
    uint8_t d[32];
    sha256::Hash h;
    h.finish(d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256::Hash a;
    a.update("abc", 3);
    a.finish(d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256::Hash two;  // a message over two blocks, fed in odd pieces
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    two.update(m, 5);
    two.update(m + 5, strlen(m) - 5);
    two.finish(d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // RFC 4231 test cases 1 and 2
    uint8_t key[20];
    memset(key, 0x0b, sizeof(key));
    sha256::hmac(key, sizeof(key), "Hi There", 8, d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    sha256::hmac("Jefe", 4, "what do ya want for nothing?", 28, d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    uint8_t longKey[131];  // RFC 4231 case 6: a key longer than the block is hashed first
    memset(longKey, 0xaa, sizeof(longKey));
    const char* t6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    sha256::hmac(longKey, sizeof(longKey), t6, strlen(t6), d);
    sha256::toHex(d, hex);
    CHECK_STR(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(phone_wifi_handshake_proofs) {
    const char* token = "0123456789abcdef0123";
    const char* nonce = "00112233445566778899aabbccddeeff";
    char p[65], b[65];
    proof(token, "phone", nonce, p);
    proof(token, "board", nonce, b);
    CHECK(strcmp(p, b) != 0);  // a proof cannot be bounced back as the other side's
    CHECK(proofOk(token, "phone", nonce, p));
    CHECK(!proofOk(token, "board", nonce, p));
    CHECK(!proofOk("0123456789abcdef0124", "phone", nonce, p));  // another phone
    CHECK(!proofOk(token, "phone", "00112233445566778899aabbccddeefe", p));  // an old answer to another nonce
    CHECK(!proofOk("", "phone", nonce, p));    // not paired: nothing is accepted
    CHECK(!proofOk(token, "phone", "short", p));
    CHECK(!proofOk(token, "phone", nonce, "abc"));
    // = Python hmac.new(token, b"phone|" + nonce, sha256); the phone app checks the same value (PhoneLinkTest)
    CHECK_STR(p, "23a20f99625dd8c9080f6817134f977d7a6999cf45fade74777dacbd7e9e4e5c");
    Msg m;
    CHECK(parse("{\"t\":\"hello\",\"app\":\"1.2\",\"nonce\":\"0123456789abcdef\",\"proof\":\"ab\"}", m, 1));
    CHECK(m.kind == Kind::Hello && validNonce(m.nonce));
    CHECK_STR(m.proof, "ab");
    CHECK(!validNonce("0123456789ABCDEF"));  // lowercase only
    CHECK(!validNonce("0123456789abcdeg"));
}

TEST(phone_pairing_tokens) {
    Msg m;
    CHECK(parse("{\"t\":\"pair\",\"token\":\"0123456789abcdef0123\"}", m, 1) && m.kind == Kind::Pair);
    CHECK(validToken(m.token));
    CHECK(parse("{\"t\":\"hello\",\"app\":\"1.2\",\"token\":\"0123456789abcdef0123\"}", m, 1) && m.kind == Kind::Hello);
    CHECK_STR(m.app, "1.2");
    CHECK(!validToken("short"));
    CHECK(!validToken("0123456789abcdef 0123"));  // spaces would split a console line
    CHECK(!validToken("0123456789abcdef\"0123"));
    CHECK(!validToken(nullptr));
    CHECK(sameToken("0123456789abcdef", "0123456789abcdef"));
    CHECK(!sameToken("0123456789abcdef", "0123456789abcdeF"));
    CHECK(!sameToken("0123456789abcdef", "0123456789abcde"));
    CHECK(!sameToken("", ""));  // not paired is never "the same"
}

TEST(phone_distances_in_thai) {
    char b[32];
    formatDistance(347, b, sizeof(b));
    CHECK_STR(b, "350 เมตร");
    formatDistance(83, b, sizeof(b));
    CHECK_STR(b, "80 เมตร");
    formatDistance(990, b, sizeof(b));
    CHECK_STR(b, "1 กิโลเมตร");
    formatDistance(1240, b, sizeof(b), true);
    CHECK_STR(b, "1.2 กม.");
    formatDistance(23400, b, sizeof(b), true);
    CHECK_STR(b, "23 กม.");
    formatDistance(-1, b, sizeof(b));
    CHECK_STR(b, "-");
}

static Nav step(const char* icon, const char* text, int dist, uint32_t now) {
    Nav n;
    n.active = true;
    n.icon = parseIcon(icon);
    copyText(n.text, sizeof(n.text), text);
    n.distM = dist;
    n.at = now;
    return n;
}

TEST(phone_turn_said_once_ahead_of_the_turn) {
    TurnAnnouncer a;
    char out[200];
    CHECK_EQ(TurnAnnouncer::leadMetres(0), 200);
    CHECK_EQ(TurnAnnouncer::leadMetres(60), 333);   // 20 s at 60 km/h
    CHECK_EQ(TurnAnnouncer::leadMetres(200), 800);  // capped
    uint32_t t = 10000;
    CHECK(!a.update(step("right", "เลี้ยวขวาเข้า ถ.พระราม 4", 900, t), 60, t, out, sizeof(out)));  // too far yet
    t += 5000;
    CHECK(a.update(step("right", "เลี้ยวขวาเข้า ถ.พระราม 4", 320, t), 60, t, out, sizeof(out)));
    CHECK_STR(out, "อีก 300 เมตร เลี้ยวขวาเข้า ถ.พระราม 4");
    t += 3000;
    CHECK(!a.update(step("right", "เลี้ยวขวาเข้า ถ.พระราม 4", 150, t), 60, t, out, sizeof(out)));  // once per step
    // the next step comes right after: waits for the 8 s gap, then says it
    t += 2000;
    CHECK(!a.update(step("left", "เลี้ยวซ้ายเข้า ซ.สุขุมวิท 24", 180, t), 30, t, out, sizeof(out)));
    t += 7000;
    CHECK(a.update(step("left", "เลี้ยวซ้ายเข้า ซ.สุขุมวิท 24", 120, t), 30, t, out, sizeof(out)));
    CHECK_STR(out, "อีก 100 เมตร เลี้ยวซ้ายเข้า ซ.สุขุมวิท 24");
}

TEST(phone_turn_skipped_when_late_stale_or_ended) {
    TurnAnnouncer a;
    char out[200];
    uint32_t t = 50000;
    CHECK(!a.update(step("right", "เลี้ยวขวา", 20, t), 40, t, out, sizeof(out)));  // already at the corner
    CHECK(!a.update(step("right", "เลี้ยวขวา", 10, t + 1000), 40, t + 1000, out, sizeof(out)));
    Nav old = step("left", "เลี้ยวซ้าย", 150, t);
    CHECK(!a.update(old, 40, t + kNavStaleMs + 1, out, sizeof(out)));  // Maps stopped updating
    CHECK(!a.update(step("left", "เลี้ยวซ้าย", -1, t), 40, t, out, sizeof(out)));  // distance unknown
    Nav none;  // navigation ended: forget the step, a new route says its first turn again
    CHECK(!a.update(none, 0, t, out, sizeof(out)));
    CHECK(a.update(step("uturn", "", 150, t + 20000), 20, t + 20000, out, sizeof(out)));
    CHECK_STR(out, "อีก 150 เมตร กลับรถ");  // no text: the icon's words
}

TEST(phone_arrival) {
    TurnAnnouncer a;
    char out[200];
    CHECK(!a.update(step("arrive", "ถึงที่หมาย เซ็นทรัลเวิลด์ ทางซ้าย", 400, 1000), 50, 1000, out, sizeof(out)));
    CHECK(a.update(step("arrive", "ถึงที่หมาย เซ็นทรัลเวิลด์ ทางซ้าย", 140, 2000), 30, 2000, out, sizeof(out)));
    CHECK_STR(out, "อีก 150 เมตร ถึงที่หมาย เซ็นทรัลเวิลด์ ทางซ้าย");
    TurnAnnouncer b;
    CHECK(b.update(step("arrive", "", 20, 1000), 5, 1000, out, sizeof(out)));
    CHECK_STR(out, "ถึงจุดหมายแล้ว");
}

TEST(phone_icons_round_trip) {
    for (uint8_t i = 0; i < (uint8_t)Icon::Count; i++) CHECK(parseIcon(iconKey((Icon)i)) == (Icon)i);
    CHECK(parseIcon(nullptr) == Icon::None);
    CHECK(strlen(TurnAnnouncer::iconTh(Icon::Roundabout)) > 0);
}
