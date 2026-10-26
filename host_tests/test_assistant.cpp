/**
 * @file test_assistant.cpp
 * @brief Host tests: personas, scheduler, to-do list and the full tool layer
 */

#include <ArduinoJson.h>

#include <set>

#include "agent/AgentTools.h"
#include "agent/Personas.h"
#include "agent/Scheduler.h"
#include "agent/TaskList.h"
#include "test_framework.h"
#include "util/TimeFmt.h"

// 2025-10-14 16:00 Bangkok (Tuesday)
static const uint32_t kNow = 1760432400;
static const int32_t kOff = 7 * 3600;

// ============================================================
// Personas
// ============================================================

TEST(personas_catalog) {
    CHECK_EQ(persona::kVoiceCount, (size_t)30);
    CHECK(persona::findVoice("aoede") != nullptr);
    CHECK(persona::findVoice("Sulafat")->gender == persona::kFemale);
    CHECK(persona::findVoice("Nobody") == nullptr);
    size_t female = 0;
    for (size_t i = 0; i < persona::kVoiceCount; i++) female += persona::kVoices[i].gender == persona::kFemale;
    CHECK_EQ(female, (size_t)14);

    CHECK(persona::findPersonality("tutor") >= 0);
    CHECK(persona::findPersonality("สายฮา") >= 0);
    CHECK_EQ(persona::findPersonality("evil"), -1);

    const persona::Profile* p = persona::findProfile("มะปราง");
    CHECK(p && strcmp(p->id, "maprang") == 0);
    CHECK(persona::findProfile("LINA") != nullptr);
    CHECK(persona::findProfile("น้องมะปรางหน่อย") != nullptr);  // name inside a phrase
    for (size_t i = 0; i < persona::kProfileCount; i++) {
        CHECK(persona::findVoice(persona::kProfiles[i].voice) != nullptr);
        CHECK(persona::findPersonality(persona::kProfiles[i].personality) >= 0);
        CHECK(persona::kProfiles[i].eyeColor < persona::kEyeColorCount);
    }
    CHECK_EQ(persona::parseGender("ผู้หญิง"), persona::kFemale);
    CHECK_EQ(persona::parseGender("male"), persona::kMale);
    CHECK_EQ(persona::parseGender("?"), persona::kNeutral);
    CHECK(persona::findEyeColor("ชมพู") >= 0);
}

// ============================================================
// Scheduler
// ============================================================

TEST(scheduler_parse_when) {
    char buf[24];
    uint32_t t = sched::parseWhen("2025-10-14 18:30", kNow, kOff);
    timefmt::formatDateTime(t, kOff, buf, sizeof(buf));
    CHECK(strcmp(buf, "2025-10-14 18:30") == 0);

    t = sched::parseWhen("07:00", kNow, kOff);  // already passed today -> tomorrow
    timefmt::formatDateTime(t, kOff, buf, sizeof(buf));
    CHECK(strcmp(buf, "2025-10-15 07:00") == 0);

    t = sched::parseWhen("20:15", kNow, kOff);  // later today
    timefmt::formatDateTime(t, kOff, buf, sizeof(buf));
    CHECK(strcmp(buf, "2025-10-14 20:15") == 0);

    t = sched::parseWhen("พรุ่งนี้ 20:15", kNow, kOff);
    timefmt::formatDateTime(t, kOff, buf, sizeof(buf));
    CHECK(strcmp(buf, "2025-10-15 20:15") == 0);

    CHECK_EQ(sched::parseWhen("2025-10-14 09:00", kNow, kOff), 0u);  // past
    CHECK_EQ(sched::parseWhen("25:00", kNow, kOff), 0u);
    CHECK_EQ(sched::parseWhen("soon", kNow, kOff), 0u);
    CHECK_EQ(sched::parseWhen("08:00", 0, kOff), 0u);  // clock not synced

    // Friday 2025-10-17 07:00 -> next weekday is Monday 2025-10-20
    const uint32_t fri = sched::parseWhen("2025-10-17 07:00", kNow, kOff);
    timefmt::formatDateTime(sched::nextOccurrence(fri, sched::Repeat::Weekdays, kOff), kOff, buf, sizeof(buf));
    CHECK(strcmp(buf, "2025-10-20 07:00") == 0);
    CHECK_EQ(sched::nextOccurrence(fri, sched::Repeat::Weekly, kOff), fri + 7 * 86400u);
}

TEST(scheduler_fire_and_persist) {
    const std::string dir = tempDir("sched");
    const std::string path = dir + "/reminders.json";
    uint32_t daily = 0;
    {
        sched::Scheduler s(kOff);
        CHECK(s.open(path.c_str()));
        CHECK(s.addTimer(kNow, 300, "ต้มไข่") > 0);
        daily = s.addReminder(sched::parseWhen("17:00", kNow, kOff), sched::Repeat::Daily, "กินยา");
        CHECK(daily > 0);
        CHECK(s.addReminder(sched::parseWhen("2025-10-20 09:00", kNow, kOff), sched::Repeat::None, "ประชุม") > 0);
        CHECK_EQ(s.items().size(), (size_t)3);
        CHECK(s.items().front().timer);  // sorted: timer (16:05) first
        CHECK_EQ(s.addTimer(kNow, 0, "x"), 0u);
    }
    {
        sched::Scheduler s(kOff);
        CHECK(s.open(path.c_str()));
        CHECK_EQ(s.items().size(), (size_t)3);
        std::vector<sched::Reminder> fired;
        CHECK_EQ(s.collectDue(kNow + 100, fired), (size_t)0);
        CHECK_EQ(s.collectDue(kNow + 3700, fired), (size_t)2);  // 16:05 timer + 17:00 daily
        CHECK_EQ(s.items().size(), (size_t)2);                  // daily stays, rescheduled
        const sched::Reminder* d = s.find(daily);
        char buf[24];
        timefmt::formatDateTime(d ? d->due : 0, kOff, buf, sizeof(buf));
        CHECK(strcmp(buf, "2025-10-15 17:00") == 0);
        CHECK(s.cancel(daily));
        CHECK(!s.cancel(daily));
        CHECK_EQ(s.cancelAll(), (size_t)1);
    }
    {
        sched::Scheduler s(kOff);
        s.open(path.c_str());
        CHECK(s.items().empty());
    }
}

// ============================================================
// To-do list
// ============================================================

TEST(task_list) {
    const std::string dir = tempDir("tasks");
    const std::string path = dir + "/tasks.json";
    uint32_t milk = 0, tax = 0;
    {
        todo::TaskList t;
        CHECK(t.open(path.c_str()));
        milk = t.add("ซื้อนม", 2, 0, kNow);
        tax = t.add("ยื่นภาษี", 1, kNow + 86400, kNow + 1);
        CHECK(t.add("   ", 2, 0, kNow) == 0);
        CHECK_EQ(t.openCount(), (size_t)2);
        auto l = t.list(false);
        CHECK(l.size() == 2 && l[0]->id == tax);  // high priority first
        CHECK(t.setDone(milk, true, kNow + 10));
        CHECK_EQ(t.openCount(), (size_t)1);
        CHECK(t.rename(tax, "ยื่นภาษีออนไลน์"));
    }
    {
        todo::TaskList t;
        t.open(path.c_str());
        CHECK_EQ(t.size(), (size_t)2);
        CHECK(t.find(tax) && t.find(tax)->title == "ยื่นภาษีออนไลน์");
        CHECK(t.find(milk) && t.find(milk)->done);
        CHECK_EQ(t.list(false).size(), (size_t)1);
        CHECK_EQ(t.purgeDone(kNow + 40 * 86400, 30), (size_t)1);
        CHECK(t.remove(tax));
        CHECK_EQ(t.size(), (size_t)0);
    }
}

// ============================================================
// Tool layer
// ============================================================

class MockDevice : public DeviceOps {
public:
    uint32_t now = kNow;
    int volumeLevel = 70, brightnessLevel = 80;
    std::string led, expression, voice = "Aoede", agentName = "ไข่มุก", userName;
    bool sleepRequested = false;
    int applied = 0;

    uint32_t nowEpoch() override { return now; }
    bool setLedColor(const char* color) override { return led = color, true; }
    int setVolume(int level) override { return volumeLevel = level; }
    int setBrightness(int level) override { return brightnessLevel = level; }
    int volume() override { return volumeLevel; }
    int brightness() override { return brightnessLevel; }
    int mic = 5, sleepMin = 3, clockMin = 5, dim = 8;
    bool powerSaveNow = false, micMuteRequested = false;
    int micGain() override { return mic; }
    void configureDevice(const DeviceChange& c, JsonObject r) override {
        if (c.micGain >= 0) mic = c.micGain;
        if (c.sleepMinutes >= 0) sleepMin = c.sleepMinutes;
        if (c.clockMinutes >= 0) clockMin = c.clockMinutes;
        if (c.dimBrightness >= 0) dim = c.dimBrightness;
        powerSaveNow = c.powerSaveNow;
        r["mic_sensitivity"] = mic;
    }
    void requestMicMute() override { micMuteRequested = true; }
    bool radioOn = true;
    bool stopRadio() override { const bool was = radioOn; radioOn = false; return was; }
    std::string radioAsked;
    const char* playRadio(const char* station, JsonObject r) override {
        radioAsked = station;
        if (!strcmp(station, "ไม่มี")) return "ไม่มีสถานีนี้ในรายการ";
        radioOn = true;
        r["station"] = "COOL Fahrenheit 93";
        return nullptr;
    }
    void fillStatus(JsonObject out) override { out["battery_percent"] = 80; }
    bool vehicleOn = false;
    std::string vehicleCall;
    bool vehicleEnabled() override { return vehicleOn; }
    bool phoneOn = false;
    std::string navCall;
    bool phonePaired() override { return phoneOn; }
    const char* navigateAction(const char* action, const char* to, JsonObject r) override {
        navCall = std::string(action) + ":" + to;
        if (!strcmp(action, "go") && !strcmp(to, "offline")) return "the phone is not connected";
        r["navigating"] = !strcmp(action, "go");
        return nullptr;
    }
    const char* vehicleAction(const char* action, const char* source, JsonObject r) override {
        vehicleCall = std::string(action) + ":" + source;
        if (!strcmp(action, "set_source") && !strcmp(source, "nope")) return "unknown source";
        r["status"] = "LIVE";
        r["readings"]["rpm"] = 2500;
        return nullptr;
    }
    bool setExpression(const char* e) override { return expression = e, true; }
    void requestSleep() override { sleepRequested = true; }
    bool applyAssistantChange(const AssistantChange& c, JsonObject result) override {
        applied++;
        if (c.profile) {
            const persona::Profile* p = persona::findProfile(c.profile);
            voice = p->voice;
            agentName = p->agentName;
        }
        if (c.voice) voice = c.voice;
        if (c.agentName) agentName = c.agentName;
        if (c.userName) userName = c.userName;
        result["voice"] = voice.c_str();
        result["agent_name"] = agentName.c_str();
        return true;
    }
    void describeAssistant(JsonObject out) override {
        out["name"] = agentName.c_str();
        out["voice"] = voice.c_str();
    }
};

static bool callTool(AgentTools& tools, const char* name, const char* argsJson, JsonDocument& resp) {
    JsonDocument args;
    deserializeJson(args, argsJson);
    resp.clear();
    return tools.execute(name, args.as<JsonObjectConst>(), resp.to<JsonObject>());
}

struct ToolRig {
    ltm::MemoryStore mem;
    ltm::EpisodicLog log;
    sched::Scheduler scheduler{kOff};
    todo::TaskList tasks;
    MockDevice dev;
    AgentTools tools{mem, log, scheduler, tasks, dev, kOff};
    explicit ToolRig(const std::string& dir) {
        mem.open((dir + "/ltm.jsonl").c_str(), DirectFileSink::instance());
        log.open(dir.c_str(), DirectFileSink::instance(), kNow);
        scheduler.open((dir + "/reminders.json").c_str());
        tasks.open((dir + "/tasks.json").c_str());
    }
};

TEST(tool_declarations_per_model) {
    ToolRig rig(tempDir("decl"));
    auto behaviors = [&](ToolBehavior b, bool search, std::string& expr, std::string& recall, size_t& count,
                         bool& hasSearch) {
        JsonDocument doc;
        rig.tools.addDeclarations(doc["tools"].to<JsonArray>(), b, search);
        JsonArrayConst tools = doc["tools"];
        hasSearch = !tools[0]["googleSearch"].isNull();
        JsonArrayConst decls = tools[hasSearch ? 1 : 0]["functionDeclarations"];
        count = decls.size();
        for (JsonObjectConst d : decls) {
            const char* beh = d["behavior"] | "(none)";
            if (!strcmp(d["name"] | "", "set_expression")) expr = beh;
            if (!strcmp(d["name"] | "", "recall_memory")) recall = beh;
        }
        return measureJson(doc);
    };
    std::string expr, recall;
    size_t count;
    bool search;
    const size_t bytes = behaviors(ToolBehavior::Blocking, true, expr, recall, count, search);
    CHECK_EQ(count, (size_t)44);  // about_me + 25 core (incl. configure_device, rest_screen, mute_microphone, play_radio, stop_radio) + 5 live data + 6 utilities + water + 6 finance
    {
        JsonDocument doc;
        rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
        std::string names;
        for (JsonObjectConst d : doc[0]["functionDeclarations"].as<JsonArrayConst>()) names += std::string(d["name"] | "") + ",";
        for (const char* t : {"get_weather,", "get_oil_price,", "get_market_quote,", "convert_currency,", "get_news,",
                              "calculate,", "convert_units,", "get_world_time,", "get_location,", "search_knowledge,",
                              "notes,", "market_overview,", "market_analysis,", "market_scan,", "economic_calendar,",
                              "position_size,", "price_alert,"}) {
            CHECK(names.find(t) != std::string::npos);
        }
    }
    CHECK(search);
    CHECK(recall == "BLOCKING" && expr == "NON_BLOCKING");
    // billed every turn: keep the declarations compact (+200 for about_me, T096; +470 for water, T100 -
    // switched off with "set water 0" like the finance pack)
    CHECK(bytes < 15700);
    printf("    tool declarations: %u bytes JSON\n", (unsigned)bytes);

    // the vehicle dash: the board always declares it, so "เปิดหน้าปัด" works before a source is set (T079)
    rig.dev.vehicleOn = true;
    const size_t withVehicle = behaviors(ToolBehavior::Blocking, true, expr, recall, count, search);
    CHECK_EQ(count, (size_t)45);
    CHECK(withVehicle - bytes < 400);  // one small tool
    CHECK(withVehicle < 16100);        // what the board really sends every session
    printf("    + vehicle dash tool: %u bytes JSON\n", (unsigned)(withVehicle - bytes));
    {
        // default build (VEHICLE_BLE = 0): the model is not offered the Bluetooth source it cannot pick (T078)
        JsonDocument doc;
        rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
        std::string decl;
        serializeJson(doc, decl);
        CHECK(decl.find("\"obd_uart\"") != std::string::npos);
        CHECK(decl.find("\"obd_ble\"") == std::string::npos);
    }
    JsonDocument vr;
    CHECK(callTool(rig.tools, "vehicle", "{\"action\":\"status\"}", vr));
    CHECK(rig.dev.vehicleCall == "status:" && vr["result"]["readings"]["rpm"] == 2500);
    CHECK(callTool(rig.tools, "vehicle", "{\"action\":\"set_source\",\"source\":\"kline\"}", vr));
    CHECK(rig.dev.vehicleCall == "set_source:kline");
    CHECK(callTool(rig.tools, "vehicle", "{\"action\":\"open\"}", vr));  // "เปิดหน้าปัด"
    CHECK(rig.dev.vehicleCall == "open:");
    CHECK(!callTool(rig.tools, "vehicle", "{\"action\":\"set_source\",\"source\":\"nope\"}", vr));
    CHECK(!callTool(rig.tools, "vehicle", "{\"action\":\"drive\"}", vr));
    rig.dev.vehicleOn = false;

    // the phone app (wiki 18): `navigate` only once a phone was paired, small like the dash tool
    rig.dev.phoneOn = true;
    const size_t withPhone = behaviors(ToolBehavior::Blocking, true, expr, recall, count, search);
    CHECK_EQ(count, (size_t)45);
    CHECK(withPhone - bytes < 450);
    printf("    + navigate tool: %u bytes JSON\n", (unsigned)(withPhone - bytes));
    JsonDocument nr;
    CHECK(callTool(rig.tools, "navigate", "{\"action\":\"go\",\"to\":\"เซ็นทรัลเวิลด์\"}", nr));
    CHECK(rig.dev.navCall == "go:เซ็นทรัลเวิลด์" && nr["result"]["navigating"] == true);
    CHECK(!callTool(rig.tools, "navigate", "{\"action\":\"go\"}", nr));  // where to?
    CHECK(!callTool(rig.tools, "navigate", "{\"action\":\"go\",\"to\":\"offline\"}", nr));
    CHECK(callTool(rig.tools, "navigate", "{\"action\":\"status\"}", nr) && rig.dev.navCall == "status:");
    CHECK(!callTool(rig.tools, "navigate", "{\"action\":\"fly\"}", nr));
    rig.dev.phoneOn = false;

    // optional finance pack off -> 38 (no trading tools exist at all)
    rig.tools.setFinancePack(false);
    behaviors(ToolBehavior::Blocking, false, expr, recall, count, search);
    CHECK_EQ(count, (size_t)38);
    rig.tools.setFinancePack(true);
    behaviors(ToolBehavior::Blocking, false, expr, recall, count, search);
    CHECK_EQ(count, (size_t)44);
    {
        JsonDocument doc;
        rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
        for (JsonObjectConst d : doc[0]["functionDeclarations"].as<JsonArrayConst>()) {
            CHECK(std::string(d["name"] | "").rfind("mt5", 0) != 0);
        }
    }

    behaviors(ToolBehavior::NonBlocking, false, expr, recall, count, search);
    CHECK(recall == "NON_BLOCKING" && expr == "NON_BLOCKING" && !search);
    behaviors(ToolBehavior::Unspecified, true, expr, recall, count, search);
    CHECK(recall == "(none)" && expr == "(none)");
}

// T071: a NON_BLOCKING call answered without "scheduling" (or with it inside "response") defaults to
// WHEN_IDLE - gemini-3.8-live then spoke its whole answer twice
// about_me is built from the declarations at call time: every declared tool is in it, nothing kept by hand
TEST(about_me_lists_what_is_declared) {
    ToolRig rig(tempDir("aboutme"));
    auto declaredNames = [&]() {
        JsonDocument doc;
        rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
        std::set<std::string> names;
        for (JsonObjectConst d : doc[0]["functionDeclarations"].as<JsonArrayConst>()) names.insert(d["name"] | "");
        names.erase("about_me");
        return names;
    };
    auto listedNames = [](JsonDocument& resp, std::set<std::string>& groups) {
        std::set<std::string> names;
        groups.clear();
        for (JsonObjectConst g : resp["result"]["abilities"].as<JsonArrayConst>()) {
            groups.insert(g["group"] | "");
            CHECK(g["tools"].size() > 0);  // empty sections are dropped
            for (JsonObjectConst t : g["tools"].as<JsonArrayConst>()) {
                names.insert(t["name"] | "");
                CHECK(strlen(t["does"] | "") > 5);
            }
        }
        return names;
    };

    JsonDocument r;
    std::set<std::string> groups;
    CHECK(callTool(rig.tools, "about_me", "{}", r));
    CHECK(r["result"]["assistant"]["name"] == "ไข่มุก");
    CHECK(!r["result"]["how_to_present"].isNull());
    std::set<std::string> listed = listedNames(r, groups);
    CHECK(listed == declaredNames());
    CHECK_EQ(r["result"]["tool_count"].as<size_t>(), listed.size());
    CHECK(listed.count("about_me") == 0);
    CHECK(groups.count("finance (TradingView, not investment advice)") == 1);
    CHECK(groups.count("connected hardware, car and phone") == 0);  // nothing connected in the rig
    CHECK(r["result"]["connected"]["google_search"] == false);
    const size_t bytes = measureJson(r);
    CHECK(bytes < 8000);  // one function response, not billed every turn
    printf("    about_me: %u tools, %u bytes JSON\n", (unsigned)listed.size(), (unsigned)bytes);

    // what changes at run time changes the answer: finance pack off, a paired phone, Search offered
    rig.tools.setFinancePack(false);
    rig.dev.phoneOn = true;
    rig.tools.setSearchOffered(true);
    CHECK(callTool(rig.tools, "about_me", "{}", r));
    listed = listedNames(r, groups);
    CHECK(listed == declaredNames());
    CHECK(listed.count("market_analysis") == 0 && listed.count("navigate") == 1);
    CHECK(groups.count("connected hardware, car and phone") == 1);
    CHECK(r["result"]["connected"]["phone_app_paired"] == true && r["result"]["connected"]["google_search"] == true);
    rig.tools.setFinancePack(true);
    rig.dev.phoneOn = false;
    rig.tools.setSearchOffered(false);

    // the collector is off again: a later setup is not affected
    JsonDocument doc;
    rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
    CHECK(doc[0]["functionDeclarations"].size() == declaredNames().size() + 1);
}

TEST(tool_response_scheduling) {
    auto sched = [](const char* name, ToolBehavior b) {
        const char* s = AgentTools::responseScheduling(name, b);
        return std::string(s ? s : "(none)");
    };
    CHECK(sched("set_expression", ToolBehavior::Blocking) == "SILENT");
    CHECK(sched("get_weather", ToolBehavior::Blocking) == "(none)");     // BLOCKING: the server ignores it
    CHECK(sched("recall_memory", ToolBehavior::Blocking) == "(none)");
    CHECK(sched("set_expression", ToolBehavior::NonBlocking) == "(none)");  // extended thinking: not supported
    CHECK(sched("set_expression", ToolBehavior::Unspecified) == "(none)");  // 3.1: no async calls
    CHECK(sched(nullptr, ToolBehavior::Blocking) == "(none)");

    // 3.8 Live: every tool declared NON_BLOCKING gets a scheduling, and only those
    ToolRig rig(tempDir("sched"));
    JsonDocument doc;
    rig.tools.addDeclarations(doc.to<JsonArray>(), ToolBehavior::Blocking, false);
    size_t nonBlocking = 0;
    for (JsonObjectConst d : doc[0]["functionDeclarations"].as<JsonArrayConst>()) {
        const bool async = !strcmp(d["behavior"] | "", "NON_BLOCKING");
        nonBlocking += async;
        CHECK(async == (AgentTools::responseScheduling(d["name"] | "", ToolBehavior::Blocking) != nullptr));
    }
    CHECK_EQ(nonBlocking, (size_t)1);
}

TEST(tools_memory_roundtrip) {
    ToolRig rig(tempDir("toolmem"));
    JsonDocument r;
    CHECK(callTool(rig.tools, "save_memory",
                   "{\"content\":\"ผู้ใช้ชอบกินส้มตำไม่ใส่ปู\",\"category\":\"preference\",\"importance\":4}", r));
    const uint32_t id = r["result"]["memory_id"] | 0u;
    CHECK(id > 0);
    CHECK(callTool(rig.tools, "recall_memory", "{\"query\":\"ส้มตำ\"}", r));
    CHECK_EQ(r["result"]["count"] | 0, 1);
    CHECK(strcmp(r["result"]["memories"][0]["saved"] | "", "2025-10-14") == 0);
    CHECK(callTool(rig.tools, "save_relation", "{\"subject\":\"ผู้ใช้\",\"relation\":\"มีแมวชื่อ\",\"object\":\"ส้มจี๊ด\"}", r));
    const std::string forget = "{\"memory_id\":" + std::to_string(id) + "}";
    CHECK(callTool(rig.tools, "forget_memory", forget.c_str(), r));
    CHECK(strcmp(r["result"]["deleted_content"] | "", "ผู้ใช้ชอบกินส้มตำไม่ใส่ปู") == 0);
    CHECK(!callTool(rig.tools, "forget_memory", "{\"memory_id\":9999}", r));

    rig.log.append('u', "ช่วยเตือนเรื่องจ่ายค่าไฟด้วย", kNow);
    CHECK(callTool(rig.tools, "recall_conversation", "{\"query\":\"ค่าไฟ\"}", r));
    CHECK_EQ(r["result"]["count"] | 0, 1);
}

TEST(tools_time_and_tasks) {
    ToolRig rig(tempDir("tooltime"));
    JsonDocument r;
    CHECK(callTool(rig.tools, "set_timer", "{\"minutes\":5,\"label\":\"ต้มมาม่า\"}", r));
    CHECK_EQ(r["result"]["rings_in_seconds"] | 0, 300);
    CHECK(strcmp(r["result"]["rings_at"] | "", "2025-10-14 16:05") == 0);
    CHECK(!callTool(rig.tools, "set_timer", "{}", r));

    CHECK(callTool(rig.tools, "set_reminder", "{\"when\":\"07:30\",\"label\":\"ตื่นนอน\",\"repeat\":\"weekdays\"}", r));
    CHECK(strcmp(r["result"]["due"] | "", "2025-10-15 07:30") == 0);
    CHECK(!callTool(rig.tools, "set_reminder", "{\"when\":\"yesterday\",\"label\":\"x\"}", r));

    CHECK(callTool(rig.tools, "list_reminders", "{}", r));
    CHECK_EQ(r["result"]["count"] | 0, 2);
    CHECK(strcmp(r["result"]["items"][0]["type"] | "", "timer") == 0);
    CHECK_EQ(r["result"]["items"][0]["in_minutes"] | 0, 5);
    const uint32_t rid = r["result"]["items"][1]["id"] | 0u;
    CHECK(callTool(rig.tools, "cancel_reminder", ("{\"id\":" + std::to_string(rid) + "}").c_str(), r));
    CHECK(callTool(rig.tools, "cancel_reminder", "{\"all\":true}", r));
    CHECK_EQ(r["result"]["cancelled"] | 0, 1);

    rig.dev.now = 0;  // clock not synced
    CHECK(!callTool(rig.tools, "set_timer", "{\"minutes\":1}", r));
    CHECK(strcmp(r["error"] | "", "clock not synced yet") == 0);
    rig.dev.now = kNow;

    CHECK(callTool(rig.tools, "add_task", "{\"title\":\"ซื้อของขวัญวันเกิดแม่\",\"priority\":1,\"due\":\"2025-10-20 18:00\"}", r));
    const uint32_t tid = r["result"]["id"] | 0u;
    CHECK(callTool(rig.tools, "add_task", "{\"title\":\"ล้างรถ\"}", r));
    CHECK_EQ(r["result"]["open_tasks"] | 0, 2);
    CHECK(callTool(rig.tools, "list_tasks", "{}", r));
    CHECK(strcmp(r["result"]["tasks"][0]["title"] | "", "ซื้อของขวัญวันเกิดแม่") == 0);
    CHECK(strcmp(r["result"]["tasks"][0]["due"] | "", "2025-10-20 18:00") == 0);
    CHECK(callTool(rig.tools, "update_task", ("{\"id\":" + std::to_string(tid) + ",\"action\":\"done\"}").c_str(), r));
    CHECK_EQ(r["result"]["open_tasks"] | 0, 1);
    CHECK(!callTool(rig.tools, "update_task", "{\"id\":999,\"action\":\"done\"}", r));
    CHECK(!callTool(rig.tools, "add_task", "{\"title\":\"x\",\"due\":\"someday\"}", r));
}

TEST(tools_assistant_and_device) {
    ToolRig rig(tempDir("toolasst"));
    JsonDocument r;
    CHECK(callTool(rig.tools, "configure_assistant", "{\"profile\":\"มะปราง\"}", r));
    CHECK(rig.dev.voice == "Leda");
    CHECK(strcmp(r["result"]["agent_name"] | "", "มะปราง") == 0);
    CHECK(callTool(rig.tools, "configure_assistant", "{\"voice\":\"puck\",\"user_name\":\"โจ\"}", r));
    CHECK(rig.dev.voice == "Puck" && rig.dev.userName == "โจ");
    CHECK(!callTool(rig.tools, "configure_assistant", "{\"voice\":\"HAL9000\"}", r));
    CHECK(!callTool(rig.tools, "configure_assistant", "{}", r));
    CHECK(!callTool(rig.tools, "configure_assistant", "{\"personality\":\"evil\"}", r));
    CHECK_EQ(rig.dev.applied, 2);

    CHECK(callTool(rig.tools, "list_assistant_options", "{\"kind\":\"voices\"}", r));
    CHECK_EQ(r["result"]["options"].size(), (size_t)30);
    CHECK(callTool(rig.tools, "list_assistant_options", "{\"kind\":\"profiles\"}", r));
    CHECK_EQ(r["result"]["options"].size(), persona::kProfileCount);
    CHECK(!callTool(rig.tools, "list_assistant_options", "{\"kind\":\"cars\"}", r));

    CHECK(callTool(rig.tools, "set_expression", "{\"emotion\":\"love\"}", r));
    CHECK(rig.dev.expression == "love");
    CHECK(!callTool(rig.tools, "set_expression", "{\"emotion\":\"hungry\"}", r));

    CHECK(callTool(rig.tools, "set_brightness", "{\"level\":1}", r));
    CHECK_EQ(rig.dev.brightnessLevel, 5);
    CHECK(callTool(rig.tools, "set_volume", "{\"level\":150}", r));
    CHECK_EQ(rig.dev.volumeLevel, 100);
    // relative control by voice: "ลดเสียงลงหน่อย" / "เพิ่มความสว่าง"
    CHECK(callTool(rig.tools, "set_volume", "{\"change\":-30}", r));
    CHECK_EQ(rig.dev.volumeLevel, 70);
    CHECK_EQ(r["result"]["previous"] | 0, 100);
    CHECK(callTool(rig.tools, "set_volume", "{\"change\":-90}", r));
    CHECK_EQ(rig.dev.volumeLevel, 0);
    CHECK(callTool(rig.tools, "set_brightness", "{\"change\":20}", r));
    CHECK_EQ(rig.dev.brightnessLevel, 25);
    CHECK(!callTool(rig.tools, "set_brightness", "{}", r));
    CHECK(callTool(rig.tools, "configure_device", "{\"mic_change\":1,\"clock_minutes\":10,\"dim_brightness\":200}", r));
    CHECK_EQ(rig.dev.mic, 6);
    CHECK_EQ(rig.dev.clockMin, 10);
    CHECK_EQ(rig.dev.dim, 60);
    CHECK(callTool(rig.tools, "rest_screen", "{}", r));  // "พักหน้าจอ"
    CHECK(rig.dev.powerSaveNow);
    CHECK(!callTool(rig.tools, "configure_device", "{}", r));
    CHECK(callTool(rig.tools, "mute_microphone", "{}", r));
    CHECK(rig.dev.micMuteRequested);
    CHECK(callTool(rig.tools, "play_radio", "{\"station\":\"คูล 93\"}", r));  // local tool: station list on the device
    CHECK(rig.dev.radioAsked == "คูล 93");
    CHECK(strcmp(r["result"]["station"] | "", "COOL Fahrenheit 93") == 0);
    CHECK(!callTool(rig.tools, "play_radio", "{\"station\":\"ไม่มี\"}", r));
    CHECK(callTool(rig.tools, "play_radio", "{}", r));  // empty = last station
    CHECK(rig.dev.radioAsked.empty());
    CHECK(callTool(rig.tools, "stop_radio", "{}", r));
    CHECK(r["result"]["was_playing"] | false);
    CHECK(!rig.dev.radioOn);
    CHECK(callTool(rig.tools, "control_led", "{\"color\":\"purple\"}", r));
    CHECK(!callTool(rig.tools, "control_led", "{\"color\":\"pink\"}", r));
    CHECK(callTool(rig.tools, "go_to_sleep", "{}", r));
    CHECK(rig.dev.sleepRequested);

    CHECK(callTool(rig.tools, "get_device_status", "{}", r));
    CHECK(strcmp(r["result"]["weekday"] | "", "อังคาร") == 0);
    CHECK(strcmp(r["result"]["assistant"]["voice"] | "", "Puck") == 0);
    CHECK_EQ(r["result"]["battery_percent"] | 0, 80);

    CHECK(!callTool(rig.tools, "self_destruct", "{}", r));
    CHECK(strcmp(r["error"] | "", "unknown function") == 0);
}

// ============================================================
// Model fallback
// ============================================================

#include "ai/ModelFallback.h"

TEST(model_fallback) {
    using fallback::Failure;
    CHECK(fallback::classifyClose(1011, "Internal error") == Failure::Unavailable);
    CHECK(fallback::classifyClose(1008, "Quota exceeded for metric generate_content_requests") == Failure::Quota);
    CHECK(fallback::classifyClose(1008, "API key not valid. Please pass a valid API key.") == Failure::Auth);
    CHECK(fallback::classifyClose(1008, "models/foo is not found for API version v1beta") == Failure::NotFound);
    CHECK(fallback::classifyClose(1006, "") == Failure::Transient);
    CHECK(fallback::classifyClose(1000, "") == Failure::None);
    CHECK(fallback::classifyHttp(429, "{}") == Failure::Quota);
    CHECK(fallback::classifyHttp(503, "overloaded") == Failure::Unavailable);
    CHECK(fallback::classifyHttp(403, "") == Failure::Auth);
    CHECK(fallback::classifyHttp(404, "") == Failure::NotFound);

    fallback::ModelSelector s(3);
    s.setPreferred(0);
    const uint32_t t = 1000000;
    CHECK_EQ(s.active(t), (uint8_t)0);
    CHECK(!s.reportFailure(0, Failure::Auth, t));  // key problem: no switch
    CHECK(!s.reportFailure(0, Failure::Transient, t));
    CHECK(!s.reportFailure(0, Failure::Transient, t));
    CHECK(s.reportFailure(0, Failure::Transient, t));  // third in a row -> 2 min cool-down
    CHECK_EQ(s.active(t), (uint8_t)1);
    CHECK(s.usingFallback(t));
    CHECK_EQ(s.active(t + 121), (uint8_t)0);  // back to the preferred model

    const uint32_t u = t + 200;  // the transient cool-down is over
    CHECK(s.reportFailure(0, Failure::Quota, u));  // 10 min
    CHECK(s.reportFailure(1, Failure::NotFound, u));
    CHECK_EQ(s.active(u), (uint8_t)2);
    CHECK_EQ(s.active(u + 601), (uint8_t)0);
    s.reportFailure(0, Failure::Quota, u + 601);  // second strike: 20 min
    CHECK_EQ(s.cooldownLeft(0, u + 601), 1200u);
    s.reportFailure(2, Failure::Unavailable, u + 601);  // all three cooling down
    CHECK_EQ(s.active(u + 602), (uint8_t)2);              // recovers first (5 min)
    s.reportSuccess(0);
    CHECK_EQ(s.active(u + 602), (uint8_t)0);

    s.setPreferred(2);
    CHECK_EQ(s.active(u + 602 + 301), (uint8_t)2);
}

// Seen on the device (2026-10-14): a key without quota closes every live model with
// 1011 "You exceeded your current quota". The selector must report "all down" so the
// agent pauses instead of cycling, and a later transient error must not shorten the pause.
TEST(model_fallback_key_wide_quota) {
    using fallback::Failure;
    const char* reason = "You exceeded your current quota, please check your plan and billing details. For more";
    CHECK(fallback::classifyClose(1011, reason) == Failure::Quota);

    fallback::ModelSelector s(3);
    const uint32_t t = 5000;
    CHECK(!s.allCoolingDown(t));
    CHECK_EQ(s.nextRecoveryIn(t), 0u);
    CHECK(s.reportFailure(0, Failure::Quota, t));
    CHECK(!s.allCoolingDown(t));
    CHECK(s.reportFailure(1, Failure::Quota, t + 2));
    s.reportFailure(2, Failure::Quota, t + 4);
    CHECK(s.allCoolingDown(t + 4));
    CHECK(s.lastFailure() == Failure::Quota);
    CHECK_EQ(s.nextRecoveryIn(t + 4), 596u);  // model 0 recovers first
    CHECK_EQ(s.active(t + 4), (uint8_t)0);

    // the disconnect that follows a close frame used to count as "transient" and replace the
    // 10-min quota cool-down with a 2-min one
    s.reportFailure(0, Failure::Transient, t + 5);
    s.reportFailure(0, Failure::Transient, t + 5);
    s.reportFailure(0, Failure::Transient, t + 5);
    CHECK_EQ(s.cooldownLeft(0, t + 5), 595u);

    CHECK(!s.allCoolingDown(t + 601));  // pause over: try again
    CHECK_EQ(s.active(t + 601), (uint8_t)0);
    s.reportFailure(0, Failure::Quota, t + 601);  // second strike: 20 min
    CHECK_EQ(s.cooldownLeft(0, t + 601), 1200u);

    // 1008 after waking from sleep with an old resumption handle: not the model's fault (it used
    // to be classified NotFound and put two models on a 24-h cool-down)
    CHECK(fallback::classifyClose(1008, "BidiGenerateContent session not found or expired") ==
          Failure::SessionExpired);
    CHECK(fallback::classifyClose(1008, "models/foo is not found for API version v1beta") == Failure::NotFound);
    // bare 1008 = server hiccup, not a missing model (used to cost the model a 24-h cool-down)
    CHECK(fallback::classifyClose(1008, "The operation was aborted.") == Failure::Transient);
    const uint32_t before = s.cooldownLeft(1, t + 601);
    CHECK(!s.reportFailure(1, Failure::SessionExpired, t + 601));
    CHECK_EQ(s.cooldownLeft(1, t + 601), before);  // no penalty

    s.reset();  // new API key
    CHECK(!s.allCoolingDown(t + 602));
    CHECK_EQ(s.active(t + 602), (uint8_t)0);
    CHECK(s.lastFailure() == Failure::None);
    s.reportFailure(0, Failure::Quota, t + 603);
    CHECK_EQ(s.cooldownLeft(0, t + 603), 600u);  // strikes were cleared too
}

// ============================================================
// Consolidation helpers
// ============================================================

#include "agent/Consolidation.h"

TEST(consolidation_prompt_and_parse) {
    std::vector<ltm::Turn> turns;
    auto add = [&](char r, const char* t) {
        ltm::Turn x;
        x.role = r;
        x.text = t;
        turns.push_back(x);
    };
    add('u', "สวัสดี");
    add('a', "สวัสดีค่ะ");
    PsString prompt;
    CHECK(!consolidate::buildUserPrompt("", turns, 2, 4000, prompt));  // only one user turn
    add('u', "ฉันเพิ่งย้ายไปอยู่เชียงใหม่ ทำงานที่ร้านกาแฟ");
    add('a', "ยินดีด้วยค่ะ");
    CHECK(consolidate::buildUserPrompt("- ผู้ใช้ชื่อโจ\n", turns, 2, 4000, prompt));
    CHECK(prompt.find("Known memories:\n- ผู้ใช้ชื่อโจ") != PsString::npos);
    CHECK(prompt.find("User: ฉันเพิ่งย้ายไปอยู่เชียงใหม่") != PsString::npos);
    CHECK(consolidate::buildUserPrompt("", turns, 2, 120, prompt));  // byte budget keeps the newest turns
    CHECK(prompt.find("สวัสดีค่ะ") == PsString::npos);

    JsonDocument schema;
    consolidate::buildSchema(schema.to<JsonObject>());
    CHECK(strcmp(schema["properties"]["facts"]["items"]["properties"]["category"]["enum"][0] | "", "profile") == 0);

    const char* answer =
        "{\"facts\":[{\"content\":\"ผู้ใช้อาศัยอยู่ที่เชียงใหม่\",\"category\":\"profile\",\"importance\":4},"
        "{\"content\":\"x\",\"category\":\"other\",\"importance\":1},"
        "{\"content\":\"ผู้ใช้ทำงานที่ร้านกาแฟ\",\"category\":\"bogus\",\"importance\":9}],"
        "\"relations\":[{\"subject\":\"ผู้ใช้\",\"relation\":\"ทำงานที่\",\"object\":\"ร้านกาแฟ\"},{\"subject\":\"\"}]}";
    std::vector<consolidate::Fact> facts;
    std::vector<consolidate::Relation> rels;
    CHECK(consolidate::parseResult(answer, strlen(answer), facts, rels));
    CHECK_EQ(facts.size(), (size_t)2);  // "x" too short
    CHECK(facts.size() == 2 && facts[1].category == ltm::Category::Other &&
          facts[1].importance == consolidate::kMaxImportance);  // 9 -> capped (not a core memory)
    CHECK(facts.size() == 2 && facts[0].importance == consolidate::kMaxImportance);  // 4 -> 3 too
    CHECK(strstr(consolidate::systemPrompt(), "radio/TV") != nullptr);
    CHECK_EQ(rels.size(), (size_t)1);
    CHECK(!consolidate::parseResult("not json", 8, facts, rels));

    consolidate::RateLimiter rl(300, 2, 7 * 3600);
    const uint32_t t = 1760432400;
    CHECK(rl.allowed(t));
    rl.record(t);
    CHECK(!rl.allowed(t + 100));
    CHECK(rl.allowed(t + 301));
    rl.record(t + 301);
    CHECK(!rl.allowed(t + 1000));  // daily cap of 2
    CHECK(rl.allowed(t + 86400));  // next day
    rl.backoff(t + 86400, 600);
    CHECK(!rl.allowed(t + 86400 + 10));
}

TEST(memory_clear_all) {
    const std::string dir = tempDir("clear");
    ltm::MemoryStore m;
    m.open((dir + "/ltm.jsonl").c_str(), DirectFileSink::instance());
    const uint32_t a = m.addFact("ผู้ใช้ชอบแมว", ltm::Category::Preference, 3, kNow);
    CHECK(m.clearAll());
    CHECK_EQ(m.size(), (size_t)0);
    CHECK(m.addFact("ผู้ใช้ชอบหมา", ltm::Category::Preference, 3, kNow) > a);  // ids never reused
    ltm::MemoryStore m2;
    m2.open((dir + "/ltm.jsonl").c_str(), DirectFileSink::instance());
    CHECK_EQ(m2.size(), (size_t)1);

    ltm::EpisodicLog log;
    log.open(dir.c_str(), DirectFileSink::instance(), kNow);
    log.append('u', "ทดสอบ", kNow);
    log.append('u', "ทดสอบสอง", kNow + 86400);
    CHECK_EQ(log.clearAll(), (size_t)2);
    CHECK_EQ(log.workingSize(), (size_t)0);
    CHECK(fs::exists(dir + "/ltm.jsonl"));  // not an episode file
}

// ============================================================
// Local tools: calculate / convert_units
// ============================================================

#include "util/Calc.h"
#include "util/Units.h"

static bool near(double a, double b, double eps = 1e-6) { return fabs(a - b) <= eps * (1 + fabs(b)); }

TEST(calc_expressions) {
    double v;
    const char* err = nullptr;
    CHECK(calc::evaluate("1 + 2 * 3", v, err) && near(v, 7));
    CHECK(calc::evaluate("(1 + 2) * 3", v, err) && near(v, 9));
    CHECK(calc::evaluate("2 ^ 3 ^ 2", v, err) && near(v, 512));  // right associative
    CHECK(calc::evaluate("-3 + 5", v, err) && near(v, 2));
    CHECK(calc::evaluate("1,234.5 * 2", v, err) && near(v, 2469));
    CHECK(calc::evaluate("200 * 15%", v, err) && near(v, 30));
    CHECK(calc::evaluate("17 % 5", v, err) && near(v, 2));
    CHECK(calc::evaluate("sqrt(16) + abs(-2)", v, err) && near(v, 6));
    CHECK(calc::evaluate("sin(30)", v, err) && near(v, 0.5));
    CHECK(calc::evaluate("2 x 3", v, err) && near(v, 6));
    CHECK(calc::evaluate("1.5e3 / 3", v, err) && near(v, 500));
    CHECK(calc::evaluate("pi * 2", v, err) && near(v, 6.283185307));
    CHECK(!calc::evaluate("1 / 0", v, err) && err);
    CHECK(!calc::evaluate("2 +", v, err));
    CHECK(!calc::evaluate("foo(3)", v, err));
    CHECK(!calc::evaluate("((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))", v, err));
}

TEST(unit_conversion) {
    double v;
    const units::Unit *a, *b;
    const char* err = nullptr;
    CHECK(units::convert(1, "ไร่", "ตารางวา", v, a, b, err) && near(v, 400));
    CHECK(units::convert(1, "ไร่", "งาน", v, a, b, err) && near(v, 4));
    CHECK(units::convert(2, "km", "เมตร", v, a, b, err) && near(v, 2000));
    CHECK(units::convert(100, "กม./ชม.", "m/s", v, a, b, err) && near(v, 27.7777778));
    CHECK(units::convert(1, "บาททอง", "g", v, a, b, err) && near(v, 15.244));
    CHECK(units::convert(37, "C", "F", v, a, b, err) && near(v, 98.6));
    CHECK(units::convert(212, "fahrenheit", "celsius", v, a, b, err) && near(v, 100));
    CHECK(units::convert(1, "Kg", "lb", v, a, b, err) && near(v, 2.20462262));
    CHECK(!units::convert(1, "kg", "km", v, a, b, err) && err);  // different kinds
    CHECK(!units::convert(1, "parsec", "km", v, a, b, err));
}

#include "util/SetupFile.h"

TEST(sd_setup_file) {
    const std::string path = tempDir("setup") + "/ai-setup.txt";
    {
        FILE* f = fopen(path.c_str(), "wb");
        fputs("\xEF\xBB\xBF# written with Windows Notepad\r\n", f);
        fputs("  wifi_ssid = \"Home Net 2.4G\"  \r\n", f);
        fputs("WIFI_PASSWORD=p@ss=word#1\r\n", f);
        fputs("GEMINI_API_KEY=your-gemini-api-key-here\r\n", f);  // template value -> ignored
        fputs("; a comment=ignored\n", f);
        fputs("MT5_TOKEN='tok 123'\n", f);  // key from older templates: MT5 support was removed -> ignored
        fputs("UNKNOWN=x\n", f);
        fclose(f);
    }
    setupfile::Values v;
    CHECK(setupfile::parse(path.c_str(), v));
    CHECK(std::string(v.ssid) == "Home Net 2.4G");
    CHECK(std::string(v.pass) == "p@ss=word#1");  // only the first '=' splits; '#' inside a value is kept
    CHECK(v.key[0] == '\0');
    {
        char line[] = "gemini_api_key = 'AIza key 123'";  // single quotes are stripped
        setupfile::Values q;
        CHECK(setupfile::parseLine(line, q) && std::string(q.key) == "AIza key 123");
        char legacy[] = "MT5_URL=http://192.168.1.10:8000";
        CHECK(!setupfile::parseLine(legacy, q));
    }

    setupfile::wipe(path.c_str());
    FILE* gone = fopen(path.c_str(), "rb");
    CHECK(gone == nullptr);
    if (gone) fclose(gone);

    // a file holding only template values imports nothing
    {
        FILE* f = fopen(path.c_str(), "wb");
        fputs("WIFI_SSID=<your Wi-Fi name>\nWIFI_PASSWORD=YourWiFiPassword\nGEMINI_API_KEY=your-gemini-api-key-here\n", f);
        fclose(f);
    }
    setupfile::Values t;
    CHECK(!setupfile::parse(path.c_str(), t));
    CHECK(!setupfile::parse((path + ".missing").c_str(), t));

    // real values that merely look like templates are kept
    {
        FILE* f = fopen(path.c_str(), "wb");
        fputs("WIFI_PASSWORD=a<b>c\nWIFI_SSID=yourhome\n", f);
        fclose(f);
    }
    setupfile::Values r;
    CHECK(setupfile::parse(path.c_str(), r));
    CHECK(std::string(r.pass) == "a<b>c");
    CHECK(std::string(r.ssid) == "yourhome");
}

#include <cmath>
#include <vector>

#include "util/RadioResampler.h"

static double toneRms(uint32_t inRate, int channels, double hz, size_t* outCount) {
    RadioResampler rs;
    rs.reset(inRate);
    std::vector<int16_t> in(inRate * channels), out(rs.maxOutput(inRate));
    for (uint32_t i = 0; i < inRate; i++) {
        const int16_t v = (int16_t)(10000 * std::sin(2 * 3.141592653589793 * hz * i / inRate));
        for (int c = 0; c < channels; c++) in[i * channels + c] = v;
    }
    const size_t n = rs.process(in.data(), inRate, channels, out.data(), out.size());
    *outCount = n;
    double e = 0;
    for (size_t i = n / 4; i < n; i++) e += (double)out[i] * out[i];  // skip the filter warm-up
    return std::sqrt(e / (n - n / 4));
}

TEST(radio_resampler) {
    size_t n = 0;
    const double pass = toneRms(44100, 2, 1000, &n);  // 1 kHz stereo 44.1 kHz -> passes
    CHECK(n >= 23990 && n <= 24010);                    // one second at 24 kHz
    CHECK(pass > 6500 && pass < 7500);                  // ~10000/sqrt(2)
    const double stop = toneRms(48000, 2, 18000, &n);  // 18 kHz would alias: removed
    CHECK(stop < pass * 0.15);
    const double up = toneRms(22050, 1, 1000, &n);      // mono 22.05 kHz is upsampled
    CHECK(n >= 23990 && n <= 24010);
    CHECK(up > 6500 && up < 7500);
}

#include "agent/SearchGate.h"

TEST(search_gate) {
    using search::Gate;
    const uint32_t E = 1800000000;  // a synced clock
    uint32_t ep = 99, pause = 0;
    uint8_t n = 0;

    // Reboot left a session open: quota with Search, quota without Search too -> not Search
    Gate g;
    CHECK(g.available(10, E));
    CHECK(g.onLiveQuota(10));            // first time: log it
    CHECK(!g.onLiveQuota(12));           // still suspect: no second log
    CHECK(!g.available(12, E));          // the retry goes without Search
    CHECK(g.onQuotaWithoutSearch(14));   // ... and fails the same way
    CHECK(g.available(15, E) && !g.confirmed(15, E));
    CHECK(!g.onQuotaWithoutSearch(16));  // not suspect any more: nothing to say
    CHECK(!g.onReadyWithoutSearch(17, E, ep, n, pause));  // a later plain session confirms nothing

    // No Search allowance: quota with Search, fine without -> confirmed 6 h, saved
    CHECK(g.onLiveQuota(30));
    CHECK(g.onReadyWithoutSearch(32, E, ep, n, pause));
    CHECK(pause == Gate::kConfirmedSec && ep == E + Gate::kConfirmedSec && n == 1);
    CHECK(!g.available(33, E + 1) && g.confirmed(33, E + 1));
    CHECK_EQ(g.secondsLeft(32, E), Gate::kConfirmedSec);

    // ... survives a reboot: before SNTP it stays off, after the deadline it comes back
    Gate b;
    b.restore(ep, n);
    CHECK(!b.available(1, 0));               // clock unknown: keep it off
    CHECK(!b.available(1, E + 60));          // still inside the pause
    CHECK(!b.savedExpired(E + 60));
    CHECK(b.savedExpired(ep) && b.available(1, ep));
    b.clearSaved();
    CHECK(b.available(1, 0));

    // ... and when it fails again the pause doubles: 12 h, 24 h, 48 h, 48 h
    for (uint32_t want : {2u, 4u, 8u, 8u}) {
        b.onLiveQuota(100);
        CHECK(b.onReadyWithoutSearch(101, E, ep, n, pause));
        CHECK_EQ(pause, Gate::kConfirmedSec * want);
        b.clearSaved();
    }
    CHECK(n == 5);
    // a session WITH Search that works: the streak starts over (and the saved state is cleared)
    CHECK(b.onReadyWithSearch());
    CHECK(b.strikes() == 0 && b.available(102, E));
    b.onLiveQuota(200);
    CHECK(b.onReadyWithoutSearch(201, E, ep, n, pause) && pause == Gate::kConfirmedSec);

    // A suspicion nothing settles expires by itself
    Gate u;
    u.onLiveQuota(100);
    CHECK(!u.available(100 + Gate::kSuspectSec - 1, E));
    CHECK(u.available(100 + Gate::kSuspectSec, E));

    // Confirmed before the clock synced: saved once the clock is known
    Gate c;
    c.onLiveQuota(5);
    CHECK(c.onReadyWithoutSearch(5, 0, ep, n, pause) && ep == 0);  // nothing to save yet
    CHECK(!c.available(6, 0));
    uint32_t persist = 0;
    CHECK(!c.needsSave(6, 0, persist));
    CHECK(c.needsSave(105, E, persist) && persist == E + Gate::kConfirmedSec - 100);
    CHECK(!c.needsSave(106, E + 1, persist));  // only once

    // "Try Search again" clears everything, and says whether flash must be cleared
    CHECK(g.reset());
    CHECK(g.available(33, E + 1));
    CHECK(!g.reset());
}

#include "util/DataDates.h"

TEST(data_dates) {
    using namespace datadates;
    Ymd e;
    CHECK(parseThaiDate("ราคามีผล ณ วันที่ 9 ต.ค. 69 เวลา 05.00 น.", e));
    CHECK(e.y == 2026 && e.m == 10 && e.d == 9 && e.hh == 5 && e.mm == 0);
    CHECK(parseThaiDate("มีผล 31 ธ.ค. 2569", e) && e.y == 2026 && e.m == 12 && e.d == 31 && e.hh == 0);
    CHECK(parseThaiDate("วันที่ 1 มี.ค. 70 เวลา 22:30", e) && e.y == 2027 && e.m == 3 && e.hh == 22 && e.mm == 30);
    CHECK(!parseThaiDate("ราคาน้ำมันวันนี้", e));
    CHECK(!parseThaiDate("12 ตค 69", e));  // not an abbreviation we know
    CHECK(std::string(thaiMonthAbbr(10)) == "ต.ค.");

    CHECK(parseSlashDate("08/10/2569", "20:40", e) && e.y == 2026 && e.m == 10 && e.d == 8 && e.hh == 20 && e.mm == 40);
    CHECK(parseSlashDate("15/10/2026", nullptr, e) && e.y == 2026);
    CHECK(!parseSlashDate("2569-10-08", nullptr, e));

    // 9 Oct -> 15 Oct = 6 days (what the fuel tool reports as days_since_last_change)
    Ymd now, eff;
    now.y = eff.y = 2026;
    now.m = eff.m = 10;
    now.d = 15;
    eff.d = 9;
    CHECK_EQ(now.day() - eff.day(), (int64_t)6);

    const uint32_t t = parseRfc822("Thu, 15 Oct 2026 12:14:28 GMT");
    char buf[20];
    timefmt::formatDateTime(t, 7 * 3600, buf, sizeof(buf));
    CHECK(std::string(buf) == "2026-10-15 19:14");
    CHECK_EQ(parseRfc822("Thu, 15 Oct 2026 19:14:28 +0700"), t);
    CHECK_EQ(parseRfc822("garbage"), 0u);
}

#include "util/Utf8.h"

TEST(utf8_complete_length) {
    const std::string th = "ซื้อนมพรุ่งนี้เช้า";  // 3 bytes per character
    CHECK_EQ(utf8::completeLength(th.c_str(), th.size()), th.size());
    CHECK_EQ(utf8::completeLength(th.c_str(), 40), (size_t)39);  // 13 characters + 1 stray byte
    CHECK_EQ(utf8::completeLength(th.c_str(), 41), (size_t)39);
    CHECK_EQ(utf8::completeLength(th.c_str(), 42), (size_t)42);
    CHECK_EQ(utf8::completeLength("abc", 3), (size_t)3);
    CHECK_EQ(utf8::completeLength("", 0), (size_t)0);
    // what the notes tool now does for a title cut from the text
    const size_t cut = utf8::clampToBoundary(th.c_str(), th.size(), 40);
    CHECK_EQ(cut, (size_t)39);
    CHECK_EQ(utf8::completeLength(th.c_str(), cut), cut);
}

// ============================================================
// v3.4.3: millis() wrap, VAD noise floor, transcript scaffolding, episodic reload / scan budget
// ============================================================

#include "util/Deadline.h"

TEST(deadline_wrap) {
    const uint32_t nearWrap = 0xFFFFFF00u;
    const uint32_t at = nearWrap + 1000;      // wraps past 0
    CHECK(!deadline::reached(nearWrap, at));  // `now >= at` got this right ...
    CHECK(deadline::reached(at, at));
    CHECK(deadline::reached(at + 5, at));
    // the old bug: a deadline set just before the wrap that did not overflow itself
    const uint32_t justBefore = 0xFFFFFF00u + 200;  // 0xFFFFFFC8
    const uint32_t afterWrap = 5;                   // millis() went round
    CHECK(!(afterWrap >= justBefore));              // `now >= deadline`: stuck for 49.7 days
    CHECK(deadline::reached(afterWrap, justBefore));

    CHECK_EQ(deadline::elapsed(1000, 400), 600u);
    CHECK_EQ(deadline::elapsed(1000, 1003), 0u);                 // stamped just after `now`
    CHECK_EQ(deadline::elapsed(5, 0xFFFFFFF0u), 21u);            // across the wrap
    const uint32_t day = 86400000u;
    CHECK_EQ(deadline::elapsed(30 * day, 2 * day), 28 * day);    // 28 days ago is not "just now"

    deadline::Timer t;
    CHECK(!t.active(0));
    t.arm(nearWrap, 6000);
    CHECK(t.active(nearWrap + 100));
    CHECK_EQ(t.remaining(nearWrap + 1000), 5000u);
    CHECK(!t.active(nearWrap + 6000));
    CHECK(!t.active(nearWrap + 6000 + 25 * day));  // never reads stale (a signed check flips at 24.8 days)
    t.arm(100, 600);
    t.cancel();
    CHECK(!t.active(200));
    // "0 = off" on a board up for more than 24.8 days: the old follow-up check read it as active
    const uint32_t uptime = 25 * day;
    CHECK((int32_t)(0u - uptime) > 0);
}

#include "util/NoiseFloor.h"

TEST(vad_noise_floor) {
    const uint32_t riseAfter = 10000 / 40;
    {  // a fan switched on: loud steady background stops counting as speech
        vad::NoiseFloor nf(0.002f, 0.004f, 2.5f, riseAfter);
        for (int i = 0; i < 100; i++) nf.update(0.002f, true);
        CHECK(!nf.update(0.002f, true));
        int voiced = 0, i = 0;
        for (; i < 2000; i++) {
            if (!nf.update(0.03f, true)) break;
            voiced++;
        }
        CHECK(voiced >= (int)riseAfter);  // not before 10 s ...
        CHECK(i < 2000);                  // ... but it does end
        CHECK(!nf.update(0.03f, true));
        CHECK(nf.update(0.12f, true));    // speech over the fan still counts
    }
    {  // speech with pauses never raises the floor
        vad::NoiseFloor nf(0.002f, 0.004f, 2.5f, riseAfter);
        for (int i = 0; i < 3000; i++) nf.update(i % 50 < 45 ? 0.08f : 0.002f, true);
        CHECK(nf.noise() < 0.004f);
        CHECK(nf.update(0.08f, true));
    }
    {  // radio sounding: the old behaviour (echo estimate handles it)
        vad::NoiseFloor nf(0.002f, 0.004f, 2.5f, riseAfter);
        for (int i = 0; i < 2000; i++) nf.update(0.03f, false);
        CHECK(nf.update(0.03f, false));
        CHECK_EQ(nf.loudRun(), 0u);
    }
    vad::NoiseFloor q(0.0f, 0.004f, 2.5f, riseAfter);
    CHECK(q.threshold() == 0.004f);  // never below the minimum
}

#include "util/TranscriptClean.h"

TEST(transcript_scaffolding) {
    std::string out;
    transcript::stripToolScaffolding("functionreponsetextfunctionสวัสดีค่ะ", out);
    CHECK(out == "สวัสดีค่ะ");
    transcript::stripToolScaffolding("FunctionCall ok", out);
    CHECK(out == " ok");
    transcript::stripToolScaffolding("a functional dysfunction function", out);
    CHECK(out == "a functional dysfunction ");  // real words stay (the old rule dropped them)
    transcript::stripToolScaffolding("text name id", out);
    CHECK(out == "text name id");  // no "function": not scaffolding
    transcript::stripToolScaffolding("", out);
    CHECK(out.empty());
}

TEST(episodic_reload_and_scan_budget) {
    const std::string dir = tempDir("epreload");
    const uint32_t today = 1760432400;  // 2025-10-14 16:00 Bangkok
    {  // yesterday's file exists from an earlier boot
        ltm::EpisodicLog log;
        log.open(dir.c_str(), DirectFileSink::instance(), today);
        log.append('u', "เมื่อวานคุยเรื่องแมว", today - 86400);
        log.append('a', "แมวน่ารักครับ", today - 86400 + 5);
    }
    {  // boot without a clock: only the undated file, then a turn before SNTP, then the sync
        ltm::EpisodicLog log;
        log.open(dir.c_str(), DirectFileSink::instance(), 0);
        CHECK_EQ(log.workingSize(), (size_t)0);
        log.append('u', "พูดก่อนนาฬิกาซิงก์", 0);
        log.append('u', "พูดหลังซิงก์แล้ว", today + 10);
        log.reloadDays(today);
        CHECK_EQ(log.workingSize(), (size_t)4);  // yesterday's 2 + this boot's 2 (open() used to drop them)
        PsString ctx;
        log.buildRecentContext(ctx, 10, 4096, "u", "a");
        CHECK(ctx.find("พูดก่อนนาฬิกาซิงก์") != PsString::npos);
        CHECK(ctx.find("แมวน่ารัก") < ctx.find("พูดหลังซิงก์แล้ว"));
        CHECK(ctx.find("พูดหลังซิงก์แล้ว") == ctx.rfind("พูดหลังซิงก์แล้ว"));  // already on disk: not twice
    }
    {  // a long history: older days are skipped once the read budget is used
        ltm::EpisodicLog::Options o;
        o.maxScanBytes = 2000;
        ltm::EpisodicLog log(o);
        log.open(dir.c_str(), DirectFileSink::instance(), today);
        std::string filler(300, 'x');
        for (int d = 2; d < 20; d++) {
            for (int k = 0; k < 4; k++) log.append('u', ("วันเก่า " + filler).c_str(), today - (uint32_t)d * 86400u);
        }
        log.append('u', "ร้านกาแฟใหม่เปิดวันนี้", today);
        std::vector<ltm::Turn> turns;
        int searched = 0;
        log.search("ร้านกาแฟ", 30, 5, today, turns, &searched);
        CHECK(!turns.empty() && turns[0].text.find("ร้านกาแฟ") != PsString::npos);
        CHECK(searched >= 1 && searched < 30);
        log.search("ร้านกาแฟ", 1, 5, today, turns, &searched);
        CHECK_EQ(searched, 1);
    }
}

// ============================================================
// v3.5.0: OTA logic (versions, GitHub release JSON, sha256 lines, image header, firmware marker)
// ============================================================

#include <fstream>
#include <iterator>

#include "util/OtaLogic.h"

TEST(ota_versions_and_release) {
    int v[3];
    CHECK(ota::parseVersion("v3.5.0", v) && v[0] == 3 && v[1] == 5 && v[2] == 0);
    CHECK(ota::parseVersion("3.4", v) && v[1] == 4 && v[2] == 0);
    CHECK(!ota::parseVersion("3.5.0-rc1", v));
    CHECK(!ota::parseVersion("latest", v));
    CHECK(!ota::parseVersion("1.2.3.4", v));
    CHECK(ota::compareVersions("v3.5.0", "3.4.4") > 0);
    CHECK(ota::compareVersions("v3.4.10", "3.4.9") > 0);  // numeric, not text
    CHECK(ota::compareVersions("v3.4.4", "3.4.4") == 0);
    CHECK(ota::compareVersions("v3.4.3", "3.4.4") < 0);
    CHECK(ota::compareVersions("garbage", "3.4.4") == 0);  // never "upgrade" to junk

    char sha[65];
    const std::string line = "5FB956D3D7E5430AFF8BD5ED00184ACA4175207A4B86CC5CBF18B19BD28818AF  PersonalAIBot-ESP32-v3.5.0-app.bin\n";
    CHECK(ota::parseSha256(line.c_str(), line.size(), sha));
    CHECK(std::string(sha) == "5fb956d3d7e5430aff8bd5ed00184aca4175207a4b86cc5cbf18b19bd28818af");
    CHECK(!ota::parseSha256("abc123", 6, sha));
    const std::string longer = std::string(65, 'a');
    CHECK(!ota::parseSha256(longer.c_str(), longer.size(), sha));

    const char* json =
        "{\"tag_name\":\"v3.5.0\",\"draft\":false,\"prerelease\":false,\"body\":\"notes ...\",\"assets\":["
        "{\"name\":\"PersonalAIBot-ESP32-v3.5.0-factory.bin\",\"size\":1900000,"
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v3.5.0/PersonalAIBot-ESP32-v3.5.0-factory.bin\"},"
        "{\"name\":\"PersonalAIBot-ESP32-v3.5.0-app.bin\",\"size\":1862400,"
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v3.5.0/PersonalAIBot-ESP32-v3.5.0-app.bin\"},"
        "{\"name\":\"PersonalAIBot-ESP32-v3.5.0-app.bin.sha256\",\"size\":100,"
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v3.5.0/PersonalAIBot-ESP32-v3.5.0-app.bin.sha256\"}]}";
    ota::Release r;
    CHECK(ota::parseRelease(json, strlen(json), r));
    CHECK(std::string(r.tag) == "v3.5.0");
    CHECK(std::string(r.appUrl).find("-app.bin") != std::string::npos && std::string(r.appUrl).find("sha256") == std::string::npos);
    CHECK(std::string(r.shaUrl).find("-app.bin.sha256") != std::string::npos);
    CHECK_EQ(r.appSize, 1862400u);

    const char* old = "{\"tag_name\":\"v3.4.4\",\"assets\":[{\"name\":\"x-factory.bin\",\"browser_download_url\":\"https://a/b\"}]}";
    CHECK(!ota::parseRelease(old, strlen(old), r));
    CHECK(std::string(r.tag) == "v3.4.4");  // still known: "up to date" vs "no app image" is decided from it
    const char* pre = "{\"tag_name\":\"v9.0.0\",\"prerelease\":true,\"assets\":[]}";
    CHECK(!ota::parseRelease(pre, strlen(pre), r));
    CHECK(!ota::parseRelease("not json", 8, r));
}

TEST(ota_image_checks) {
    uint8_t h[40] = {};
    CHECK(ota::checkImageHeader(h, 10, 9) != nullptr);
    h[0] = 0xE9;
    h[12] = 9;
    h[32] = 0x32, h[33] = 0x54, h[34] = 0xCD, h[35] = 0xAB;
    CHECK(ota::checkImageHeader(h, sizeof(h), 9) == nullptr);
    CHECK(ota::checkImageHeader(h, sizeof(h), 5) != nullptr);  // other chip
    h[32] = 0;
    CHECK(ota::checkImageHeader(h, sizeof(h), 9) != nullptr);

    // marker split across chunks; the scanner's own constant (prefix + NUL) is not a match
    const std::string blob = std::string("xxPAB-ESP32-FW:") + '\0' + "yyPAB-ESPAB-ESP32-FW:3.5.0" + '\0' + "zz";
    for (size_t chunk : {1, 3, 7, 64}) {
        ota::MarkerScanner m;
        for (size_t i = 0; i < blob.size(); i += chunk) {
            m.feed((const uint8_t*)blob.data() + i, std::min(chunk, blob.size() - i));
        }
        CHECK(m.found() && std::string(m.version()) == "3.5.0");
    }
    ota::MarkerScanner none;
    const std::string junk = "PAB-ESP32-FW:abc";
    none.feed((const uint8_t*)junk.data(), junk.size());
    CHECK(!none.found());

    // the real firmware image (built by "pio run"): header + marker = AppConfig's version
    std::ifstream f(std::string(fs::current_path().string()) + "/.pio/build/esp32s3/firmware.bin", std::ios::binary);
    if (f) {
        const std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(ota::checkImageHeader(img.data(), img.size(), 9) == nullptr);
        ota::MarkerScanner m;
        for (size_t i = 0; i < img.size(); i += 4096) m.feed(img.data() + i, std::min<size_t>(4096, img.size() - i));
        CHECK(m.found());
        printf("    firmware.bin: %u KB, marker v%s\n", (unsigned)(img.size() / 1024), m.version());
    }
}

// ============================================================
// v3.5.1: waking by voice over the radio with the DAC-loopback echo reference
// ============================================================

#include "util/EchoWakeGate.h"

// music: reference 0.1 for `on` chunks then silence; the mic hears coupling x the room's response,
// which lingers after the music stops (reverb, decaying `roomDecay` per 40-ms chunk)
static int runMusic(echo::WakeGate& g, float coupling, float roomDecay, int settleChunks, float speechFrom = -1,
                    float speech = 0.0f) {
    float room = 0.0f;
    int wakes = 0;
    for (int i = 0; i < 160; i++) {
        const float ref = (i % 80) < 60 ? 0.1f : 0.0f;  // 2.4 s of music, 0.8 s of silence, twice
        room = ref > room ? ref : room * roomDecay;
        float mic = coupling * room + 0.002f;
        if (speechFrom >= 0 && i >= speechFrom && i < speechFrom + 12) mic += speech;
        if (g.update(mic, ref, 0.005f, true, i < settleChunks, 0.002f)) wakes++;
    }
    return wakes;
}

TEST(echo_wake_gate) {
    echo::WakeParams p;  // factor 3, min 0.02, decay 0.6
    {  // music alone, room reverb a bit shorter than the envelope: never wakes, also when the music stops
        echo::WakeGate g(p);
        CHECK_EQ(runMusic(g, 0.3f, 0.55f, 30), 0);
        CHECK(g.coupling() > 0.25f && g.coupling() < 0.35f);
        CHECK(g.takePeakRatio() < p.factor);
        CHECK(g.takePeakRatio() == 0.0f);  // reset on read
    }
    {  // louder echo (0.06): the tail stays above the 0.02 floor for a few chunks
        echo::WakeGate g(p);
        CHECK_EQ(runMusic(g, 0.6f, 0.55f, 30), 0);  // expected with the envelope
        echo::WakeParams raw = p;
        raw.decay = 0.0f;  // reference taken as is: the reverb tail looks like speech
        echo::WakeGate r(raw);
        CHECK(runMusic(r, 0.6f, 0.55f, 30) > 0);
    }
    {  // speech over the music (0.12 on top of an echo of 0.03) wakes it
        echo::WakeGate g(p);
        CHECK(runMusic(g, 0.3f, 0.55f, 30, 40, 0.12f) >= 8);
    }
    {  // soft sound over loud music does not (below factor x echo)
        echo::WakeGate g(p);
        CHECK_EQ(runMusic(g, 0.3f, 0.55f, 30, 40, 0.03f), 0);
    }
    {  // never while settling, and a relearn starts from scratch
        echo::WakeGate g(p);
        CHECK_EQ(runMusic(g, 0.3f, 0.55f, 1000, 40, 0.12f), 0);
        g.relearn();
        CHECK(g.coupling() == 0.0f);
    }
    {  // nothing playing: the plain voice threshold
        echo::WakeGate g(p);
        CHECK(g.update(0.01f, 0.0f, 0.005f, false, false));
        CHECK(!g.update(0.004f, 0.0f, 0.005f, false, false));
    }
    {  // radio session on, its buffer ran dry (reference 0): a noisy room (0.015) must not wake it -
       // as "nothing playing" (the old radioSounding() test) the plain threshold would have
        echo::WakeGate g(p);
        runMusic(g, 0.3f, 0.55f, 30);
        int wakes = 0;
        for (int i = 0; i < 40; i++) wakes += g.update(0.015f, 0.0f, 0.012f, true, false, 0.012f) ? 1 : 0;
        CHECK_EQ(wakes, 0);
        echo::WakeGate idle(p);
        CHECK(idle.update(0.015f, 0.0f, 0.012f, false, false, 0.012f));
    }
    {  // volume up 6 dB: the coupling is carried over (x2), learning continues from there
        echo::WakeGate g(p);
        runMusic(g, 0.3f, 0.55f, 30);
        const float c = g.coupling();
        g.scaleCoupling(2.0f);
        CHECK(fabsf(g.coupling() - 2.0f * c) < 1e-4f);
        CHECK_EQ(runMusic(g, 0.6f, 0.55f, 0), 0);  // louder echo, no settling needed, no false wake
        CHECK(fabsf(g.coupling() - 0.6f) < 0.1f);
    }
    {  // speaker so loud it saturates the mic (0.5 RMS at volume 85 on the board): never a wake
        echo::WakeGate g(p);
        runMusic(g, 0.3f, 0.55f, 30);
        const float c = g.coupling();
        int wakes = 0;
        for (int i = 0; i < 20; i++) wakes += g.update(0.5f, 0.25f, 0.012f, true, false, 0.01f) ? 1 : 0;
        CHECK_EQ(wakes, 0);
        CHECK(g.coupling() == c);  // and nothing learned from it
    }
}

// ============================================================
// v3.5.1: NLMS echo canceller on the DAC-loopback reference
// ============================================================

#include <random>

#include "util/EchoCanceller.h"

TEST(echo_canceller) {
    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    // echo path like the board: 22 samples late, short decaying tail (sound through the case + desk)
    float h[64] = {};
    for (int k = 0; k < 40; k++) h[22 + k] = 0.25f * expf(-k / 6.0f) * (k % 2 ? -0.6f : 1.0f);
    const size_t N = 640, chunks = 120;
    std::vector<float> refF(N * chunks), micF(N * chunks), speech(N * chunks, 0.0f);
    for (auto& v : refF) v = 0.15f * nd(rng);
    for (size_t c = 80; c < 90; c++)  // someone talks over the music
        for (size_t i = 0; i < N; i++) speech[c * N + i] = 0.06f * nd(rng);
    for (size_t i = 0; i < micF.size(); i++) {
        float e = 0.0f;
        for (int k = 0; k < 64; k++) e += i >= (size_t)k ? h[k] * refF[i - k] : 0.0f;
        micF[i] = e + speech[i] + 0.001f * nd(rng);
    }
    auto toI16 = [](float v) { return (int16_t)std::max(-32768.0f, std::min(32767.0f, v * 32768.0f)); };
    std::vector<int16_t> mic(micF.size()), ref(refF.size()), out(N);
    for (size_t i = 0; i < mic.size(); i++) mic[i] = toI16(micF[i]), ref[i] = toI16(refF[i]);

    static echo::Canceller aec;  // ~5 KB: not on the stack
    aec.reset();
    float resMusic = 0.0f, resSpeech = 0.0f, micMusic = 0.0f, resAfter = 0.0f;
    int dt = 0, dtFalse = 0;
    for (size_t c = 0; c < chunks; c++) {
        const float res = aec.process(&mic[c * N], &ref[c * N], out.data(), N, c < 25);
        if (c >= 40 && c < 80) {
            resMusic = std::max(resMusic, res);
            micMusic = std::max(micMusic, aec.micRms());
            dtFalse += aec.doubleTalk() ? 1 : 0;
        }
        if (c >= 80 && c < 90) {
            resSpeech += res / 10;
            dt += aec.doubleTalk() ? 1 : 0;
        }
        if (c >= 95) resAfter = std::max(resAfter, res);
    }
    printf("    echo canceller: music %.4f -> %.4f (%.1f dB), speech kept %.3f of 0.06\n", micMusic, resMusic,
           20 * log10f(micMusic / resMusic), resSpeech);
    CHECK(resMusic < micMusic / 10);              // > 20 dB of echo removed
    CHECK_EQ(dtFalse, 0);                         // music alone is never taken for talk
    CHECK(dt >= 9);                               // talk over the music is
    CHECK(resSpeech > 0.045f && resSpeech < 0.08f);  // and the voice comes through
    CHECK(resAfter < micMusic / 10);              // after the talk it still cancels (it did not learn the voice)

    // loud music for a long time, then a very quiet passage (the board: the filter diverged there and
    // added a fake echo): the residual must stay below the mic
    {
        static echo::Canceller a2;
        a2.reset();
        std::vector<int16_t> m2(N), r2(N), o2(N);
        std::vector<float> hist(64, 0.0f);
        int worse = 0;
        size_t t = 0;
        for (size_t c = 0; c < 1500; c++) {  // 60 s
            const float amp = c < 1400 ? 0.3f : 0.003f;
            for (size_t i = 0; i < N; i++, t++) {
                const float r = amp * nd(rng);
                for (int k = 63; k > 0; k--) hist[k] = hist[k - 1];
                hist[0] = r;
                float e = 0.0f;
                for (int k = 0; k < 64; k++) e += h[k] * hist[k];
                r2[i] = toI16(r);
                m2[i] = toI16(e + 0.0005f * nd(rng));
            }
            const float res = a2.process(m2.data(), r2.data(), o2.data(), N, c < 25);
            if (c >= 1400 && res > a2.micRms() * 1.25f) worse++;
        }
        CHECK_EQ(worse, 0);
        CHECK_EQ(a2.takeBypasses(), 0u);
        // a wrong filter (here: 10x too strong) is caught: the mic passes through, then it starts over
        a2.scale(10.0f);
        for (size_t c = 0; c < 4; c++) {
            for (size_t i = 0; i < N; i++) {
                const float r = 0.3f * nd(rng);
                for (int k = 63; k > 0; k--) hist[k] = hist[k - 1];
                hist[0] = r;
                float e = 0.0f;
                for (int k = 0; k < 64; k++) e += h[k] * hist[k];
                r2[i] = toI16(r);
                m2[i] = toI16(e);
            }
            a2.process(m2.data(), r2.data(), o2.data(), N, false);
            if (c == 0) CHECK(memcmp(o2.data(), m2.data(), N * 2) == 0);
        }
        CHECK(a2.takeBypasses() >= 3);
        CHECK_EQ(a2.takeResets(), 1u);
    }

    // loud echo through a slightly non-linear speaker (micgain 6 on the board: 0.06-0.17): what the linear
    // filter cannot remove is above the absolute talk floor, yet it is not talk - no freezing, no bypass
    {
        static echo::Canceller a3;
        a3.reset();
        std::vector<int16_t> m3(N), r3(N), o3(N);
        std::vector<float> hist(64, 0.0f);
        int dtLoud = 0;
        for (size_t c = 0; c < 200; c++) {
            for (size_t i = 0; i < N; i++) {
                const float r = 0.3f * nd(rng);
                for (int k = 63; k > 0; k--) hist[k] = hist[k - 1];
                hist[0] = r;
                float e = 0.0f;
                for (int k = 0; k < 64; k++) e += 2.0f * h[k] * hist[k];
                e = tanhf(3.0f * e) / 3.0f;  // soft distortion
                r3[i] = toI16(r);
                m3[i] = toI16(e + 0.001f * nd(rng));
            }
            a3.process(m3.data(), r3.data(), o3.data(), N, c < 25);
            if (c >= 50) dtLoud += a3.doubleTalk() ? 1 : 0;
        }
        printf("    non-linear loud echo: residual floor %.4f, talk flags %d / 150\n", a3.residualFloor(), dtLoud);
        CHECK(a3.residualFloor() > 0.008f);  // the case: leftovers above kDtMin
        CHECK(dtLoud <= 5);
        CHECK_EQ(a3.takeBypasses(), 0u);
    }

    // nothing playing: the output is the mic, sample for sample
    std::vector<int16_t> zeros(N, 0), quiet(N);
    for (size_t i = 0; i < N; i++) quiet[i] = (int16_t)(i % 50);
    aec.process(quiet.data(), zeros.data(), out.data(), N, false);
    CHECK(!aec.passthrough());  // the first silent chunk still flushes the echo history
    aec.process(quiet.data(), zeros.data(), out.data(), N, false);
    CHECK(memcmp(out.data(), quiet.data(), N * 2) == 0);
    CHECK(aec.passthrough());
    // ...but its level is still measured in the residual's band: room hum (here 50 Hz) passed out raw
    // must not read louder than it did while the radio played (it woke the board when the radio stopped)
    std::vector<int16_t> hum(N);
    for (int rep = 0; rep < 20; rep++) {
        for (size_t i = 0; i < N; i++) hum[i] = (int16_t)(3000.0f * sinf(2.0f * 3.14159265f * 50.0f * (rep * N + i) / 16000.0f));
        aec.process(hum.data(), zeros.data(), out.data(), N, false);
    }
    float humRaw = 0.0f;
    for (size_t i = 0; i < N; i++) humRaw += (hum[i] / 32768.0f) * (hum[i] / 32768.0f);
    humRaw = sqrtf(humRaw / N);
    CHECK(aec.passthrough());
    CHECK(aec.micRms() < humRaw * 0.1f);
}

// ============================================================
// Devices on the free ports (util/DeviceConfig.h, agent/devices.json - wiki 15)
// ============================================================
#include "util/DeviceConfig.h"

TEST(device_config) {
    using namespace periph;
    // only the free pins; analog only on ADC1 (Wi-Fi stops ADC2); not the BOOT key (a UI button already)
    CHECK(pinProblem(Kind::Switch, 21) == nullptr);
    CHECK(pinProblem(Kind::Analog, 2) == nullptr);
    CHECK(pinProblem(Kind::Analog, 14) != nullptr);
    CHECK(pinProblem(Kind::Input, 0) != nullptr);
    CHECK(pinProblem(Kind::Switch, 10) != nullptr);  // the LCD's CS
    CHECK(pinProblem(Kind::Dimmer, 38) != nullptr);  // the SD card's clock

    JsonDocument doc;
    deserializeJson(doc, R"({"devices":[
        {"name":"พัดลม","type":"switch","pin":21,"active_low":true},
        {"name":"ไฟโต๊ะ","type":"dimmer","pin":2},
        {"name":"ประตู","type":"input","pin":3,"notify":true},
        {"name":"ผิด","type":"switch","pin":10},
        {"name":"ซ้ำ","type":"input","pin":21},
        {"name":"ดิน","type":"analog","pin":14},
        {"name":"มอเตอร์","type":"servo","pin":14},
        {"type":"switch","pin":14}
    ]})");
    Spec specs[kMaxDevices];
    char errors[400];
    const int n = parse(doc, specs, errors, sizeof(errors));
    CHECK_EQ(n, 3);
    CHECK(specs[0].kind == Kind::Switch && specs[0].pin == 21 && specs[0].activeLow);
    CHECK(specs[1].kind == Kind::Dimmer && !specs[1].notify);
    CHECK(specs[2].kind == Kind::Input && specs[2].pullUp && specs[2].notify);  // inputs default to a pull-up
    CHECK(strstr(errors, "ผิด: not a free pin") != nullptr);
    CHECK(strstr(errors, "ซ้ำ: pin or name used twice") != nullptr);
    CHECK(strstr(errors, "ดิน: analog only") != nullptr);
    CHECK(strstr(errors, "มอเตอร์: type must be") != nullptr);
    CHECK(strstr(errors, "?: name missing") != nullptr);

    // values the model sends for "set"
    JsonDocument v;
    v.set("on");    CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), 100);
    v.set("ปิด");   CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), 0);
    v.set("40%");   CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), 40);
    v.set(150);     CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), 100);
    v.set(true);    CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), 100);
    v.set("bright"); CHECK_EQ(parseLevel(v.as<JsonVariantConst>()), -1);

    // names the model uses: exact, a unique part, never a guess between several (T069)
    const char* names[] = {"ไฟโต๊ะ", "ไฟห้องนอน", "พัดลม", "ไฟ"};
    auto at3 = [&](int i) { return names[i]; };
    CHECK_EQ(findByName(3, "พัดลม", at3), 2);
    CHECK_EQ(findByName(3, "โต๊ะ", at3), 0);         // a part only one name has
    CHECK_EQ(findByName(3, "ไฟ", at3), kAmbiguous);  // the desk lamp or the bedroom light?
    CHECK_EQ(findByName(4, "ไฟ", at3), 3);           // ...unless a device is called exactly that
    CHECK_EQ(findByName(3, "แอร์", at3), kNoMatch);
    CHECK_EQ(findByName(3, "", at3), kNoMatch);
    CHECK_EQ(findByName(3, nullptr, at3), kNoMatch);
}

// ============================================================
// Thai provinces for get_weather without a geocoding request (agent/ThaiProvinces.h - T072)
// ============================================================
#include <set>

#include "agent/ThaiProvinces.h"

TEST(thai_provinces) {
    using thaiplace::findProvince;
    CHECK_EQ(thaiplace::kProvinceCount, (size_t)77);
    std::set<std::string> th, en;
    for (size_t i = 0; i < thaiplace::kProvinceCount; i++) {
        const thaiplace::Province& p = thaiplace::kProvinces[i];
        th.insert(p.th);
        en.insert(p.en);
        CHECK(p.lat > 5.5f && p.lat < 20.5f && p.lon > 97.3f && p.lon < 105.7f);  // inside Thailand
        CHECK(findProvince(p.th) == &p);
        CHECK(findProvince(p.en) == &p);
    }
    CHECK_EQ(th.size(), (size_t)77);
    CHECK_EQ(en.size(), (size_t)77);
    // ThaiWater's province_code (T100): TIS 1099, unique, 10 (Bangkok) .. 96 (Narathiwat)
    std::set<int> codes;
    for (size_t i = 0; i < thaiplace::kProvinceCount; i++) {
        codes.insert(thaiplace::kProvinces[i].code);
        CHECK(thaiplace::kProvinces[i].code >= 10 && thaiplace::kProvinces[i].code <= 96);
    }
    CHECK_EQ(codes.size(), (size_t)77);
    CHECK_EQ(findProvince("เชียงใหม่")->code, 50);
    CHECK_EQ(findProvince("กรุงเทพมหานคร")->code, 10);
    CHECK_EQ(findProvince("บึงกาฬ")->code, 38);
    // a province inside free text (water tool's place), whole words only: districts of other provinces that
    // start with a province name ("ตากใบ" Narathiwat, "ตากฟ้า" Nakhon Sawan) are not "ตาก"
    using thaiplace::findProvinceInText;
    CHECK(findProvinceInText("ปิง อ.จอมทอง จ.เชียงใหม่") == findProvince("เชียงใหม่"));
    CHECK(findProvinceInText("อ.เมืองตาก") == findProvince("ตาก"));
    CHECK(findProvinceInText("ตาก") == findProvince("ตาก"));
    CHECK(findProvinceInText("อ.ตากใบ") == nullptr);
    CHECK(findProvinceInText("ตากฟ้า") == nullptr);
    CHECK(findProvinceInText("ตากใบ นราธิวาส") == findProvince("นราธิวาส"));
    CHECK(findProvinceInText("สะพานนวรัฐ") == nullptr);
    CHECK(findProvinceInText("") == nullptr && findProvinceInText(nullptr) == nullptr);

    auto is = [&](const char* name, const char* want) {
        const thaiplace::Province* p = findProvince(name);
        return p && !strcmp(p->th, want);
    };
    // what the model / the user actually send
    CHECK(is("จังหวัดขอนแก่น", "ขอนแก่น"));
    CHECK(is("จ.ขอนแก่น", "ขอนแก่น"));
    CHECK(is(" จ. ขอนแก่น ", "ขอนแก่น"));
    CHECK(is("เมืองเชียงใหม่", "เชียงใหม่"));
    CHECK(is("จันทบุรี", "จันทบุรี"));  // starts with "จ": must not be read as "จ." + "ันทบุรี"
    CHECK(is("จ.จันทบุรี", "จันทบุรี"));
    CHECK(is("khon kaen", "ขอนแก่น"));
    CHECK(is("KhonKaen", "ขอนแก่น"));
    CHECK(is("Khon Kaen Province", "ขอนแก่น"));
    CHECK(is("Chonburi", "ชลบุรี"));
    CHECK(is("Lopburi", "ลพบุรี"));
    CHECK(is("Sisaket", "ศรีสะเกษ"));
    CHECK(is("Phang-nga", "พังงา"));
    CHECK(is("Nong Bua Lamphu", "หนองบัวลำภู"));
    CHECK(is("Ubon Ratchathani, Thailand", "อุบลราชธานี"));
    CHECK(is("กทม.", "กรุงเทพมหานคร"));
    CHECK(is("กรุงเทพฯ", "กรุงเทพมหานคร"));
    CHECK(is("Bangkok", "กรุงเทพมหานคร"));
    CHECK(is("โคราช", "นครราชสีมา"));
    CHECK(is("Korat", "นครราชสีมา"));
    CHECK(is("อยุธยา", "พระนครศรีอยุธยา"));
    CHECK(is("อุบล", "อุบลราชธานี"));
    // places that are not provinces go to the geocoder
    CHECK(findProvince("หาดใหญ่") == nullptr);
    CHECK(findProvince("Pattaya") == nullptr);
    CHECK(findProvince("เกาะสมุย") == nullptr);
    CHECK(findProvince("จังหวัด") == nullptr);
    CHECK(findProvince("จ") == nullptr);
    CHECK(findProvince("") == nullptr);
    CHECK(findProvince(nullptr) == nullptr);
}

// ============================================================
// Radio stream header, notes, state files (T073)
// ============================================================

#include <filesystem>

#include "util/IcyMeta.h"
#include "util/JsonFile.h"
#include "util/NoteSearch.h"

TEST(icy_meta_interval) {
    CHECK_EQ(icy::metaInterval("16000"), 16000);
    CHECK_EQ(icy::metaInterval(" 8192 "), 8192);
    CHECK_EQ(icy::metaInterval(""), 0);  // no header: no titles
    CHECK_EQ(icy::metaInterval(nullptr), 0);
    // from the server (or anyone on a plain http link): never a negative or absurd read size
    CHECK_EQ(icy::metaInterval("-5"), 0);
    CHECK_EQ(icy::metaInterval("-2147483648"), 0);
    CHECK_EQ(icy::metaInterval("+16000"), 0);
    CHECK_EQ(icy::metaInterval("0"), 0);
    CHECK_EQ(icy::metaInterval("16000abc"), 0);
    CHECK_EQ(icy::metaInterval("abc"), 0);
    CHECK_EQ(icy::metaInterval("262144"), 262144);
    CHECK_EQ(icy::metaInterval("262145"), 0);
    CHECK_EQ(icy::metaInterval("99999999999999999999"), 0);
}

TEST(notes_delete_pick) {
    JsonDocument doc;
    deserializeJson(doc, R"({"notes":[
        {"id":1,"title":"ซื้อของ","text":"นม ไข่"},
        {"id":2,"title":"สูตรแกง","text":"พริกแกง กะทิ"},
        {"id":5,"title":"ซื้อของขวัญ","text":"วันเกิดแม่"}]})");
    JsonArrayConst notes = doc["notes"];
    CHECK_EQ(notebook::pickForDelete(notes, 2, ""), 1);
    CHECK_EQ(notebook::pickForDelete(notes, 5, "ไม่เกี่ยว"), 2);  // the id wins over the query
    CHECK_EQ(notebook::pickForDelete(notes, 9, ""), notebook::kNone);
    CHECK_EQ(notebook::pickForDelete(notes, 0, "แกง"), 1);       // one match: that one
    CHECK_EQ(notebook::pickForDelete(notes, 0, "ซื้อ"), notebook::kAmbiguous);  // used to delete both
    CHECK_EQ(notebook::pickForDelete(notes, 0, "ไม่มี"), notebook::kNone);
    CHECK_EQ(notebook::pickForDelete(notes, 0, ""), notebook::kNone);  // nothing given: nothing picked
    CHECK(notebook::matches(notes[0], 0, "นม"));                     // text counts too
    CHECK(!notebook::matches(notes[0], 0, nullptr));
}

TEST(json_file_status) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pab_jsonfile_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "notes.json").string();
    auto write = [](const std::string& p, const char* text) {
        FILE* f = fopen(p.c_str(), "wb");
        fputs(text, f);
        fclose(f);
    };
    JsonDocument doc;
    CHECK(jsonfile::loadStatus(path.c_str(), doc) == jsonfile::Status::Missing);
    write(path, "");
    CHECK(jsonfile::loadStatus(path.c_str(), doc) == jsonfile::Status::Missing);  // empty: nothing to lose
    write(path, "{\"notes\":[{\"id\":1,\"text\":\"a\"");                          // cut short / damaged
    CHECK(jsonfile::loadStatus(path.c_str(), doc) == jsonfile::Status::Unreadable);
    CHECK(!jsonfile::load(path.c_str(), doc));

    doc.clear();
    doc["notes"].to<JsonArray>().add<JsonObject>()["id"] = 7;
    CHECK(jsonfile::save(path.c_str(), doc));
    JsonDocument back;
    CHECK(jsonfile::loadStatus(path.c_str(), back) == jsonfile::Status::Ok);
    CHECK_EQ(back["notes"][0]["id"].as<int>(), 7);

    // power cut between "remove" and "rename": the .tmp copy is read
    fs::rename(path, path + ".tmp");
    JsonDocument tmp;
    CHECK(jsonfile::loadStatus(path.c_str(), tmp) == jsonfile::Status::Ok);
    CHECK_EQ(tmp["notes"][0]["id"].as<int>(), 7);
    fs::remove_all(dir);
}

// ============================================================
// Signed updates, radio redirects (T074)
// ============================================================

#include "util/HttpRedirect.h"

TEST(ota_signature_asset) {
    const char* json =
        "{\"tag_name\":\"v4.2.0\",\"assets\":["
        "{\"name\":\"PersonalAIBot-ESP32-v4.2.0-app.bin\",\"size\":3398455,\"browser_download_url\":\"https://g/a-app.bin\"},"
        "{\"name\":\"PersonalAIBot-ESP32-v4.2.0-app.bin.sha256\",\"browser_download_url\":\"https://g/a-app.bin.sha256\"},"
        "{\"name\":\"PersonalAIBot-ESP32-v4.2.0-app.bin.sig\",\"browser_download_url\":\"https://g/a-app.bin.sig\"}]}";
    ota::Release r;
    CHECK(ota::parseRelease(json, strlen(json), r));
    CHECK(std::string(r.sigUrl) == "https://g/a-app.bin.sig");
    CHECK(std::string(r.appUrl) == "https://g/a-app.bin");
    const char* unsignedRel =
        "{\"tag_name\":\"v4.1.0\",\"assets\":["
        "{\"name\":\"x-app.bin\",\"browser_download_url\":\"https://g/x-app.bin\"},"
        "{\"name\":\"x-app.bin.sha256\",\"browser_download_url\":\"https://g/x-app.bin.sha256\"}]}";
    CHECK(ota::parseRelease(unsignedRel, strlen(unsignedRel), r));  // complete; a board with a key refuses it
    CHECK(r.sigUrl[0] == '\0');

    // tools/make_release.py writes base64(DER) + newline - a real one (71 bytes)
    const char* sigText = "MEUCIQCspirxglv2EUsXBN/psNOZFUvjIoaWghoTsWz2RRS+2gIgfsUXhQRrUlTug6lx60CG9/8Ram2kvDJG5jydDL+TKZo=\n";
    uint8_t sig[80];
    CHECK_EQ(ota::parseSignature(sigText, strlen(sigText), sig, sizeof(sig)), (size_t)71);
    CHECK_EQ(sig[0], 0x30);
    const std::string padded = "  " + std::string(sigText, strlen(sigText) - 1) + "\r\n";  // CRLF from a Windows PC
    CHECK_EQ(ota::parseSignature(padded.c_str(), padded.size(), sig, sizeof(sig)), (size_t)71);
    CHECK_EQ(ota::parseSignature("", 0, sig, sizeof(sig)), (size_t)0);
    CHECK_EQ(ota::parseSignature("not base64!!", 12, sig, sizeof(sig)), (size_t)0);
    CHECK_EQ(ota::parseSignature("QUJDREVGR0hJSktM", 16, sig, sizeof(sig)), (size_t)0);  // base64, not DER
    const std::string twoLines = std::string(sigText) + "MEUCIQ==\n";
    CHECK_EQ(ota::parseSignature(twoLines.c_str(), twoLines.size(), sig, sizeof(sig)), (size_t)0);
    const std::string html = "<html><body>Not Found</body></html>";  // an error page instead of the asset
    CHECK_EQ(ota::parseSignature(html.c_str(), html.size(), sig, sizeof(sig)), (size_t)0);
    uint8_t small[16];
    CHECK_EQ(ota::parseSignature(sigText, strlen(sigText), small, sizeof(small)), (size_t)0);  // never overruns
}

TEST(radio_redirect_resolve) {
    char out[128];
    auto is = [&](const char* base, const char* loc, const char* want) {
        return redirect::resolve(base, loc, out, sizeof(out)) && !strcmp(out, want);
    };
    CHECK(is("http://a.com/x", "https://cdn.b.com/live.mp3", "https://cdn.b.com/live.mp3"));
    CHECK(is("https://a.com:8000/x/y.pls", "/stream", "https://a.com:8000/stream"));
    CHECK(is("http://a.com/x/y.pls?id=3", "live.mp3", "http://a.com/x/live.mp3"));
    CHECK(is("http://a.com/x/", "live.mp3", "http://a.com/x/live.mp3"));
    CHECK(is("http://a.com", "live.mp3", "http://a.com/live.mp3"));
    CHECK(is("http://a.com:8000", "/;", "http://a.com:8000/;"));
    CHECK(is("https://a.com/x", "//cdn.b.com/s", "https://cdn.b.com/s"));
    CHECK(is("http://a.com/radio?x=1", "?x=2", "http://a.com/radio?x=2"));
    CHECK(is("http://a.com/x", " /stream \r", "http://a.com/stream"));  // stray spaces / CR from the header
    CHECK(!redirect::resolve("http://a.com/x", "", out, sizeof(out)));
    CHECK(!redirect::resolve("not a url", "/stream", out, sizeof(out)));
    CHECK(!redirect::resolve(nullptr, "/s", out, sizeof(out)));
    char tiny[16];
    CHECK(!redirect::resolve("http://a.com/x", "/a-very-long-stream-path", tiny, sizeof(tiny)));  // never overruns
    CHECK(redirect::resolve("http://a.com/x", "/s", tiny, sizeof(tiny)) && !strcmp(tiny, "http://a.com/s"));
}

// ============================================================
// Remembered Wi-Fi networks (T099)
// ============================================================

#include "util/WifiKnown.h"

TEST(wifi_known_networks) {
    wifiknown::List l;
    CHECK(l.remember("Home", "pass1"));
    CHECK(!l.remember("Home", "pass1"));        // already the latest, same password: nothing to write
    CHECK(l.remember("Office", "pw2"));
    CHECK(l.remember("Home", "pass1"));         // used again: becomes the latest
    CHECK(l.remember("Home", "newpass"));
    CHECK_EQ(l.size(), (size_t)2);
    CHECK(!strcmp(l.at((size_t)l.find("Home")).pass, "newpass"));
    CHECK(l.remember("Cafe", ""));              // open network
    CHECK(!l.remember("", "x"));                // no name
    CHECK(!l.remember("123456789012345678901234567890123", "x"));  // 33 bytes: too long for an SSID

    // full: the least recently used goes (Office)
    for (const char* s : {"A", "B", "C"}) CHECK(l.remember(s, "p"));
    CHECK_EQ(l.size(), wifiknown::kMax);
    CHECK(l.remember("D", "p"));
    CHECK_EQ(l.size(), wifiknown::kMax);
    CHECK(l.find("Office") < 0 && l.find("Home") >= 0 && l.find("D") >= 0);
    CHECK(l.forget("D") && !l.forget("D"));

    // pick: strongest in range; within 8 dB the more recently used; unknown networks ignored
    wifiknown::List p;
    p.remember("Old", "1");
    p.remember("Recent", "2");
    const wifiknown::Seen far[] = {{"Neighbour", -40}, {"Old", -50}, {"Recent", -70}};
    CHECK(!strcmp(p.at((size_t)p.pick(far, 3)).ssid, "Old"));     // 20 dB stronger wins
    const wifiknown::Seen close[] = {{"Old", -55}, {"Recent", -60}};
    CHECK(!strcmp(p.at((size_t)p.pick(close, 2)).ssid, "Recent"));  // about the same: the one used last
    const wifiknown::Seen none[] = {{"Neighbour", -40}, {nullptr, -30}};
    CHECK_EQ(p.pick(none, 2), -1);
    CHECK_EQ(p.pick(nullptr, 0), -1);

    // the load path keeps the stored order numbers and refuses duplicates
    wifiknown::List q;
    CHECK(q.add("X", "1", 7) && !q.add("X", "2", 9));
    CHECK(q.remember("Y", "3") && q.at((size_t)q.find("Y")).used == 8);
}


// ============================================================
// ThaiWater answers (util/ThaiWater.h, T100) - trimmed real records from 2026-10-25
// ============================================================

#include "util/ThaiWater.h"

TEST(thaiwater_parsing) {
    using namespace thaiwater;
    // situation by % of the bank (ThaiWater's own scale)
    CHECK_EQ(levelFromPercent(NAN), 0);
    CHECK_EQ(levelFromPercent(-13.67f), 1);
    CHECK_EQ(levelFromPercent(10), 1);
    CHECK_EQ(levelFromPercent(27.76f), 2);
    CHECK_EQ(levelFromPercent(30.01f), 3);
    CHECK_EQ(levelFromPercent(72.37f), 4);
    CHECK_EQ(levelFromPercent(124.7f), 5);
    CHECK(!strcmp(levelTh(5), "น้ำล้นตลิ่ง"));

    // public/waterlevel?province_code=50
    JsonDocument wl;
    CHECK(!deserializeJson(wl, R"JSON({"result": "OK", "data": [{"waterlevel_datetime": "2026-10-25 22:20", "waterlevel_msl": "794.36", "storage_percent": "-13.67", "diff_wl_bank": "1.95", "station": {"id": 1121228, "tele_station_name": {"en": "Mae Tom Bridge", "th": "สะพานน้ำแม่ต๋อม"}, "tele_station_oldcode": "MOU298"}, "basin": {"basin_name": {"th": "ลุ่มน้ำปิง", "en": "Ping Basin"}}, "geocode": {"amphoe_name": {"th": "อมก๋อย", "en": "Omkoi District"}, "tumbon_name": {"th": "อมก๋อย", "en": "Omkoi"}}}, {"waterlevel_datetime": "2026-10-25 22:00", "waterlevel_msl": "273.01", "storage_percent": "27.76", "diff_wl_bank": "7.62", "station": {"id": 1422304, "tele_station_name": {"th": "แม่น้ำปิงที่ อ.จอมทอง"}, "tele_station_oldcode": "BBU01"}, "basin": {"basin_name": {"th": "ลุ่มน้ำปิง", "en": "Ping Basin"}}, "geocode": {"amphoe_name": {"th": "ดอยหล่อ", "en": "Doi Lo District"}, "tumbon_name": {"th": "ดอยหล่อ", "en": "Doi Lo"}}}, {"waterlevel_datetime": "2026-10-25 18:00", "waterlevel_msl": "265.06", "storage_percent": "72.37", "diff_wl_bank": "2.99", "station": {"id": 3164, "tele_station_name": {"th": "บ้านสบสอย"}, "tele_station_oldcode": "P.73"}, "basin": {"basin_name": {"th": "ลุ่มน้ำปิง", "en": "Ping Basin"}}, "geocode": {"amphoe_name": {"th": "จอมทอง", "en": "Chom Thong District"}, "tumbon_name": {"th": "แม่สอย", "en": "Mae Soi"}}}]})JSON"));
    WaterLevel st[8];
    size_t matched = 0;
    size_t n = pickWaterLevels(wl["data"], "", st, 8, matched);
    CHECK_EQ(n, (size_t)3);
    CHECK_EQ(matched, (size_t)3);
    CHECK(!strcmp(st[0].name, "บ้านสบสอย") && st[0].id == 3164 && st[0].level == 4);  // most filled first
    CHECK(!strcmp(st[0].code, "P.73") && !strcmp(st[0].river, "ลุ่มน้ำปิง") && !strcmp(st[0].place, "อ.จอมทอง"));
    CHECK(fabsf(st[0].msl - 265.06f) < 0.01f && fabsf(st[0].toBank - 2.99f) < 0.01f);
    CHECK(!strcmp(st[0].time, "2026-10-25 18:00"));
    CHECK_EQ(st[1].level, 2);
    CHECK_EQ(st[2].level, 1);  // a negative % = below the gauge's low mark: critically low
    n = pickWaterLevels(wl["data"], "จอมทอง", st, 8, matched);  // station name or district
    CHECK_EQ(matched, (size_t)2);
    n = pickWaterLevels(wl["data"], "ปิง", st, 8, matched);     // the basin
    CHECK_EQ(matched, (size_t)3);
    n = pickWaterLevels(wl["data"], "P.73", st, 8, matched);    // the station code
    CHECK(matched == 1 && st[0].id == 3164);
    n = pickWaterLevels(wl["data"], "", st, 2, matched);        // only the 2 most filled are kept
    CHECK(n == 2 && matched == 3 && st[1].level == 2);
    CHECK_EQ(pickWaterLevels(wl["data"], "ไม่มีสถานีนี้", st, 8, matched), (size_t)0);

    // public/waterlevel_graph: nulls skipped, averaged into buckets
    JsonDocument gr;
    CHECK(!deserializeJson(gr, R"JSON({"result": "OK", "data": {"graph_data": [{"datetime": "2026-10-23 00:00", "value": 3.324}, {"datetime": "2026-10-23 00:10", "value": 3.324}, {"datetime": "2026-10-23 00:20", "value": 3.325}, {"datetime": "2026-10-23 00:30", "value": 3.323}, {"datetime": "2026-10-23 00:40", "value": 3.323}, {"datetime": "2026-10-25 22:40", "value": null}], "min_bank": 2.139, "warning_level": null, "critical_level": null}})JSON"));
    float v[60];
    char first[17], last[17];
    CHECK_EQ(downsample(gr["data"]["graph_data"], v, 60, first, last, sizeof(first)), (size_t)5);
    CHECK(!strcmp(first, "2026-10-23 00:00") && !strcmp(last, "2026-10-23 00:40"));
    CHECK_EQ(downsample(gr["data"]["graph_data"], v, 2, first, last, sizeof(first)), (size_t)2);
    CHECK(fabsf(v[0] - 3.3243f) < 0.001f && fabsf(v[1] - 3.323f) < 0.001f);
    CHECK(fabsf(num(gr["data"]["min_bank"]) - 2.139f) < 0.001f);
    CHECK(isnan(num(gr["data"]["warning_level"])));
    char day[12];
    CHECK(dayBefore("2026-10-01 00:10", 1, day, sizeof(day)) && !strcmp(day, "2026-09-30"));
    CHECK(dayBefore("2028-03-01 06:00", 1, day, sizeof(day)) && !strcmp(day, "2028-02-29"));
    CHECK(dayBefore("2026-10-25 22:30", 0, day, sizeof(day)) && !strcmp(day, "2026-10-25"));
    CHECK(!dayBefore("", 1, day, sizeof(day)));

    // public/rain_24h?province_code=50
    JsonDocument rn;
    CHECK(!deserializeJson(rn, R"JSON({"result": "OK", "data": [{"rain_24h": 19.2, "rainfall_datetime": "2026-10-25 22:00", "station": {"tele_station_name": {"en": "Piang Luang", "th": "อบต.เปียงหลวง"}}, "geocode": {"amphoe_name": {"th": "เวียงแหง", "en": "Wiang Haeng District"}, "tumbon_name": {"th": "เปียงหลวง", "en": "Piang Luang"}}}, {"rain_24h": 12, "rainfall_datetime": "2026-10-25 21:00", "station": {"tele_station_name": {"th": "บ้านม่วงเครือ"}}, "geocode": {"amphoe_name": {"th": "เวียงแหง", "en": "Wiang Haeng District"}, "tumbon_name": {"th": "เปียงหลวง", "en": "Piang Luang"}}}, {"rain_24h": 0, "rainfall_datetime": "2026-10-25 22:00", "station": {"tele_station_name": {"en": "Mae Sao bridge", "th": "สะพานห้วยแม่สาว"}}, "geocode": {"amphoe_name": {"th": "แม่อาย", "en": "Mae Ai District"}, "tumbon_name": {"th": "แม่สาว", "en": "Mae Sao"}}}, {"rain_24h": 0, "rainfall_datetime": "2026-10-25 21:00", "station": {"tele_station_name": {"th": "บ้านปางอั้น"}}, "geocode": {"amphoe_name": {"th": "ดอยสะเก็ด", "en": "Doi Saket District"}, "tumbon_name": {"th": "ป่าเมี่ยง", "en": "Pa Miang"}}}]})JSON"));
    Rain top[8];
    RainSummary sum;
    n = pickRain(rn["data"], "rain_24h", "", top, 8, sum);
    CHECK_EQ(n, (size_t)4);
    CHECK(!strcmp(top[0].name, "อบต.เปียงหลวง") && fabsf(top[0].mm - 19.2f) < 0.01f && !strcmp(top[0].place, "อ.เวียงแหง"));
    CHECK(sum.stations == 4 && sum.wet == 2 && fabsf(sum.max - 19.2f) < 0.01f && fabsf(sum.avg - 7.8f) < 0.01f);
    CHECK(!strcmp(sum.time, "2026-10-25 22:00"));
    n = pickRain(rn["data"], "rain_24h", "เวียงแหง", top, 8, sum);
    CHECK(sum.stations == 2 && n == 2);
    CHECK(!strcmp(rainTh(0), "ไม่มีฝน") && !strcmp(rainTh(19.2f), "ฝนปานกลาง") && !strcmp(rainTh(40), "ฝนหนัก"));
    // rain_7d comes as a string ("25.90")
    JsonDocument r7;
    CHECK(!deserializeJson(r7, R"({"data":[{"rain_7d":"25.90","station":{"tele_station_name":{"th":"A"}},"geocode":{}}]})"));
    CHECK(pickRain(r7["data"], "rain_7d", "", top, 8, sum) == 1 && fabsf(top[0].mm - 25.9f) < 0.01f);
    char lbl[64];  // like WaterLevel::place (Thai = 3 bytes a character)
    districtLabel(lbl, sizeof(lbl), "เขตบางรัก");
    CHECK(!strcmp(lbl, "เขตบางรัก"));
    districtLabel(lbl, sizeof(lbl), "บางขุนเทียน", true);  // ThaiWater's Bangkok districts come without "เขต"
    CHECK(!strcmp(lbl, "เขตบางขุนเทียน"));

    // "P.1" is that station, not also P.13 / P.17 (board test 2026-10-25); a part of a code still finds them all
    JsonDocument codes;
    CHECK(!deserializeJson(codes, R"({"data":[
        {"storage_percent":"51","station":{"id":1,"tele_station_name":{"th":"สะพานนวรัฐ"},"tele_station_oldcode":"P.1"},"geocode":{}},
        {"storage_percent":"60","station":{"id":13,"tele_station_name":{"th":"บ้านหนึ่ง"},"tele_station_oldcode":"P.13"},"geocode":{}},
        {"storage_percent":"40","station":{"id":17,"tele_station_name":{"th":"บ้านสอง"},"tele_station_oldcode":"P.17"},"geocode":{}}]})"));
    n = pickWaterLevels(codes["data"], "P.1", st, 8, matched);
    CHECK(matched == 1 && st[0].id == 1);
    n = pickWaterLevels(codes["data"], "สะพานนวรัฐ", st, 8, matched);
    CHECK(matched == 1 && st[0].id == 1);
    n = pickWaterLevels(codes["data"], "P.", st, 8, matched);
    CHECK(matched == 3 && st[0].id == 13);
}

// whole country: one row per province, the highest station first (T100 province rankings)
TEST(thaiwater_province_ranking) {
    using namespace thaiwater;
    JsonDocument rain;
    CHECK(!deserializeJson(rain, R"({"data":[
        {"rain_24h":19.2,"station":{"tele_station_name":{"th":"อบต.เปียงหลวง"}},"geocode":{"province_code":"50","province_name":{"th":"เชียงใหม่"},"amphoe_name":{"th":"เวียงแหง"}}},
        {"rain_24h":0,"station":{"tele_station_name":{"th":"บ้านปางอั้น"}},"geocode":{"province_code":"50","province_name":{"th":"เชียงใหม่"},"amphoe_name":{"th":"ดอยสะเก็ด"}}},
        {"rain_24h":74.5,"station":{"tele_station_name":{"th":"ส.คลองสนามชัย"}},"geocode":{"province_code":"10","province_name":{"th":"กรุงเทพมหานคร"},"amphoe_name":{"th":"บางขุนเทียน"}}},
        {"rain_24h":8,"station":{"tele_station_name":{"th":"ปตร.บึงขวาง"}},"geocode":{"province_code":"10","province_name":{"th":"กรุงเทพมหานคร"},"amphoe_name":{"th":"มีนบุรี"}}},
        {"rain_24h":30,"station":{"tele_station_name":{"th":"อุ้มผาง"}},"geocode":{"province_code":"63","province_name":{"th":"ตาก"},"amphoe_name":{"th":"อุ้มผาง"}}},
        {"rain_24h":99,"station":{"tele_station_name":{"th":"เวียงจันทน์"}},"geocode":{"province_code":"","province_name":{"th":"สาธารณรัฐประชาธิปไตยประชาชนลาว"}}},
        {"rain_24h":null,"station":{"tele_station_name":{"th":"ไม่มีค่า"}},"geocode":{"province_code":"63"}}]})"));
    static ProvinceRank slots[kProvinceSlots];  // ~25 KB (the board keeps it in PSRAM)
    ProvinceRank top[8];
    size_t provinces = 0, stations = 0;
    // fed one station at a time, as the board parses the nationwide answer
    auto rank = [&](JsonArrayConst data, const char* key, bool level, const char* query, size_t max) {
        ProvinceRanker r(slots, key, level, query);
        for (JsonObjectConst s : data) r.add(s);
        stations = r.stations();
        return r.top(top, max, provinces);
    };
    size_t n = rank(rain["data"], "rain_24h", false, "", 8);
    CHECK_EQ(n, (size_t)3);
    CHECK(provinces == 3 && stations == 5);  // Laos and the missing value left out
    CHECK(top[0].code == 10 && !strcmp(top[0].name, "กรุงเทพมหานคร") && fabsf(top[0].value - 74.5f) < 0.01f);
    CHECK(!strcmp(top[0].station, "ส.คลองสนามชัย") && !strcmp(top[0].place, "เขตบางขุนเทียน"));
    CHECK(top[0].stations == 2 && top[0].count == 2);
    CHECK(top[1].code == 63 && top[2].code == 50);
    CHECK(top[2].stations == 2 && top[2].count == 1);  // 0 mm is not "with rain"
    n = rank(rain["data"], "rain_24h", false, "", 2);
    CHECK(n == 2 && provinces == 3 && top[1].code == 63);  // only the top 2 kept, all 3 counted

    JsonDocument wl;
    CHECK(!deserializeJson(wl, R"({"data":[
        {"storage_percent":"124.7","station":{"tele_station_name":{"th":"วัดท่าเจดีย์"},"tele_station_oldcode":"TTC06"},"basin":{"basin_name":{"th":"ลุ่มน้ำท่าจีน"}},"geocode":{"province_code":"72","province_name":{"th":"สุพรรณบุรี"},"amphoe_name":{"th":"สองพี่น้อง"}}},
        {"storage_percent":"80","station":{"tele_station_name":{"th":"ข"}},"basin":{"basin_name":{"th":"ลุ่มน้ำท่าจีน"}},"geocode":{"province_code":"72","province_name":{"th":"สุพรรณบุรี"}}},
        {"storage_percent":"40","station":{"tele_station_name":{"th":"ค"}},"basin":{"basin_name":{"th":"ลุ่มน้ำท่าจีน"}},"geocode":{"province_code":"72","province_name":{"th":"สุพรรณบุรี"}}},
        {"storage_percent":"72.37","station":{"tele_station_name":{"th":"บ้านสบสอย"},"tele_station_oldcode":"P.73"},"basin":{"basin_name":{"th":"ลุ่มน้ำปิง"}},"geocode":{"province_code":"50","province_name":{"th":"เชียงใหม่"},"amphoe_name":{"th":"จอมทอง"}}},
        {"storage_percent":"-13.67","station":{"tele_station_name":{"th":"สะพานน้ำแม่ต๋อม"}},"basin":{"basin_name":{"th":"ลุ่มน้ำปิง"}},"geocode":{"province_code":"50","province_name":{"th":"เชียงใหม่"}}}]})"));
    n = rank(wl["data"], "storage_percent", true, "", 8);
    CHECK(n == 2 && top[0].code == 72 && fabsf(top[0].value - 124.7f) < 0.1f);
    CHECK(top[0].over == 1 && top[0].count == 2 && top[0].stations == 3);  // > 100 %: 1, > 70 %: 2
    CHECK(!strcmp(top[0].river, "ลุ่มน้ำท่าจีน") && !strcmp(top[0].station, "วัดท่าเจดีย์"));
    CHECK(top[1].code == 50 && top[1].over == 0 && top[1].count == 1);
    n = rank(wl["data"], "storage_percent", true, "ปิง", 8);  // one basin
    CHECK(n == 1 && top[0].code == 50 && stations == 2);

    // province_code as a number works too (the API sends "50" today)
    JsonDocument numeric;
    CHECK(!deserializeJson(numeric, R"({"data":[
        {"rain_24h":5,"station":{"tele_station_name":{"th":"ก"}},"geocode":{"province_code":50,"province_name":{"th":"เชียงใหม่"}}},
        {"rain_24h":9,"station":{"tele_station_name":{"th":"ข"}},"geocode":{"province_code":"63","province_name":{"th":"ตาก"}}}]})"));
    n = rank(numeric["data"], "rain_24h", false, "", 8);
    CHECK(n == 2 && top[0].code == 63 && top[1].code == 50 && stations == 2);
    CHECK_EQ(intOf(numeric["data"][0]["geocode"]["province_code"]), 50);
    CHECK_EQ(intOf(numeric["data"][1]["geocode"]["province_code"]), 63);
    CHECK_EQ(intOf(numeric["data"][0]["missing"]), 0);

    // the shared "best first, at most max" insertion: ties keep arrival order, overflow drops the lowest
    int best[3];
    size_t kept = 0;
    for (int v : {5, 9, 1, 9, 7}) kept = insertTop(best, kept, 3, v, [](int x) { return (float)x; });
    CHECK(kept == 3 && best[0] == 9 && best[1] == 9 && best[2] == 7);
}

// a card's short time (graph axis, water cards)
TEST(time_short_stamp) {
    char buf[40];
    timefmt::shortStamp("2026-10-25 22:30", buf, sizeof(buf));
    CHECK(!strcmp(buf, "25/10 22:30"));
    timefmt::shortStamp("2026-10-05", buf, sizeof(buf));
    CHECK(!strcmp(buf, "5/10"));  // no time: no made-up 00:00
    timefmt::shortStamp("เมื่อวาน", buf, sizeof(buf));
    CHECK(!strcmp(buf, "เมื่อวาน"));
    timefmt::shortStamp(nullptr, buf, sizeof(buf));
    CHECK(!buf[0]);
}
