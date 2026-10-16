/**
 * @file AgentTools.cpp
 * @brief Tool declarations and dispatch for the Gemini Live personal assistant
 */

#include "AgentTools.h"

#include <stdio.h>
#include <string.h>

#include "../ui/Emotion.h"
#include "../util/TimeFmt.h"
#include "Personas.h"

namespace {

struct Param {
    const char* name;
    const char* type;  // Gemini Schema type names
    const char* description;
    bool required;
    const char* const* enumValues;  // nullptr-terminated (STRING only)
};

const char* const kCategories[] = {"profile", "preference", "person", "event", "task", "knowledge", "other", nullptr};
const char* const kRepeats[] = {"none", "daily", "weekdays", "weekly", nullptr};
const char* const kTaskActions[] = {"done", "undone", "delete", "rename", nullptr};
const char* const kOptionKinds[] = {"profiles", "voices", "personalities", "eye_colors", nullptr};
const char* const kGenders[] = {"female", "male", "neutral", nullptr};

void declare(JsonArray decls, ToolBehavior behavior, const char* name, const char* description,
             const Param* params = nullptr, size_t count = 0) {
    JsonObject f = decls.add<JsonObject>();
    f["name"] = name;
    f["description"] = description;
    if (behavior == ToolBehavior::Blocking) {
        f["behavior"] = AgentTools::isSilentTool(name) ? "NON_BLOCKING" : "BLOCKING";
    } else if (behavior == ToolBehavior::NonBlocking) {
        f["behavior"] = "NON_BLOCKING";
    }
    if (count == 0) return;

    JsonObject schema = f["parameters"].to<JsonObject>();
    schema["type"] = "OBJECT";
    JsonObject props = schema["properties"].to<JsonObject>();
    JsonArray required;
    for (size_t i = 0; i < count; i++) {
        const Param& p = params[i];
        JsonObject s = props[p.name].to<JsonObject>();
        s["type"] = p.type;
        if (p.description) s["description"] = p.description;
        if (p.enumValues) {
            JsonArray e = s["enum"].to<JsonArray>();
            for (const char* const* v = p.enumValues; *v; v++) e.add(*v);
        }
        if (p.required) {
            if (required.isNull()) required = schema["required"].to<JsonArray>();
            required.add(p.name);
        }
    }
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void writeRecord(JsonObject o, const ltm::Record& r, int32_t off, bool viaGraph) {
    char date[16];
    timefmt::formatDate(r.updated, off, date, sizeof(date));
    o["id"] = r.id;
    o["type"] = r.kind == ltm::Kind::Fact ? "fact" : "relation";
    o["category"] = ltm::categoryName(r.category);
    o["content"] = r.text.c_str();
    o["importance"] = r.importance;
    o["saved"] = date;
    if (viaGraph) o["via"] = "knowledge_graph";
}

void writeWhen(JsonObject o, const char* key, uint32_t t, int32_t off) {
    char buf[24];
    timefmt::formatDateTime(t, off, buf, sizeof(buf));
    o[key] = buf;
}

}  // namespace

const char* const AgentTools::kLedColors[] = {"red",    "green",  "blue", "white", "yellow",
                                              "purple", "orange", "off",  "auto",  nullptr};

const AgentTools::Entry AgentTools::kHandlers[] = {
    {"save_memory", &AgentTools::saveMemory},
    {"save_relation", &AgentTools::saveRelation},
    {"recall_memory", &AgentTools::recallMemory},
    {"forget_memory", &AgentTools::forgetMemory},
    {"recall_conversation", &AgentTools::recallConversation},
    {"set_timer", &AgentTools::setTimer},
    {"set_reminder", &AgentTools::setReminder},
    {"list_reminders", &AgentTools::listReminders},
    {"cancel_reminder", &AgentTools::cancelReminder},
    {"add_task", &AgentTools::addTask},
    {"list_tasks", &AgentTools::listTasks},
    {"update_task", &AgentTools::updateTask},
    {"get_device_status", &AgentTools::deviceStatus},
    {"set_expression", &AgentTools::setExpression},
    {"configure_assistant", &AgentTools::configureAssistant},
    {"list_assistant_options", &AgentTools::listAssistantOptions},
    {"set_volume", &AgentTools::setVolume},
    {"set_brightness", &AgentTools::setBrightness},
    {"configure_device", &AgentTools::configureDevice},
    {"control_led", &AgentTools::controlLed},
    {"go_to_sleep", &AgentTools::goToSleep},
    {"mute_microphone", &AgentTools::muteMicrophone},
    {"rest_screen", &AgentTools::restScreen},
    {"stop_radio", &AgentTools::stopRadio},
    {"play_radio", &AgentTools::playRadio},
    {nullptr, nullptr},
};

AgentTools::AgentTools(ltm::MemoryStore& memory, ltm::EpisodicLog& episodes, sched::Scheduler& scheduler,
                       todo::TaskList& tasks, DeviceOps& device, int32_t utcOffsetSec)
    : _memory(memory), _episodes(episodes), _scheduler(scheduler), _tasks(tasks), _device(device),
      _utcOffset(utcOffsetSec) {}

bool AgentTools::isSilentTool(const char* name) { return name && strcmp(name, "set_expression") == 0; }

// ============================================================
// Declarations (keep descriptions short: they are billed every turn)
// ============================================================

void AgentTools::addDeclarations(JsonArray tools, ToolBehavior b, bool googleSearch) const {
    if (googleSearch) tools.add<JsonObject>()["googleSearch"].to<JsonObject>();
    JsonArray d = tools.add<JsonObject>()["functionDeclarations"].to<JsonArray>();

    // ---- Long-term memory ----
    static const Param save[] = {
        {"content", "STRING", "One self-contained sentence, third person, user's language (e.g. 'ผู้ใช้แพ้กุ้ง').", true, nullptr},
        {"category", "STRING", nullptr, false, kCategories},
        {"importance", "INTEGER", "1-5 (5 = essential: name, allergies, family).", false, nullptr},
    };
    declare(d, b, "save_memory",
            "Save a durable fact about the user (personal details, preferences, people, pets, plans, "
            "things to remember) the user clearly said. Call as soon as you learn it.", save, 3);
    static const Param rel[] = {
        {"subject", "STRING", nullptr, true, nullptr},
        {"relation", "STRING", "Short verb phrase, e.g. 'มีสุนัขชื่อ'.", true, nullptr},
        {"object", "STRING", nullptr, true, nullptr},
    };
    declare(d, b, "save_relation", "Save a relationship between two entities in the knowledge graph.", rel, 3);
    static const Param recall[] = {
        {"query", "STRING", "Short keywords incl. names; retry with synonyms if nothing found.", true, nullptr},
        {"limit", "INTEGER", "1-10, default 5.", false, nullptr},
    };
    declare(d, b, "recall_memory",
            "Search long-term memory. Call BEFORE answering anything about the user, their past, "
            "people, preferences or plans.", recall, 2);
    static const Param forget[] = {{"memory_id", "INTEGER", "Id from recall_memory.", true, nullptr}};
    declare(d, b, "forget_memory", "Delete a wrong or unwanted memory.", forget, 1);
    static const Param conv[] = {
        {"query", "STRING", "Keywords; empty = latest exchanges.", false, nullptr},
        {"days_back", "INTEGER", "1-90, default 7.", false, nullptr},
    };
    declare(d, b, "recall_conversation", "Search the log of earlier conversations.", conv, 2);

    // ---- Time ----
    static const Param timer[] = {
        {"minutes", "INTEGER", nullptr, false, nullptr},
        {"seconds", "INTEGER", nullptr, false, nullptr},
        {"label", "STRING", "What the timer is for.", false, nullptr},
    };
    declare(d, b, "set_timer", "Start a countdown timer (rings on the device).", timer, 3);
    static const Param remind[] = {
        {"when", "STRING", "'YYYY-MM-DD HH:MM', 'HH:MM' (next occurrence) or 'tomorrow HH:MM', local time.", true, nullptr},
        {"label", "STRING", "What to remind.", true, nullptr},
        {"repeat", "STRING", nullptr, false, kRepeats},
    };
    declare(d, b, "set_reminder", "Create a reminder/alarm at a clock time. Get the date first if unsure.", remind, 3);
    declare(d, b, "list_reminders", "List pending timers and reminders.");
    static const Param cancel[] = {
        {"id", "INTEGER", nullptr, false, nullptr},
        {"all", "BOOLEAN", "Cancel everything.", false, nullptr},
    };
    declare(d, b, "cancel_reminder", "Cancel a timer/reminder.", cancel, 2);

    // ---- Tasks ----
    static const Param addt[] = {
        {"title", "STRING", nullptr, true, nullptr},
        {"priority", "INTEGER", "1 high, 2 normal, 3 low.", false, nullptr},
        {"due", "STRING", "Optional 'YYYY-MM-DD HH:MM'.", false, nullptr},
    };
    declare(d, b, "add_task", "Add an item to the user's to-do list.", addt, 3);
    static const Param listt[] = {{"include_done", "BOOLEAN", nullptr, false, nullptr}};
    declare(d, b, "list_tasks", "List to-do items.", listt, 1);
    static const Param upd[] = {
        {"id", "INTEGER", nullptr, true, nullptr},
        {"action", "STRING", nullptr, true, kTaskActions},
        {"title", "STRING", "New title for rename.", false, nullptr},
    };
    declare(d, b, "update_task", "Complete, reopen, delete or rename a to-do item.", upd, 3);

    // ---- Assistant & device ----
    declare(d, b, "get_device_status",
            "Current date/time, battery, Wi-Fi, volume, assistant settings, memory/reminder/task counts.");
    static const Param expr[] = {{"emotion", "STRING", nullptr, true, emotionNames()}};
    declare(d, b, "set_expression", "Show an emotion with your eyes on the screen while you talk.", expr, 1);
    static const Param cfg[] = {
        {"profile", "STRING", "Preset id from list_assistant_options.", false, nullptr},
        {"voice", "STRING", "Gemini voice name.", false, nullptr},
        {"personality", "STRING", "Personality id.", false, nullptr},
        {"gender", "STRING", nullptr, false, kGenders},
        {"agent_name", "STRING", "Your new name.", false, nullptr},
        {"user_name", "STRING", "What to call the user.", false, nullptr},
        {"eye_color", "STRING", nullptr, false, nullptr},
    };
    declare(d, b, "configure_assistant",
            "Change your profile, voice, personality, gender, name, the user's name or eye color. "
            "Voice/personality changes apply after this reply.", cfg, 7);
    static const Param opts[] = {{"kind", "STRING", nullptr, true, kOptionKinds}};
    declare(d, b, "list_assistant_options", "List available profiles, voices, personalities or eye colors.", opts, 1);
    static const Param vol[] = {
        {"level", "INTEGER", "Absolute 0-100.", false, nullptr},
        {"change", "INTEGER", "Relative, e.g. +10 louder / -10 quieter.", false, nullptr},
    };
    declare(d, b, "set_volume", "Speaker volume: set a level or turn it up/down.", vol, 2);
    static const Param bri[] = {
        {"level", "INTEGER", "Absolute 5-100.", false, nullptr},
        {"change", "INTEGER", "Relative, e.g. +15 / -15.", false, nullptr},
    };
    declare(d, b, "set_brightness", "Screen brightness: set a level or make it brighter/dimmer.", bri, 2);
    static const Param dev[] = {
        {"mic_sensitivity", "INTEGER", "0-7, default 5.", false, nullptr},
        {"mic_change", "INTEGER", "+1 / -1.", false, nullptr},
        {"sleep_minutes", "INTEGER", "Silence before sleeping, 0 = never.", false, nullptr},
        {"clock_minutes", "INTEGER", "Asleep this long -> dim clock, 0 = never.", false, nullptr},
        {"dim_brightness", "INTEGER", "Clock-mode brightness %, 1-60.", false, nullptr},
    };
    declare(d, b, "configure_device",
            "Mic sensitivity, auto-sleep time, clock-mode delay and dim level.", dev, 5);
    declare(d, b, "rest_screen",
            "Rest the screen now (พักหน้าจอ / clock mode): dim clock after this reply until the user speaks or taps.");
    static const Param led[] = {{"color", "STRING", nullptr, true, kLedColors}};
    declare(d, b, "control_led", "Set the RGB LED ('auto' = status colors).", led, 1);
    declare(d, b, "go_to_sleep", "Stop listening and sleep after this reply (user said goodbye / good night).");
    static const Param radio[] = {
        {"station", "STRING", "Number, name or genre the user asked for. Omit = the last station played.", false, nullptr},
    };
    declare(d, b, "play_radio",
            "Play a station from the radio list (in your instructions). Name it briefly, then stay quiet.", radio, 1);
    declare(d, b, "stop_radio", "Stop the Internet radio.");
    declare(d, b, "mute_microphone",
            "Mic OFF after this reply. Only the screen mic button turns it back on: tell the user.");

    // ---- Live data with on-screen graphics (run by webtools::WebTools, answered asynchronously) ----
    static const Param weather[] = {
        {"location", "STRING", "Thai province / city / district in Thai, e.g. 'เชียงใหม่', 'หาดใหญ่'. Empty = Bangkok.", false, nullptr},
        {"location_en", "STRING", "The same place in English (fallback for the geocoder), e.g. 'Ubon Ratchathani'.", false, nullptr},
    };
    declare(d, b, "get_weather",
            "Current weather, PM2.5 air quality and 5-day forecast for a place in Thailand (or anywhere). "
            "Shows a weather card on the screen. ALWAYS use this for weather / rain / heat / PM2.5 questions.",
            weather, 2);
    declare(d, b, "get_oil_price",
            "Today's Thai retail fuel prices (Bangchak: diesel, gasohol 91/95/E20/E85, premium) incl. announced "
            "changes for tomorrow. Shows a price table on the screen. Use for น้ำมัน / ราคาน้ำมัน questions.");
    static const Param quote[] = {
        {"symbols", "STRING",
         "1-6 comma-separated TradingView symbols, e.g. 'SET:PTT', 'SET:SET' (Thai index), 'NASDAQ:AAPL', "
         "'OANDA:XAUUSD' (gold), 'BINANCE:BTCUSDT', 'FX_IDC:USDTHB', 'NYMEX:CL1!' (WTI), 'ICEEUR:BRN1!' (Brent), 'TVC:SILVER'. "
         "Thai words also work: ทองคำ, บิทคอยน์, ดัชนีหุ้นไทย, ดอลลาร์, น้ำมันดิบ.", true, nullptr},
    };
    declare(d, b, "get_market_quote",
            "Live market data from TradingView: price, change, day range, 1W/1M/3M/1Y performance, RSI and "
            "technical rating. Shows a chart card on the screen. Use for stocks, indices, gold, crypto, FX, crude oil.",
            quote, 1);
    static const Param fx[] = {
        {"amount", "NUMBER", nullptr, true, nullptr},
        {"from", "STRING", "ISO currency code, e.g. USD", true, nullptr},
        {"to", "STRING", "ISO currency code, e.g. THB", true, nullptr},
    };
    declare(d, b, "convert_currency", "Convert money with the live exchange rate (TradingView). Shows it on the screen.", fx, 3);
    static const Param news[] = {
        {"topic", "STRING", "Search words in Thai or English; empty = top Thai headlines.", false, nullptr},
    };
    declare(d, b, "get_news",
            "Latest news headlines (Google News, Thai edition), optionally about a topic. Shows the list on the screen.",
            news, 1);

    // ---- Utilities (ported from the PersonalAIBot app; computed on the device or fetched, shown as a card) ----
    static const Param calc[] = {
        {"expression", "STRING", "Math, e.g. '(1500*12)*7%', 'sqrt(2)', 'sin(30)' (degrees), '2^10'.", true, nullptr},
    };
    declare(d, b, "calculate", "Exact calculator. Use for ANY arithmetic, percentages, VAT, interest, splitting bills.", calc, 1);
    static const Param units[] = {
        {"value", "NUMBER", nullptr, true, nullptr},
        {"from", "STRING", "Unit, e.g. km, mile, kg, lb, ไร่, ตารางวา, บาททอง, C, F, L, gallon.", true, nullptr},
        {"to", "STRING", nullptr, true, nullptr},
    };
    declare(d, b, "convert_units", "Convert length, area (incl. Thai ไร่/งาน/ตารางวา), weight (incl. บาททอง), volume, temperature, speed.", units, 3);
    static const Param wt[] = {{"city", "STRING", "City or country in Thai or English, e.g. 'โตเกียว', 'London'.", true, nullptr}};
    declare(d, b, "get_world_time", "Current local time in another city (DST aware) and the difference from Thailand.", wt, 1);
    declare(d, b, "get_location", "Approximate location of the device from its internet IP (no GPS).");
    static const Param know[] = {{"query", "STRING", "Topic / person / place / thing.", true, nullptr}};
    declare(d, b, "search_knowledge",
            "Encyclopedia summary from Wikipedia (Thai, then English). Use for 'what/who is ...' facts, history, definitions.",
            know, 1);
    static const char* const kNoteActions[] = {"add", "list", "search", "read", "delete", nullptr};
    static const Param notes[] = {
        {"action", "STRING", nullptr, true, kNoteActions},
        {"title", "STRING", nullptr, false, nullptr},
        {"content", "STRING", "Note text (add).", false, nullptr},
        {"query", "STRING", "Words to find (search / read / delete).", false, nullptr},
        {"id", "INTEGER", nullptr, false, nullptr},
    };
    declare(d, b, "notes",
            "Personal notebook on the device: save longer notes (recipes, lists, ideas) and read them back. "
            "Facts about the user go to save_memory instead.", notes, 5);

    // ---- Finance pack ----
    if (_finance) {
        declare(d, b, "market_overview",
                "Snapshot of key markets: SET index, S&P 500, Nasdaq, gold, WTI oil, bitcoin, USD/THB + crypto Fear&Greed. "
                "Shows a card. Use for 'ตลาดวันนี้เป็นยังไง'.");
        static const char* const kTf[] = {"15m", "1h", "4h", "1d", "1w", "multi", nullptr};
        static const Param ta[] = {
            {"symbol", "STRING", "Same forms as get_market_quote.", true, nullptr},
            {"timeframe", "STRING", "'multi' = rating on every timeframe. Default 1d.", false, kTf},
            {"fundamentals", "BOOLEAN", "Add P/E, P/BV, dividend yield, market cap (stocks).", false, nullptr},
        };
        declare(d, b, "market_analysis",
                "Technical analysis from TradingView (RSI, MACD, Stochastic, EMA20/50/200, ADX, buy/sell rating) "
                "with a gauge card. Say it is not investment advice.", ta, 3);
        static const char* const kMarkets[] = {"thailand", "america", "crypto", nullptr};
        static const char* const kScans[] = {"gainers", "losers", "volume", "oversold", "overbought", nullptr};
        static const Param scan[] = {
            {"market", "STRING", nullptr, true, kMarkets},
            {"type", "STRING", nullptr, true, kScans},
            {"limit", "INTEGER", "1-8, default 6.", false, nullptr},
        };
        declare(d, b, "market_scan", "Stock / crypto screener: top gainers, losers, unusual volume, RSI oversold/overbought.", scan, 3);
        static const char* const kRanges[] = {"today", "tomorrow", "week", nullptr};
        static const char* const kImpact[] = {"high", "medium", nullptr};
        static const Param cal[] = {
            {"range", "STRING", nullptr, false, kRanges},
            {"impact", "STRING", "high only (default) or also medium.", false, kImpact},
        };
        declare(d, b, "economic_calendar",
                "Economic news calendar (Fed, CPI, NFP, GDP...) in Thai time from ForexFactory, with forecast/previous.", cal, 2);
        static const Param ps[] = {
            {"balance", "NUMBER", "Account size.", true, nullptr},
            {"risk_percent", "NUMBER", "Default 1.", false, nullptr},
            {"entry", "NUMBER", nullptr, true, nullptr},
            {"stop_loss", "NUMBER", nullptr, true, nullptr},
            {"symbol", "STRING", "e.g. XAUUSD, EURUSD, PTT (sets the contract size).", false, nullptr},
            {"contract_size", "NUMBER", "Units per lot if not standard.", false, nullptr},
        };
        declare(d, b, "position_size", "Risk-based position size (lots / shares) for a trade.", ps, 6);
        static const char* const kAlertActions[] = {"add", "list", "remove", nullptr};
        static const Param alert[] = {
            {"action", "STRING", nullptr, true, kAlertActions},
            {"symbol", "STRING", "Same forms as get_market_quote.", false, nullptr},
            {"above", "NUMBER", "Alert when price >= this.", false, nullptr},
            {"below", "NUMBER", "Alert when price <= this.", false, nullptr},
            {"id", "INTEGER", "remove: alert id (none = all).", false, nullptr},
        };
        declare(d, b, "price_alert",
                "Price alerts checked by the device every 2 minutes; it speaks up when one triggers.", alert, 5);
    }
}

// ============================================================
// Dispatch
// ============================================================

bool AgentTools::execute(const char* name, JsonObjectConst args, JsonObject response) {
    JsonObject result = response["result"].to<JsonObject>();
    const char* error = "unknown function";
    bool ok = false;
    if (name) {
        for (const Entry* e = kHandlers; e->name; e++) {
            if (strcmp(e->name, name) == 0) {
                error = nullptr;
                ok = (this->*(e->fn))(args, result, error);
                break;
            }
        }
    }
    if (!ok) {
        response.remove("result");
        response["error"] = error ? error : "failed";
    }
    return ok;
}

// ============================================================
// Memory
// ============================================================

bool AgentTools::saveMemory(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* content = a["content"] | "";
    if (!*content) return (e = "content is required"), false;
    bool merged = false;
    const uint32_t id = _memory.addFact(content, ltm::parseCategory(a["category"] | "other"),
                                        (uint8_t)clampInt(a["importance"] | 3, 1, 5), _device.nowEpoch(), &merged);
    if (!id) return (e = "content is empty or not searchable"), false;
    _memoryWrites++;
    r["status"] = merged ? "updated_existing_memory" : "saved";
    r["memory_id"] = id;
    return true;
}

bool AgentTools::saveRelation(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* s = a["subject"] | "";
    const char* p = a["relation"] | "";
    const char* o = a["object"] | "";
    if (!*s || !*p || !*o) return (e = "subject, relation and object are required"), false;
    bool merged = false;
    const uint32_t id = _memory.addRelation(s, p, o, ltm::parseCategory(a["category"] | "person"), 3,
                                            _device.nowEpoch(), &merged);
    if (!id) return (e = "invalid relation"), false;
    _memoryWrites++;
    r["status"] = merged ? "already_known" : "saved";
    r["memory_id"] = id;
    return true;
}

bool AgentTools::recallMemory(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* query = a["query"] | "";
    if (!*query) return (e = "query is required"), false;
    std::vector<ltm::Hit> hits;
    _memory.search(query, (size_t)clampInt(a["limit"] | 5, 1, 10), hits, 6);
    r["count"] = (uint32_t)hits.size();
    if (hits.empty()) {
        r["note"] = "Nothing found. Try synonyms; otherwise say you don't remember.";
        return true;
    }
    JsonArray arr = r["memories"].to<JsonArray>();
    for (const ltm::Hit& h : hits) writeRecord(arr.add<JsonObject>(), *h.rec, _utcOffset, h.viaGraph);
    return true;
}

bool AgentTools::forgetMemory(JsonObjectConst a, JsonObject r, const char*& e) {
    const uint32_t id = a["memory_id"] | 0u;
    const ltm::Record* rec = id ? _memory.find(id) : nullptr;
    if (!rec) return (e = "no memory with that id (use recall_memory)"), false;
    r["deleted_content"] = rec->text.c_str();
    r["memory_id"] = id;
    _memory.remove(id);
    _memoryWrites++;
    r["status"] = "deleted";
    return true;
}

bool AgentTools::recallConversation(JsonObjectConst a, JsonObject r, const char*&) {
    std::vector<ltm::Turn> turns;
    const int daysBack = clampInt(a["days_back"] | 7, 1, 90);
    int searched = 0;
    _episodes.search(a["query"] | "", daysBack, 6, _device.nowEpoch(), turns, &searched);
    r["count"] = (uint32_t)turns.size();
    if (searched && searched < daysBack) {  // the read budget stopped it (a long history)
        r["searched_days"] = searched;
        r["note"] = "Only the most recent days were searched; ask for a narrower topic if needed.";
    }
    JsonArray arr = r["turns"].to<JsonArray>();
    for (const ltm::Turn& t : turns) {
        JsonObject o = arr.add<JsonObject>();
        writeWhen(o, "time", t.ts, _utcOffset);
        o["speaker"] = t.role == 'a' ? "assistant" : "user";
        o["text"] = t.text.c_str();
    }
    return true;
}

// ============================================================
// Timers, reminders
// ============================================================

bool AgentTools::setTimer(JsonObjectConst a, JsonObject r, const char*& e) {
    const uint32_t now = _device.nowEpoch();
    if (!now) return (e = "clock not synced yet"), false;
    const int secs = clampInt(a["minutes"] | 0, 0, 7 * 24 * 60) * 60 + clampInt(a["seconds"] | 0, 0, 86400);
    if (secs <= 0) return (e = "give minutes and/or seconds"), false;
    const uint32_t id = _scheduler.addTimer(now, (uint32_t)secs, a["label"] | "");
    if (!id) return (e = "cannot add timer (limit reached?)"), false;
    r["id"] = id;
    r["rings_in_seconds"] = secs;
    writeWhen(r, "rings_at", now + (uint32_t)secs, _utcOffset);
    return true;
}

bool AgentTools::setReminder(JsonObjectConst a, JsonObject r, const char*& e) {
    const uint32_t now = _device.nowEpoch();
    if (!now) return (e = "clock not synced yet"), false;
    const uint32_t due = sched::parseWhen(a["when"] | "", now, _utcOffset);
    if (!due) return (e = "invalid or past time; use 'YYYY-MM-DD HH:MM' or 'HH:MM'"), false;
    const sched::Repeat rep = sched::parseRepeat(a["repeat"] | "none");
    const uint32_t id = _scheduler.addReminder(due, rep, a["label"] | "");
    if (!id) return (e = "cannot add reminder (limit reached?)"), false;
    r["id"] = id;
    writeWhen(r, "due", due, _utcOffset);
    r["repeat"] = sched::repeatName(rep);
    return true;
}

bool AgentTools::listReminders(JsonObjectConst, JsonObject r, const char*&) {
    const uint32_t now = _device.nowEpoch();
    JsonArray arr = r["items"].to<JsonArray>();
    for (const sched::Reminder& it : _scheduler.items()) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = it.id;
        o["type"] = it.timer ? "timer" : "reminder";
        writeWhen(o, "due", it.due, _utcOffset);
        if (now && it.due > now) o["in_minutes"] = (it.due - now + 59) / 60;
        if (it.repeat != sched::Repeat::None) o["repeat"] = sched::repeatName(it.repeat);
        o["label"] = it.label.c_str();
    }
    r["count"] = (uint32_t)arr.size();
    return true;
}

bool AgentTools::cancelReminder(JsonObjectConst a, JsonObject r, const char*& e) {
    if (a["all"] | false) {
        r["cancelled"] = (uint32_t)_scheduler.cancelAll();
        return true;
    }
    const uint32_t id = a["id"] | 0u;
    if (!id || !_scheduler.cancel(id)) return (e = "no timer/reminder with that id"), false;
    r["cancelled"] = 1;
    return true;
}

// ============================================================
// Tasks
// ============================================================

bool AgentTools::addTask(JsonObjectConst a, JsonObject r, const char*& e) {
    const uint32_t now = _device.nowEpoch();
    uint32_t due = 0;
    const char* dueStr = a["due"] | "";
    if (*dueStr) {
        due = sched::parseWhen(dueStr, now, _utcOffset);
        if (!due) return (e = "invalid due date; use 'YYYY-MM-DD HH:MM'"), false;
    }
    const uint32_t id = _tasks.add(a["title"] | "", (uint8_t)clampInt(a["priority"] | 2, 1, 3), due, now);
    if (!id) return (e = "title is required (or the list is full)"), false;
    r["id"] = id;
    r["open_tasks"] = (uint32_t)_tasks.openCount();
    return true;
}

bool AgentTools::listTasks(JsonObjectConst a, JsonObject r, const char*&) {
    const auto items = _tasks.list(a["include_done"] | false);
    JsonArray arr = r["tasks"].to<JsonArray>();
    size_t n = 0;
    for (const todo::Task* t : items) {
        if (++n > 25) break;
        JsonObject o = arr.add<JsonObject>();
        o["id"] = t->id;
        o["title"] = t->title.c_str();
        if (t->done) o["done"] = true;
        if (t->priority != 2) o["priority"] = t->priority;
        if (t->due) writeWhen(o, "due", t->due, _utcOffset);
    }
    r["open_tasks"] = (uint32_t)_tasks.openCount();
    return true;
}

bool AgentTools::updateTask(JsonObjectConst a, JsonObject r, const char*& e) {
    const uint32_t id = a["id"] | 0u;
    const char* action = a["action"] | "";
    const todo::Task* t = _tasks.find(id);
    if (!t) return (e = "no task with that id (use list_tasks)"), false;
    r["title"] = t->title.c_str();  // copied before any change
    bool ok = false;
    if (!strcmp(action, "done")) ok = _tasks.setDone(id, true, _device.nowEpoch());
    else if (!strcmp(action, "undone")) ok = _tasks.setDone(id, false, 0);
    else if (!strcmp(action, "delete")) ok = _tasks.remove(id);
    else if (!strcmp(action, "rename")) ok = _tasks.rename(id, a["title"] | "");
    else return (e = "unknown action"), false;
    if (!ok) return (e = "update failed"), false;
    r["status"] = action;
    r["open_tasks"] = (uint32_t)_tasks.openCount();
    return true;
}

// ============================================================
// Assistant & device
// ============================================================

bool AgentTools::deviceStatus(JsonObjectConst, JsonObject r, const char*&) {
    const uint32_t now = _device.nowEpoch();
    if (now) {
        writeWhen(r, "datetime", now, _utcOffset);
        r["weekday"] = timefmt::thaiWeekday(timefmt::toCivil(now, _utcOffset).weekday);
        r["timezone"] = "Asia/Bangkok (UTC+7)";
    } else {
        r["datetime"] = "unknown (clock not synced yet)";
    }
    const ltm::Stats s = _memory.stats();
    JsonObject mem = r["memory"].to<JsonObject>();
    mem["facts"] = (uint32_t)s.facts;
    mem["relations"] = (uint32_t)s.relations;
    r["pending_reminders"] = (uint32_t)_scheduler.items().size();
    r["open_tasks"] = (uint32_t)_tasks.openCount();
    _device.describeAssistant(r["assistant"].to<JsonObject>());
    _device.fillStatus(r);
    return true;
}

bool AgentTools::setExpression(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* emo = a["emotion"] | "";
    Emotion parsed;
    if (!parseEmotion(emo, parsed)) return (e = "unknown emotion"), false;
    _device.setExpression(emo);
    r["shown"] = emo;
    return true;
}

bool AgentTools::configureAssistant(JsonObjectConst a, JsonObject r, const char*& e) {
    AssistantChange c;
    if (!a["profile"].isNull()) {
        const persona::Profile* p = persona::findProfile(a["profile"] | "");
        if (!p) return (e = "unknown profile (use list_assistant_options kind=profiles)"), false;
        c.profile = p->id;
    }
    if (!a["voice"].isNull()) {
        const persona::VoiceInfo* v = persona::findVoice(a["voice"] | "");
        if (!v) return (e = "unknown voice (use list_assistant_options kind=voices)"), false;
        c.voice = v->name;
    }
    if (!a["personality"].isNull()) {
        c.personality = persona::findPersonality(a["personality"] | "");
        if (c.personality < 0) return (e = "unknown personality"), false;
    }
    if (!a["gender"].isNull()) c.gender = persona::parseGender(a["gender"] | "");
    if (!a["eye_color"].isNull()) {
        c.eyeColor = persona::findEyeColor(a["eye_color"] | "");
        if (c.eyeColor < 0) return (e = "unknown eye color"), false;
    }
    const char* an = a["agent_name"] | "";
    const char* un = a["user_name"] | "";
    if (*an) {
        if (strlen(an) > 45) return (e = "agent_name too long"), false;
        c.agentName = an;
    }
    if (*un) {
        if (strlen(un) > 45) return (e = "user_name too long"), false;
        c.userName = un;
    }
    if (!c.any()) return (e = "nothing to change"), false;
    if (!_device.applyAssistantChange(c, r)) return (e = "could not apply settings"), false;
    return true;
}

bool AgentTools::listAssistantOptions(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* kind = a["kind"] | "";
    JsonArray arr = r["options"].to<JsonArray>();
    if (!strcmp(kind, "profiles")) {
        for (size_t i = 0; i < persona::kProfileCount; i++) {
            const persona::Profile& p = persona::kProfiles[i];
            JsonObject o = arr.add<JsonObject>();
            o["id"] = p.id;
            o["name"] = p.agentName;
            o["voice"] = p.voice;
            o["gender"] = persona::genderId(p.gender);
            o["about"] = p.summaryTh;
        }
    } else if (!strcmp(kind, "voices")) {
        for (size_t i = 0; i < persona::kVoiceCount; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = persona::kVoices[i].name;
            o["style"] = persona::kVoices[i].styleEn;
            o["gender"] = persona::genderId(persona::kVoices[i].gender);
        }
    } else if (!strcmp(kind, "personalities")) {
        for (size_t i = 0; i < persona::kPersonalityCount; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["id"] = persona::kPersonalities[i].id;
            o["name"] = persona::kPersonalities[i].nameTh;
        }
    } else if (!strcmp(kind, "eye_colors")) {
        for (size_t i = 0; i < persona::kEyeColorCount; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["id"] = persona::kEyeColors[i].id;
            o["name"] = persona::kEyeColors[i].nameTh;
        }
    } else {
        return (e = "kind must be profiles, voices, personalities or eye_colors"), false;
    }
    return true;
}

// "level" = absolute, "change" = relative (+15 louder / -15 quieter); returns the new level
bool AgentTools::setVolume(JsonObjectConst a, JsonObject r, const char*& e) {
    if (a["level"].isNull() && a["change"].isNull()) return (e = "level or change is required"), false;
    const int before = _device.volume();
    const int target = a["level"].isNull() ? before + (a["change"] | 0) : (a["level"] | 0);
    r["previous"] = before;
    r["volume"] = _device.setVolume(clampInt(target, 0, 100));
    return true;
}

bool AgentTools::setBrightness(JsonObjectConst a, JsonObject r, const char*& e) {
    if (a["level"].isNull() && a["change"].isNull()) return (e = "level or change is required"), false;
    const int before = _device.brightness();
    const int target = a["level"].isNull() ? before + (a["change"] | 0) : (a["level"] | 0);
    r["previous"] = before;
    r["brightness"] = _device.setBrightness(clampInt(target, 5, 100));
    return true;
}

bool AgentTools::configureDevice(JsonObjectConst a, JsonObject r, const char*& e) {
    DeviceChange c;
    if (!a["mic_sensitivity"].isNull()) c.micGain = clampInt(a["mic_sensitivity"] | 5, 0, 7);
    else if (!a["mic_change"].isNull()) c.micGain = clampInt(_device.micGain() + (a["mic_change"] | 0), 0, 7);
    if (!a["sleep_minutes"].isNull()) c.sleepMinutes = clampInt(a["sleep_minutes"] | 3, 0, 120);
    if (!a["clock_minutes"].isNull()) c.clockMinutes = clampInt(a["clock_minutes"] | 5, 0, 120);
    if (!a["dim_brightness"].isNull()) c.dimBrightness = clampInt(a["dim_brightness"] | 8, 1, 60);
    if (!c.any()) return (e = "nothing to change"), false;
    _device.configureDevice(c, r);
    return true;
}

bool AgentTools::controlLed(JsonObjectConst a, JsonObject r, const char*& e) {
    const char* color = a["color"] | "";
    bool known = false;
    for (const char* const* c = kLedColors; *c; c++) known |= strcmp(*c, color) == 0;
    if (!known) return (e = "unsupported color"), false;
    if (!_device.setLedColor(color)) return (e = "LED not available"), false;
    r["color"] = color;
    return true;
}

bool AgentTools::playRadio(JsonObjectConst a, JsonObject r, const char*& e) {
    if (const char* err = _device.playRadio(a["station"] | "", r)) return (e = err), false;
    return true;
}

bool AgentTools::stopRadio(JsonObjectConst, JsonObject r, const char*&) {
    r["was_playing"] = _device.stopRadio();
    r["status"] = "radio off";
    return true;
}

bool AgentTools::restScreen(JsonObjectConst, JsonObject r, const char*&) {
    DeviceChange c;
    c.powerSaveNow = true;
    _device.configureDevice(c, r);
    r["status"] = "after this reply: dim clock face (screen rest); speaking or a tap wakes you with full brightness";
    return true;
}

bool AgentTools::muteMicrophone(JsonObjectConst, JsonObject r, const char*&) {
    _device.requestMicMute();
    r["status"] = "microphone turns off after this reply; only the mic button on the screen turns it back on";
    return true;
}

bool AgentTools::goToSleep(JsonObjectConst, JsonObject r, const char*&) {
    _device.requestSleep();
    r["status"] = "will sleep after this reply; tap the screen or speak to wake";
    return true;
}
