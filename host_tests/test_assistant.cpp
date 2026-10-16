/**
 * @file test_assistant.cpp
 * @brief Host tests: personas, scheduler, to-do list and the full tool layer
 */

#include <ArduinoJson.h>

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
    CHECK_EQ(count, (size_t)42);  // 25 core (incl. configure_device, rest_screen, mute_microphone, play_radio, stop_radio) + 5 live data + 6 utilities + 6 finance
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
    CHECK(bytes < 15000);  // billed every turn: keep the declarations compact
    printf("    tool declarations: %u bytes JSON\n", (unsigned)bytes);

    // optional finance pack off -> 36 (no trading tools exist at all)
    rig.tools.setFinancePack(false);
    behaviors(ToolBehavior::Blocking, false, expr, recall, count, search);
    CHECK_EQ(count, (size_t)36);
    rig.tools.setFinancePack(true);
    behaviors(ToolBehavior::Blocking, false, expr, recall, count, search);
    CHECK_EQ(count, (size_t)42);
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
