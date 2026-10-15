/**
 * @file test_main.cpp
 * @brief Host (PC) unit tests for the portable parts of the firmware
 *
 * Covers: UTF-8, Base64, JSON framing, time formatting, Thai line wrapping,
 * text features, long-term memory store (BM25 + graph + persistence),
 * episodic log, 24k->16k resampler, and the Gemini tool layer.
 *
 * Build & run:  host_tests\run_tests.bat
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <string>
#include <vector>

#include "audio/Resampler.h"
#include "memory/EpisodicLog.h"
#include "memory/MemoryStore.h"
#include "memory/TextFeatures.h"
#include "util/Base64.h"
#include "util/JsonFrame.h"
#include "util/TextWrap.h"
#include "util/TimeFmt.h"
#include "util/Utf8.h"

#include "test_framework.h"

static bool hasContent(const std::vector<ltm::Hit>& hits, const char* text, bool viaGraph) {
    for (const ltm::Hit& h : hits) {
        if (h.rec->text == text && h.viaGraph == viaGraph) return true;
    }
    return false;
}

// ============================================================
// Utilities
// ============================================================

TEST(utf8_decode_encode) {
    const char* s = "กa๙";
    size_t i = 0;
    const size_t len = strlen(s);
    CHECK_EQ(utf8::next(s, len, i), 0x0E01u);
    CHECK_EQ(utf8::next(s, len, i), (uint32_t)'a');
    CHECK_EQ(utf8::next(s, len, i), 0x0E59u);
    CHECK_EQ(i, len);

    const char bad[] = {(char)0xE0, (char)0xB8};  // truncated Thai character
    i = 0;
    CHECK_EQ(utf8::next(bad, 2, i), utf8::kReplacement);
    CHECK_EQ(i, (size_t)1);

    char buf[4];
    CHECK_EQ(utf8::encode(0x0E01, buf), (size_t)3);
    CHECK(memcmp(buf, "ก", 3) == 0);

    const char* thai = "สวัสดี";  // 6 code points x 3 bytes
    CHECK_EQ(utf8::clampToBoundary(thai, strlen(thai), 7), (size_t)6);
    CHECK_EQ(utf8::clampToBoundary(thai, strlen(thai), 100), strlen(thai));
}

TEST(base64_roundtrip) {
    char out[64];
    CHECK_EQ(b64::encode((const uint8_t*)"Man", 3, out), (size_t)4);
    CHECK(strcmp(out, "TWFu") == 0);
    b64::encode((const uint8_t*)"Ma", 2, out);
    CHECK(strcmp(out, "TWE=") == 0);
    b64::encode((const uint8_t*)"M", 1, out);
    CHECK(strcmp(out, "TQ==") == 0);

    srand(42);
    for (size_t len = 0; len < 200; len++) {
        std::vector<uint8_t> data(len);
        for (auto& b : data) b = (uint8_t)(rand() & 0xFF);
        std::vector<char> enc(b64::encodedLength(len) + 1);
        const size_t el = b64::encode(data.data(), len, enc.data());
        CHECK_EQ(el, b64::encodedLength(len));
        std::vector<uint8_t> dec(b64::decodedMaxLength(el));
        CHECK_EQ(b64::decode(enc.data(), el, dec.data()), len);
        CHECK(memcmp(dec.data(), data.data(), len) == 0);

        // in-place
        const size_t n = b64::decode(enc.data(), el, (uint8_t*)enc.data());
        CHECK_EQ(n, len);
        CHECK(memcmp(enc.data(), data.data(), len) == 0);
    }
    uint8_t tmp[8];
    CHECK_EQ(b64::decode("TW*u", 4, tmp), b64::kError);
}

TEST(json_frame_detection) {
    const char* a = "{\"a\":1}";
    CHECK(jsonframe::isCompleteObject(a, strlen(a)));
    const char* b = "{\"a\":{\"b\":[1,2]}";
    CHECK(!jsonframe::isCompleteObject(b, strlen(b)));
    const char* c = "{\"s\":\"}{\\\"}\"} \n";
    CHECK(jsonframe::isCompleteObject(c, strlen(c)));
    const char* d = "{\"a\":1}{";
    CHECK(!jsonframe::isCompleteObject(d, strlen(d)));
    CHECK(!jsonframe::isCompleteObject("", 0));
}

TEST(time_formatting) {
    // 1700000000 = 2023-11-14 22:13:20 UTC = 2023-11-15 05:13:20 in Bangkok (Wednesday)
    const timefmt::Civil c = timefmt::toCivil(1700000000u, 7 * 3600);
    CHECK_EQ(c.year, 2023);
    CHECK_EQ(c.month, 11);
    CHECK_EQ(c.day, 15);
    CHECK_EQ(c.hour, 5);
    CHECK_EQ(c.minute, 13);
    CHECK_EQ(c.weekday, 3);
    CHECK_EQ(timefmt::daysFromCivil(2023, 11, 15), timefmt::localDay(1700000000u, 7 * 3600));

    char buf[24];
    CHECK(timefmt::formatDateTime(1700000000u, 7 * 3600, buf, sizeof(buf)));
    CHECK(strcmp(buf, "2023-11-15 05:13") == 0);
    CHECK(!timefmt::formatDate(1000, 0, buf, sizeof(buf)));
}

TEST(thai_line_wrapping) {
    auto width = [](uint32_t cp) { return cp == ' ' ? 4 : 10; };

    const char* en = "hello world again";
    auto lines = textwrap::wrap(en, strlen(en), 60, width);
    CHECK_EQ(lines.size(), (size_t)3);
    CHECK(std::string(en + lines[0].start, lines[0].len) == "hello");
    CHECK(std::string(en + lines[1].start, lines[1].len) == "world");

    const char* th = "เมื่อวานผู้ใช้ไปเที่ยวทะเลกับครอบครัวและกินอาหารทะเลอร่อยมาก";
    lines = textwrap::wrap(th, strlen(th), 80, width);
    CHECK(lines.size() > 3);
    size_t total = 0;
    for (const auto& l : lines) {
        size_t i = l.start;
        const uint32_t first = utf8::next(th, strlen(th), i);
        CHECK(!utf8::isThaiCombining(first));
        CHECK(!utf8::isThaiTrailing(first));
        // the last code point of a line must not be a leading vowel
        size_t j = l.start, last = 0;
        while (j < l.start + l.len) last = utf8::next(th, strlen(th), j);
        CHECK(!utf8::isThaiLeadingVowel(last));
        total += l.len;
    }
    CHECK_EQ(total, strlen(th));  // nothing lost (no spaces in the text)

    const char* nl = "a\nb";
    lines = textwrap::wrap(nl, strlen(nl), 100, width);
    CHECK_EQ(lines.size(), (size_t)2);
}

TEST(text_features) {
    features::HashSet a, b;
    uint16_t count = 0;
    features::extract("สุนัข", strlen("สุนัข"), a, &count);
    CHECK_EQ(count, (uint16_t)4);  // สุ ุน นั ัข
    CHECK_EQ(a.size(), (size_t)4);

    // tone marks are ignored
    features::extract("ชื่อ", strlen("ชื่อ"), a);
    features::extract("ชือ", strlen("ชือ"), b);
    CHECK(a == b);

    // latin words are case-insensitive, punctuation separates
    features::extract("iPhone, Pro!", 12, a);
    features::extract("iphone pro", 10, b);
    CHECK(a == b);
    CHECK_EQ(a.size(), (size_t)2);

    CHECK(features::jaccard(a, b) > 0.99f);
    PsString norm;
    features::normalize("  โมจิ   Shiba!! ", 25, norm);
    CHECK(norm == "โมจิ shiba");
}

// ============================================================
// Long-term memory store
// ============================================================

static void seedFacts(ltm::MemoryStore& m) {
    m.addFact("ผู้ใช้ชื่อโจ้", ltm::Category::Profile, 5, 1760400000);
    m.addFact("ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาล", ltm::Category::Preference, 3, 1760400100);
    m.addFact("ผู้ใช้แพ้กุ้ง", ltm::Category::Profile, 5, 1760400200);
    m.addFact("ผู้ใช้ทำงานเป็นวิศวกรซอฟต์แวร์ที่กรุงเทพ", ltm::Category::Profile, 4, 1760400300);
    m.addFact("ผู้ใช้มีประชุมกับลูกค้าทุกวันจันทร์", ltm::Category::Task, 3, 1760400400);
    m.addFact("ผู้ใช้ชอบฟังเพลงแจ๊สตอนทำงาน", ltm::Category::Preference, 2, 1760400500);
    m.addFact("User's favourite programming language is Rust", ltm::Category::Preference, 3, 1760400600);
}

TEST(memory_search_ranking) {
    ltm::MemoryStore m;
    seedFacts(m);
    CHECK_EQ(m.size(), (size_t)7);

    std::vector<ltm::Hit> hits;
    m.search("กาแฟ", 3, hits, 0);
    CHECK(!hits.empty() && hits[0].rec->text == "ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาล");

    // conversational query with filler words
    m.search("จำได้ไหมว่าฉันแพ้อะไร", 3, hits, 0);
    CHECK(!hits.empty() && hits[0].rec->text == "ผู้ใช้แพ้กุ้ง");

    m.search("วันจันทร์มีนัดอะไร", 3, hits, 0);
    CHECK(!hits.empty() && hits[0].rec->text == "ผู้ใช้มีประชุมกับลูกค้าทุกวันจันทร์");

    m.search("programming language", 3, hits, 0);
    CHECK(!hits.empty() && hits[0].rec->text == "User's favourite programming language is Rust");

    // tone marks are ignored: "ชือ" (typo) still finds "ชื่อ"
    m.search("ชืออะไร", 3, hits, 0);
    CHECK(!hits.empty() && hits[0].rec->text == "ผู้ใช้ชื่อโจ้");

    // a query sharing no bigram with any memory returns nothing
    // (single coincidental bigrams may still return a weak hit: recall > precision,
    //  the LLM reads the content and ignores irrelevant results)
    m.search("ภูเขาไฟ", 3, hits, 0);
    CHECK(hits.empty());
    for (const auto& h : hits) printf("    unexpected hit: %s\n", h.rec->text.c_str());
    m.search("", 3, hits, 0);
    CHECK(hits.empty());
}

TEST(memory_merge_and_update) {
    ltm::MemoryStore m;
    seedFacts(m);
    bool merged = false;
    const uint32_t id = m.addFact("ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาลเลย", ltm::Category::Other, 4, 1760500000, &merged);
    CHECK(merged);
    CHECK_EQ(m.size(), (size_t)7);
    const ltm::Record* r = m.find(id);
    CHECK(r && r->text == "ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาลเลย");
    CHECK(r && r->importance == 4);
    CHECK(r && r->category == ltm::Category::Preference);  // "other" keeps the old category
    CHECK(r && r->updated == 1760500000u && r->created == 1760400100u);

    m.addFact("ผู้ใช้ชอบชาเขียว", ltm::Category::Preference, 3, 1760500100, &merged);
    CHECK(!merged);
    CHECK_EQ(m.size(), (size_t)8);

    CHECK_EQ(m.addFact("   ", ltm::Category::Other, 3, 0), 0u);
    CHECK_EQ(m.addFact("!!!", ltm::Category::Other, 3, 0), 0u);
}

TEST(memory_knowledge_graph) {
    ltm::MemoryStore m;
    seedFacts(m);
    m.addRelation("ผู้ใช้", "มีสุนัขชื่อ", "โมจิ", ltm::Category::Person, 4, 1760400700);
    m.addRelation("โมจิ", "เป็นพันธุ์", "ชิบะ", ltm::Category::Person, 3, 1760400800);
    m.addRelation("โมจิ", "ชอบกิน", "ขนมกระดูก", ltm::Category::Person, 3, 1760400900);
    m.addRelation("ผู้ใช้", "มีพี่สาวชื่อ", "แนน", ltm::Category::Person, 3, 1760401000);

    bool merged = false;
    m.addRelation("ผู้ใช้", "มีสุนัขชื่อ", "โมจิ", ltm::Category::Other, 5, 1760401100, &merged);
    CHECK(merged);

    std::vector<ltm::Hit> hits;
    m.search("สุนัขของผู้ใช้", 2, hits, 6);
    CHECK(hasContent(hits, "ผู้ใช้ มีสุนัขชื่อ โมจิ", false));
    // reached through the entity "โมจิ"
    CHECK(hasContent(hits, "โมจิ เป็นพันธุ์ ชิบะ", true));
    CHECK(hasContent(hits, "โมจิ ชอบกิน ขนมกระดูก", true));
    // the sister is linked only through the hub "ผู้ใช้" -> must not be pulled in
    bool sister = false;
    for (const auto& h : hits) sister |= h.rec->text == "ผู้ใช้ มีพี่สาวชื่อ แนน";
    CHECK(!sister);

    // explicit entity mention in the query expands it
    m.search("แนน", 5, hits, 6);
    CHECK(hasContent(hits, "ผู้ใช้ มีพี่สาวชื่อ แนน", false));
}

TEST(memory_persistence_and_compaction) {
    const std::string dir = tempDir("store");
    const std::string path = dir + "/ltm.jsonl";
    uint32_t coffeeId = 0, shrimpId = 0;
    {
        ltm::MemoryStore m;
        CHECK(m.open(path.c_str(), DirectFileSink::instance()));
        seedFacts(m);
        m.addRelation("ผู้ใช้", "มีสุนัขชื่อ", "โมจิ", ltm::Category::Person, 4, 1760400700);
        std::vector<ltm::Hit> hits;
        m.search("กาแฟ", 1, hits, 0);
        coffeeId = hits.empty() ? 0 : hits[0].rec->id;
        m.search("แพ้", 1, hits, 0);
        shrimpId = hits.empty() ? 0 : hits[0].rec->id;
        CHECK(m.remove(shrimpId));
        CHECK(!m.remove(shrimpId));
    }
    {
        ltm::MemoryStore m;
        CHECK(m.open(path.c_str(), DirectFileSink::instance()));
        CHECK_EQ(m.size(), (size_t)7);  // 7 facts + 1 relation - 1 deleted
        CHECK(m.find(coffeeId) != nullptr);
        CHECK(m.find(shrimpId) == nullptr);
        const ltm::Stats s = m.stats();
        CHECK_EQ(s.relations, (size_t)1);
        const uint32_t newId = m.addFact("ผู้ใช้ปลูกต้นมะนาว", ltm::Category::Other, 3, 1760600000);
        CHECK(newId > coffeeId && newId > shrimpId);  // ids never reused

        // many updates of the same fact grow the log
        for (int k = 0; k < 60; k++) {
            m.addFact("ผู้ใช้ชอบกาแฟดำไม่ใส่น้ำตาล", ltm::Category::Preference, 3, 1760600000 + k);
        }
        CHECK(countLines(path) > 60);
    }
    {
        ltm::MemoryStore m;
        CHECK(m.open(path.c_str(), DirectFileSink::instance()));  // compacts on open
        CHECK_EQ(m.size(), (size_t)8);
        CHECK_EQ(countLines(path), (size_t)8);
        std::vector<ltm::Hit> hits;
        m.search("มะนาว", 1, hits, 0);
        CHECK(!hits.empty() && hits[0].rec->text == "ผู้ใช้ปลูกต้นมะนาว");
    }
    {
        // interrupted compaction: only the .tmp file survived
        fs::rename(path, path + ".tmp");
        ltm::MemoryStore m;
        CHECK(m.open(path.c_str(), DirectFileSink::instance()));
        CHECK_EQ(m.size(), (size_t)8);
        CHECK(fs::exists(path));
    }
}

TEST(memory_capacity_and_core) {
    ltm::MemoryStore::Options opt;
    opt.maxRecords = 5;
    ltm::MemoryStore m(opt);
    m.addFact("ผู้ใช้ชื่อโจ้", ltm::Category::Profile, 5, 100);
    m.addFact("ผู้ใช้ชอบแมว", ltm::Category::Preference, 1, 101);
    m.addFact("ผู้ใช้ชอบเล่นเกม", ltm::Category::Preference, 2, 102);
    m.addFact("ผู้ใช้อยู่เชียงใหม่", ltm::Category::Profile, 4, 103);
    m.addFact("ผู้ใช้ตื่นนอนตีห้า", ltm::Category::Other, 3, 104);
    m.addFact("ผู้ใช้ขับรถฮอนด้า", ltm::Category::Other, 3, 105);  // evicts importance 1
    CHECK_EQ(m.size(), (size_t)5);
    std::vector<ltm::Hit> hits;
    m.search("แมว", 3, hits, 0);
    CHECK(hits.empty());

    PsString core;
    const size_t n = m.buildCoreMemory(core, 1000);
    CHECK_EQ(n, (size_t)2);  // importance >= 4 or profile
    CHECK(core.find("ผู้ใช้ชื่อโจ้") != PsString::npos);
    CHECK(core.find("ผู้ใช้อยู่เชียงใหม่") != PsString::npos);
    CHECK(core.find("เกม") == PsString::npos);

    m.buildCoreMemory(core, 45);  // budget fits only the most important line (42 bytes)
    CHECK(core.find("ผู้ใช้ชื่อโจ้") != PsString::npos);
    CHECK(core.find("เชียงใหม่") == PsString::npos);
}

// ============================================================
// Episodic log
// ============================================================

TEST(episodic_log) {
    const std::string dir = tempDir("episodes");
    const uint32_t day1 = 1760432400;  // 2025-10-14 16:00 Bangkok
    const uint32_t day2 = day1 + 86400;
    {
        ltm::EpisodicLog log;
        CHECK(log.open(dir.c_str(), DirectFileSink::instance(), day2));
        log.append('u', "วันนี้ไปตลาดนัดมา ซื้อมะม่วงมาเยอะเลย", day1);
        log.append('a', "มะม่วงหน้านี้หวานมากเลยครับ", day1 + 5);
        log.append('u', "พรุ่งนี้ต้องไปหาหมอฟัน", day2);
        log.append('a', "ขอให้ไม่เจ็บนะครับ", day2 + 3);
        log.append('u', "   ", day2 + 4);  // ignored
        CHECK_EQ(log.workingSize(), (size_t)4);

        std::vector<ltm::Turn> turns;
        log.search("มะม่วง", 7, 5, day2, turns);
        CHECK(turns.size() >= 2);
        CHECK(!turns.empty() && turns[0].text.find("มะม่วง") != PsString::npos);

        log.search("หมอฟัน", 7, 5, day2, turns);
        CHECK(!turns.empty() && turns[0].ts == day2);

        log.search("", 7, 2, day2, turns);  // recent
        CHECK_EQ(turns.size(), (size_t)2);
        CHECK(turns.size() == 2 && turns[1].role == 'a');

        PsString ctx;
        log.buildRecentContext(ctx, 3, 4096, "ผู้ใช้", "คุณ");
        CHECK(ctx.find("ผู้ใช้: พรุ่งนี้ต้องไปหาหมอฟัน") != PsString::npos);
        CHECK(ctx.find("ตลาดนัด") == PsString::npos);  // only the last 3 turns
        CHECK(ctx.find("มะม่วงหน้านี้") < ctx.find("หมอฟัน"));  // chronological
    }
    CHECK(fs::exists(dir + "/2025-10-14.jsonl"));
    CHECK(fs::exists(dir + "/2025-10-15.jsonl"));
    {
        ltm::EpisodicLog log;
        CHECK(log.open(dir.c_str(), DirectFileSink::instance(), day2));
        CHECK_EQ(log.workingSize(), (size_t)4);  // yesterday + today preloaded
        CHECK_EQ(log.prune(30, day2 + 40 * 86400), (size_t)2);
        CHECK(!fs::exists(dir + "/2025-10-14.jsonl"));
    }
}

// ============================================================
// Resampler
// ============================================================

static double rmsOf(const std::vector<int16_t>& v, size_t skip) {
    double acc = 0;
    size_t n = 0;
    for (size_t i = skip; i < v.size(); i++, n++) acc += (double)v[i] * v[i];
    return n ? sqrt(acc / n) : 0;
}

static std::vector<int16_t> runResampler(double freq, double amplitude, size_t samples) {
    Downsampler24to16 rs;
    std::vector<int16_t> in(samples);
    for (size_t i = 0; i < samples; i++) {
        in[i] = (int16_t)lrint(amplitude * sin(2.0 * 3.14159265358979 * freq * i / 24000.0));
    }
    std::vector<int16_t> out(Downsampler24to16::maxOutput(samples));
    // feed in uneven chunks to exercise state carry-over
    size_t produced = 0, pos = 0, chunk = 37;
    while (pos < samples) {
        const size_t n = std::min(chunk, samples - pos);
        produced += rs.process(in.data() + pos, n, out.data() + produced);
        pos += n;
        chunk = chunk == 37 ? 480 : 37;
    }
    out.resize(produced);
    return out;
}

TEST(resampler_24k_to_16k) {
    auto dc = runResampler(0.0, 0.0, 300);
    CHECK_EQ(dc.size(), (size_t)200);

    Downsampler24to16 rs;
    std::vector<int16_t> in(600, 10000), out(Downsampler24to16::maxOutput(600));
    const size_t n = rs.process(in.data(), in.size(), out.data());
    CHECK_EQ(n, (size_t)400);
    CHECK(abs(out[n - 1] - 10000) <= 2);  // unity DC gain after warm-up

    auto tone = runResampler(1000.0, 10000.0, 24000);
    CHECK_EQ(tone.size(), (size_t)16000);
    const double r1 = rmsOf(tone, 200);
    CHECK(fabs(r1 - 10000.0 / sqrt(2.0)) < 0.03 * 7071.0);

    auto high = runResampler(10000.0, 10000.0, 24000);  // above the new Nyquist
    CHECK(rmsOf(high, 200) < 0.02 * 7071.0);
}

// ============================================================

int main() {
    for (const TestCase& t : registry()) {
        const int before = g_failures;
        t.fn();
        printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
