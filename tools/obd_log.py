#!/usr/bin/env python3
"""The OBD log of the board's SD card (agent/obdlog, wiki 16 / T115): pull it over USB, summarize it, export the
values, replay it through the firmware's own ELM327 driver on the PC.

  python tools/obd_log.py list    [--port COM3]
  python tools/obd_log.py pull    [latest|all|<name>] [--port COM3] [--out logs/obd]
  python tools/obd_log.py summary <file.log>
  python tools/obd_log.py csv     <file.log> [-o values.csv]
  python tools/obd_log.py replay  <file.log>      (host_tests\\run_tests.bat once first: it builds the replay tool)

The board can also simply lose its SD card for a minute: the files are plain text in agent/obdlog.
Pulling needs pyserial (pip install pyserial); everything else only Python 3.
"""

import argparse
import base64
import collections
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "logs", "obd")


# ---------------------------------------------------------------- the serial console

def open_port(port):
    import serial  # only for list / pull

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False  # opening the port must not reset the board (like ESP32-AI Studio)
    s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def find_port():
    from serial.tools import list_ports

    for p in list_ports.comports():
        if (p.vid or 0) == 0x303A:  # Espressif USB (the ES3C28P's own port)
            return p.device
    sys.exit("no ESP32-S3 board found - give --port COM3")


class Console:
    def __init__(self, port):
        self.s = open_port(port)
        self.buf = b""

    def send(self, line):
        self.s.write((line + "\n").encode("utf-8"))

    def lines(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            chunk = self.s.read(65536)
            if chunk:
                self.buf += chunk
                end = max(end, time.time() + 2.0)  # still talking: keep listening
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                yield line.rstrip(b"\r").decode("utf-8", "replace")

    def tagged(self, cmd, tag, timeout=8.0):
        self.send(cmd)
        for line in self.lines(timeout):
            if line.startswith(tag + " "):
                return json.loads(line[len(tag) + 1:])
        sys.exit("no answer from the board to: " + cmd)


def cmd_list(args):
    c = Console(args.port or find_port())
    d = c.tagged("obdlog list", "OBDLOG")
    print("SD card: %s, logging %s, %s" % (
        "yes" if d.get("sd_card") else "NO", "on" if d.get("enabled") else "off",
        "recording %s (%s KB)" % (d.get("file"), d.get("kb")) if d.get("recording") else "not recording now"))
    for f in d.get("files", []):
        print("  %-48s %8.1f KB" % (f["name"], f["bytes"] / 1024.0))
    if not d.get("files"):
        print("  (no log files yet)")


def fetch_range(c, name, offset, length):
    """One `obdlog get` -> (file name, size, {offset: bytes}, sha256 hex of the range as the board read it)."""
    c.send("obdlog get %s %d %d" % (name, offset, length))
    parts, meta, digest = {}, None, None
    for line in c.lines(15.0):
        if line.startswith("OBDLOGERR"):
            sys.exit(line)
        if line.startswith("OBDLOGFILE "):
            f = line.split()
            meta = (f[1], int(f[2]))
        elif line.startswith("OBDLOGDATA "):
            f = line.split(" ", 2)
            try:
                parts[int(f[1])] = base64.b64decode(f[2])
            except (ValueError, IndexError):
                pass  # a line cut by other output: it shows up as a gap
        elif line.startswith("OBDLOGEND "):
            digest = line.split()[4]
            break
    if not meta:
        sys.exit("no answer to obdlog get %s" % name)
    return meta[0], meta[1], parts, digest


def gaps(parts, start, end):
    pos, out = start, []
    for off in sorted(parts):
        if off > pos:
            out.append((pos, off - pos))
        pos = max(pos, off + len(parts[off]))
    if pos < end:
        out.append((pos, end - pos))
    return out


def pull_one(c, name, out_dir):
    fname, size, parts, digest = fetch_range(c, name, 0, -1)
    for attempt in range(5):  # lines lost on the way: ask for those ranges again
        missing = gaps(parts, 0, size)
        if not missing:
            break
        for off, length in missing:
            _, _, more, _ = fetch_range(c, fname, off, length)
            parts.update(more)
    data = b"".join(parts[o] for o in sorted(parts))[:size]
    if len(data) != size:
        sys.exit("%s: still %d bytes missing after retries" % (fname, size - len(data)))
    if digest and hashlib.sha256(data).hexdigest() != digest:
        sys.exit("%s: sha256 differs from the board's - try again" % fname)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, fname)
    with open(path, "wb") as f:
        f.write(data)
    print("  %s  %.1f KB  sha256 ok" % (path, size / 1024.0))
    return path


def cmd_pull(args):
    c = Console(args.port or find_port())
    d = c.tagged("obdlog list", "OBDLOG")
    files = d.get("files", [])
    if not files:
        sys.exit("no log files on the board")
    if args.name == "all":
        names = [f["name"] for f in files]
    elif args.name in ("", "latest"):
        names = [files[0]["name"]]
    else:
        names = [args.name]
    for n in names:
        local = os.path.join(args.out, n)
        size = next((f["bytes"] for f in files if f["name"] == n), None)
        if args.name == "all" and size is not None and os.path.exists(local) and os.path.getsize(local) == size:
            print("  %s  already here" % local)
            continue
        pull_one(c, n, args.out)


# ---------------------------------------------------------------- reading a log

def unescape(text, reply=False):
    out, i = [], 0
    while i < len(text):
        ch = text[i]
        if ch == "|" and reply:
            out.append("\n")
        elif ch == "\\" and i + 1 < len(text):
            i += 1
            e = text[i]
            if e == "t":
                out.append("\t")
            elif e == "n":
                out.append("\n")
            elif e == "r":
                out.append("\r")
            elif e == "x" and i + 2 < len(text):
                out.append(chr(int(text[i + 1:i + 3], 16)))
                i += 2
            else:
                out.append(e)
        else:
            out.append(ch)
        i += 1
    return "".join(out)


def read_log(path):
    header, records = {}, []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line.startswith("#OBDLOG"):
                for tok in line.split()[1:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        header[k] = v
                continue
            m = re.match(r"^(\d+)\t(.)\t?(.*)$", line)
            if m:
                records.append((int(m.group(1)), m.group(2), m.group(3)))
    return header, records


def values(text):
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = float(v)
            except ValueError:
                pass
    return out


def cmd_summary(args):
    header, recs = read_log(args.file)
    if not recs:
        sys.exit("no records in " + args.file)
    t0, t1 = recs[0][0], recs[-1][0]
    tags = collections.Counter(r[1] for r in recs)
    print("%s" % args.file)
    print("  source %s, firmware %s, start %s, %.1f min, %d records %s" % (
        header.get("source", "?"), header.get("fw", "?"), header.get("start", "?"), (t1 - t0) / 60000.0, len(recs),
        dict(tags)))
    if header.get("dropped_before", "0") != "0":
        print("  ! %s records were dropped before this file (the SD card fell behind)" % header["dropped_before"])

    # the adapter: each command, its replies, latencies, timeouts, NO DATA
    stats = collections.OrderedDict()
    pending = None
    errors = collections.Counter()
    for t, tag, text in recs:
        if tag == ">":
            pending = (unescape(text), t)
            stats.setdefault(pending[0], {"n": 0, "lat": [], "timeouts": 0, "nodata": 0, "q": 0})["n"] += 1
        elif tag == "<" and pending:
            st = stats[pending[0]]
            st["lat"].append(t - pending[1])
            reply = unescape(text, True)
            if "NO DATA" in reply:
                st["nodata"] += 1
            if reply.strip() == "?":
                st["q"] += 1
            for e in ("UNABLE TO CONNECT", "BUS INIT", "BUS ERROR", "CAN ERROR", "STOPPED", "BUFFER FULL", "FB ERROR",
                      "DATA ERROR", "<RX ERROR", "ACT ALERT", "LV RESET"):
                if e in reply:
                    errors[e] += 1
            pending = None
        elif tag == "!" and pending:
            stats[pending[0]]["timeouts"] += 1
            pending = None
    if stats:
        print("\n  adapter commands (count, avg / max reply ms, timeouts, NO DATA, ?):")
        for cmd, st in sorted(stats.items(), key=lambda kv: -kv[1]["n"]):
            lat = st["lat"]
            print("    %-8s %6d  %6.0f / %5d ms  %4d  %5d  %3d" % (
                cmd, st["n"], sum(lat) / len(lat) if lat else 0, max(lat) if lat else 0, st["timeouts"],
                st["nodata"], st["q"]))
        if errors:
            print("  adapter errors: %s" % dict(errors))
        total = sum(st["n"] for st in stats.values())
        print("  %d requests, %.1f per second" % (total, total / max((t1 - t0) / 1000.0, 1)))

    print("\n  status / events / diagnosis:")
    shown = 0
    for t, tag, text in recs:
        if tag in "SECH" and (shown < args.max or tag in "CH"):
            print("    %8.1f s  [%s] %s" % ((t - t0) / 1000.0, tag, unescape(text)))
            shown += 1
    if shown >= args.max:
        print("    ... (more: --max)")

    vals = collections.defaultdict(list)
    for t, tag, text in recs:
        if tag == "D":
            for k, v in values(text).items():
                vals[k].append(v)
    if vals:
        print("\n  values (samples, min / avg / max):")
        for k in sorted(vals):
            v = vals[k]
            print("    %-16s %6d  %10.2f / %10.2f / %10.2f" % (k, len(v), min(v), sum(v) / len(v), max(v)))


def cmd_csv(args):
    header, recs = read_log(args.file)
    rows, keys = [], []
    t0 = recs[0][0] if recs else 0
    for t, tag, text in recs:
        if tag == "D":
            v = values(text)
            for k in v:
                if k not in keys:
                    keys.append(k)
            rows.append(((t - t0) / 1000.0, v))
    out = args.o or os.path.splitext(args.file)[0] + ".csv"
    with open(out, "w", encoding="utf-8") as f:
        f.write("t_s," + ",".join(keys) + "\n")
        for t, v in rows:
            f.write("%.1f," % t + ",".join("" if k not in v else ("%g" % v[k]) for k in keys) + "\n")
    print("%s: %d rows x %d values" % (out, len(rows), len(keys)))


def cmd_replay(args):
    exe = os.path.join(ROOT, "host_tests", "build", "host_tests.exe" if os.name == "nt" else "host_tests")
    if not os.path.exists(exe):
        sys.exit("build the host tests once first: host_tests\\run_tests.bat (or bash host_tests/run_tests.sh)")
    sys.exit(subprocess.call([exe, "--replay", args.file]))


def main():
    ap = argparse.ArgumentParser(description="The board's OBD log (SD card agent/obdlog): pull, summary, csv, replay")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("--port")
    p.set_defaults(fn=cmd_list)
    p = sub.add_parser("pull")
    p.add_argument("name", nargs="?", default="latest", help="latest (default), all or a file name")
    p.add_argument("--port")
    p.add_argument("--out", default=DEFAULT_OUT)
    p.set_defaults(fn=cmd_pull)
    p = sub.add_parser("summary")
    p.add_argument("file")
    p.add_argument("--max", type=int, default=80, help="status / event lines shown")
    p.set_defaults(fn=cmd_summary)
    p = sub.add_parser("csv")
    p.add_argument("file")
    p.add_argument("-o")
    p.set_defaults(fn=cmd_csv)
    p = sub.add_parser("replay")
    p.add_argument("file")
    p.set_defaults(fn=cmd_replay)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
