/**
 * @file ObdLog.h
 * @brief The OBD log format (one text line per record, on the SD card) and its replay - host tested, no Arduino (T115)
 * @see wiki/16-Vehicle-Dash.md
 *
 * A drive recorded in the car is brought back to be read and replayed: through the very same Elm327 driver
 * (the adapter's recorded answers, matched by command and time - a fixed or changed driver meets the real car
 * again), or as the recorded values alone (sources without an adapter trace: K-Line, text lines).
 *
 *   #OBDLOG 1 fw=4.5.0 source=obd_ble start=2026-10-27T15:04:05+07:00
 *   <ms since the start> TAB <tag> TAB <text>
 *     >  command sent to the adapter        010C
 *     <  its whole reply (lines split by |)  SEARCHING...|4100BE3EB811
 *     !  no reply in time (the command is retried or the adapter restarted)
 *     S  status                              LIVE / NO ECU / BT CONNECT ...
 *     E  event                               Bluetooth on, scan results, baud rate, errors, notes
 *     D  decoded values, once a second       rpm=812 speed_kmh=0 coolant_c=57 battery_v=14.1
 *     C  diagnosis when it changes           mil=1 stored=P0420 pending=P0171 ready=5/6 proto=6 vin=...
 *     H  health when it changes              warning: fuel mixture lean, bank 1 (+13.2) ...
 *     F  a Honda K-Line table (hex)
 *   Text is escaped: \\ \t \n \r \| and \xHH for other control bytes (UTF-8 kept).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../util/PsramAlloc.h"
#include "VehicleData.h"

namespace vehicle {
namespace obdlog {

constexpr uint32_t kVersion = 1;

/** `text` escaped into `out` (always terminated). Returns the length. In a `<` record '\n' (a line break of the
 *  reply) becomes an unescaped '|'. */
size_t escape(const char* text, char* out, size_t cap, bool replyLines = false);
/** The reverse; `|` becomes '\n' when replyLines. Returns the length. */
size_t unescape(const char* text, size_t n, char* out, size_t cap, bool replyLines = false);

/** "<t>\t<tag>\t<escaped text>\n" into `out`. Returns the length (0 if it does not fit). */
size_t formatRecord(char* out, size_t cap, uint32_t t, char tag, const char* text);

struct Record {
    uint32_t t = 0;
    char tag = 0;
    const char* text = nullptr;  // escaped, inside the parsed buffer
    size_t len = 0;
};
/** One line (without its '\n'). False for the header, comments and anything malformed. */
bool parseRecord(const char* line, size_t n, Record& r);

/** "rpm=812 speed_kmh=0 ..." of the fresh channels (decimals by channel). Returns the length. */
size_t formatValues(const Snapshot& s, uint32_t now, char* out, size_t cap);
/** The reverse into `out` (set at `now`, fresh for `ttlMs`). Returns how many channels were set. */
int parseValues(const char* text, size_t n, Snapshot& out, uint32_t now, uint32_t ttlMs);

/** A recorded log, indexed for replay. The text stays where it was loaded (not copied). */
class Replay {
public:
    /** Index `data` (the whole file). False if it holds no record. */
    bool load(const char* data, size_t n);

    uint32_t duration() const { return _duration; }
    /** Where a replay starts: the first command to the adapter (else the first record). */
    uint32_t start() const { return _first; }
    bool hasAdapterTrace() const { return !_cmds.empty(); }
    bool hasValues() const { return !_values.empty(); }
    size_t records() const { return _records; }
    /** Header fields ("source", "start", "fw"); "" if missing. */
    const char* header(const char* key, char* out, size_t cap) const;

    struct Answer {
        bool known = false;    // the command is in the log
        bool silent = false;   // it got no reply then (the driver timed out)
        uint32_t latencyMs = 0;
        char text[400] = "";   // the reply's lines, '\n'-separated
    };
    /** What the adapter answered to `cmd` at log time `t`: the latest recording of it at or before `t` (else the
     *  first one). A command never recorded: "OK" for AT commands, "NO DATA" otherwise. */
    Answer answer(const char* cmd, uint32_t t) const;
    /** The adapter's bytes for an answer: lines + "\r", then "\r>" (what Elm327::feed expects). Returns the length. */
    static size_t stream(const Answer& a, char* out, size_t cap);

    /** The recorded values at log time `t` (the latest D record at or before it) into `out` at `now`. */
    bool values(uint32_t t, Snapshot& out, uint32_t now) const;
    /** Events / status / diagnosis records between two log times (a, b], one at a time (for the console). */
    bool nextNote(uint32_t a, uint32_t b, size_t& cursor, Record& r) const;

private:
    struct Entry {
        uint32_t t = 0;        // command sent
        uint32_t reply = 0;    // reply time
        uint32_t off = 0, len = 0;  // the escaped reply text in _data
        bool silent = false;
    };
    struct Command {
        PsString cmd;
        PsVector<Entry> entries;
    };
    const char* _data = nullptr;
    size_t _n = 0;
    uint32_t _first = 0, _duration = 0;
    size_t _records = 0;
    PsVector<Command> _cmds;
    PsVector<Record> _values;  // D records
    PsVector<Record> _notes;   // S / E / C / H records
    size_t _headerLen = 0;

    Command* find(const char* cmd);
    const Command* find(const char* cmd) const;
};

/**
 * An ELM327 made of a recording: takes the driver's bytes, answers each command with what the car said at that
 * moment of the drive, after the recorded latency. The log plays in real time x `speed` and starts over at its end.
 * The board's replay source wraps it in a VehicleLink; the host tests and the replay tool use it directly.
 */
class ReplayAdapter {
public:
    void begin(const Replay* r, uint32_t now, float speed = 1.0f);
    /** Where the log is at `now`. */
    uint32_t logTime(uint32_t now) const;
    /** How many times it went round. */
    uint32_t loops(uint32_t now) const;
    /** Bytes from the driver; a command ends in '\r'. */
    void write(const char* data, size_t n, uint32_t now);
    /** The answer, once its latency has passed. */
    size_t read(char* out, size_t cap, uint32_t now);

    uint32_t asked() const { return _asked; }
    uint32_t unknown() const { return _unknown; }  // commands the recording does not have
    uint32_t silent() const { return _silent; }    // answered with silence (timed out when recorded)

private:
    const Replay* _r = nullptr;
    uint32_t _began = 0;
    float _speed = 1.0f;
    char _cmd[24] = "";
    size_t _cmdLen = 0;
    char _out[512] = "";
    size_t _outLen = 0, _outPos = 0;
    uint32_t _due = 0;
    uint32_t _asked = 0, _unknown = 0, _silent = 0;
};

}  // namespace obdlog
}  // namespace vehicle
