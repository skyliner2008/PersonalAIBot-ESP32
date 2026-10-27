#!/usr/bin/env python3
"""
ESP32-AI Studio — PC companion app for the ESP32-AI Personal Assistant (firmware 3.2+)

    ตั้งค่า (Wi-Fi, API key, ผู้ช่วย, เสียง, จอ) · ทดสอบ API key · debug log สด · แชทพิมพ์คุยกับ AI
    เครื่องมือทดสอบ (เสียง/ไมค์/I2C/tools ของ AI/ความจำ) · จอเสมือน + แตะจากระยะไกล · สถานะ · build/upload

Requires : Python 3.9+, pyserial (pip install pyserial); Tkinter ships with python.org builds
Run      : python tools/pc_app/esp32_ai_studio.py      (or tools\\pc_app\\run_studio.bat)
Docs     : wiki/10-PC-Studio-App.md

Protocol : plain serial console commands (type "help" on the device). Machine-readable replies are
           one line "<TAG> <json>" — CONFIG, OPTIONS, TOOLS, DIAG, WIFISCAN, APITEST, "TOOL <name>".
           Screenshots: "SCREENSHOT w h n" + n raw RGB565 big-endian bytes + "END".
Secrets  : the firmware never prints the API key or Wi-Fi password (config shows only length and
           a hint); this app masks "key=..." in the log and never writes secrets to exported files.
"""

from __future__ import annotations


import json
import math
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover - shown to the user
    tk.Tk().withdraw()
    messagebox.showerror("ESP32-AI Studio", "ต้องติดตั้ง pyserial ก่อน:\n\npip install pyserial")
    sys.exit(1)

APP_TITLE = "ESP32-AI Studio"
PROJECT_DIR = Path(__file__).resolve().parents[2]
BAUD = 115200
MAX_LOG_LINES = 6000

# ---- Dark theme (matches the device: black background, white text) ----
BG = "#0b0d10"
PANEL = "#151a20"
PANEL2 = "#1e252e"
FG = "#e8edf2"
DIM = "#8b97a5"
ACCENT = "#36b5ff"
OK = "#3ddc84"
WARN = "#ffb020"
ERR = "#ff5c5c"
USER = "#9fc4ff"
UI_FONT = ("Leelawadee UI", 10)
UI_FONT_B = ("Leelawadee UI", 10, "bold")
UI_FONT_BIG = ("Leelawadee UI", 13, "bold")
MONO = ("Consolas", 10)

EMOTIONS = ["neutral", "happy", "excited", "love", "sad", "worried", "angry", "surprised", "sleepy",
            "thinking", "curious", "confused", "shy", "wink"]
# set wake 0|1|2 (firmware Settings::wakeMode)
WAKE_MODES = ["ระดับเสียง (แบบเดิม)", "คำปลุกอย่างเดียว", "คำปลุก + ระดับเสียง"]

TAG_RE = re.compile(r"^(CONFIG|OPTIONS|TOOLS|DIAG|WIFISCAN|APITEST|RECINFO|RADIOLIST|TOOL|OTA)\s+(.*)$")
SECRET_RE = re.compile(r"(key=)[^&\s\"]+")


def mask_secrets(text: str) -> str:
    return SECRET_RE.sub(r"\1***", text)


def rgb565be_to_ppm(w: int, h: int, data: bytes) -> bytes:
    """LovyanGFX sprite buffer (RGB565, big-endian) -> binary PPM."""
    out = bytearray(w * h * 3)
    j = 0
    for i in range(0, min(len(data), w * h * 2), 2):
        v = (data[i] << 8) | data[i + 1]
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[j] = (r << 3) | (r >> 2)
        out[j + 1] = (g << 2) | (g >> 4)
        out[j + 2] = (b << 3) | (b >> 2)
        j += 3
    return b"P6\n%d %d\n255\n" % (w, h) + bytes(out)


# ============================================================
# Serial link (background reader thread -> event queue)
# ============================================================

class SerialLink:
    def __init__(self, events: "queue.Queue"):
        self.events = events
        self.ser = None
        self.port = ""
        self._thread = None
        self._stop = threading.Event()
        self._wlock = threading.Lock()

    @property
    def connected(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def open(self, port: str) -> None:
        self.close()
        s = serial.Serial()
        s.port = port
        s.baudrate = BAUD
        s.timeout = 0.05
        s.dtr = False  # opening the port must not reset the board
        s.rts = False
        s.open()
        self.ser = s
        self.port = port
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader, name="serial-reader", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread and self._thread is not threading.current_thread():
            self._thread.join(timeout=1.5)
        self._thread = None
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def send(self, line: str) -> bool:
        if not self.connected:
            return False
        data = (line.replace("\r", " ").replace("\n", " ").strip() + "\n").encode("utf-8")
        try:
            with self._wlock:
                self.ser.write(data)
            return True
        except (serial.SerialException, OSError) as e:
            self.events.put(("lost", str(e)))
            return False

    def _reader(self) -> None:
        buf = bytearray()
        shot = None     # (kind, a, b, n) while receiving a binary blob: ("shot", w, h, n) / ("rec", rate, 0, n)
        pending = None  # (kind, a, b, data) waiting for the "\nEND\n" trailer that proves nothing was interleaved
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(8192)
            except (serial.SerialException, OSError, TypeError, AttributeError) as e:
                if not self._stop.is_set():
                    self.events.put(("lost", str(e)))
                return
            if not chunk:
                continue
            buf += chunk
            while True:
                if shot:
                    kind, a, b, n = shot
                    if len(buf) < n:
                        break
                    pending = (kind, a, b, bytes(buf[:n]))
                    del buf[:n]
                    shot = None
                    continue
                if pending:
                    if len(buf) < 5:
                        break
                    if bytes(buf[:5]) == b"\nEND\n":
                        del buf[:5]
                        self.events.put(pending)
                    else:  # another task printed into the binary stream: drop it, resync on the trailer
                        self.events.put((pending[0] + "_bad",))
                        k = buf.find(b"\nEND\n")
                        if k >= 0:
                            del buf[: k + 5]
                    pending = None
                    continue
                i = buf.find(b"\n")
                if i < 0:
                    if len(buf) > 262144:
                        buf.clear()
                    break
                raw = bytes(buf[:i])
                del buf[: i + 1]
                text = raw.decode("utf-8", "replace").rstrip("\r")
                if text.startswith("SCREENSHOT ") or text.startswith("RECORD "):
                    try:
                        parts = text.split()
                        if parts[0] == "SCREENSHOT":
                            shot = ("shot", int(parts[1]), int(parts[2]), int(parts[3]))
                        else:
                            shot = ("rec", int(parts[1]), 0, int(parts[2]))
                    except (ValueError, IndexError):
                        shot = None
                    continue
                if text == "END" or not text.strip():
                    continue
                self.events.put(("line", time.time(), text))


def list_ports():
    """[(device, label)] with Espressif / USB-serial boards first."""
    ports = []
    for p in serial.tools.list_ports.comports():
        desc = p.description or ""
        likely = p.vid == 0x303A or any(k in desc for k in ("USB JTAG", "CH34", "CP210", "USB Serial", "ESP32"))
        ports.append((not likely, p.device, f"{p.device} — {desc}"))
    ports.sort()
    return [(d, label) for _, d, label in ports]


def find_pio():
    candidates = [shutil.which("pio"), shutil.which("platformio"),
                  str(Path.home() / ".platformio/penv/Scripts/pio.exe"),
                  str(Path.home() / ".platformio/penv/bin/pio")]
    for c in candidates:
        if c and Path(c).exists():
            return c
    return None


# ============================================================
# Small widgets
# ============================================================

def styled_text(parent, height=10, mono=True, wrap="word") -> tk.Text:
    t = tk.Text(parent, height=height, bg=PANEL, fg=FG, insertbackground=FG, relief="flat",
                font=MONO if mono else UI_FONT, wrap=wrap, padx=8, pady=6,
                selectbackground="#2f4a63", highlightthickness=0, borderwidth=0)
    return t


def with_scroll(parent, widget_factory, **kw):
    frame = ttk.Frame(parent)
    w = widget_factory(frame, **kw)
    sb = ttk.Scrollbar(frame, orient="vertical", command=w.yview)
    w.configure(yscrollcommand=sb.set)
    w.pack(side="left", fill="both", expand=True)
    sb.pack(side="right", fill="y")
    return frame, w


class LabeledEntry(ttk.Frame):
    def __init__(self, parent, label, var, width=32, show=None, toggle=False):
        super().__init__(parent)
        ttk.Label(self, text=label, width=18).pack(side="left")
        self.entry = ttk.Entry(self, textvariable=var, width=width, show=show or "")
        self.entry.pack(side="left", fill="x", expand=True)
        if toggle:
            self._shown = False

            def flip():
                self._shown = not self._shown
                self.entry.configure(show="" if self._shown else (show or "•"))
                btn.configure(text="ซ่อน" if self._shown else "แสดง")

            btn = ttk.Button(self, text="แสดง", width=6, command=flip)
            btn.pack(side="left", padx=(4, 0))


# ============================================================
# Main application
# ============================================================

class Studio:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.events: "queue.Queue" = queue.Queue()
        self.link = SerialLink(self.events)
        self.config_data: dict = {}
        self.options: dict = {}
        self.tools: list = []
        self.diag: dict = {}
        self.tok_history: list = []
        self.history: list = []
        self.hist_pos = 0
        self.screen_img = None
        self.screen_raw = None
        self.profile_baseline: dict = {}
        self.fw_busy = False
        self._last_diag_req = 0.0
        self._reconnect_port = None
        self._shot_retries = 0
        self.rec_info: dict = {}
        self.rec_path = None
        self.shot_time = 0.0

        root.title(APP_TITLE)
        root.geometry("1180x780")
        root.minsize(980, 640)
        root.configure(bg=BG)
        self._style()
        self._build_toolbar()
        self.nb = ttk.Notebook(root)
        self.nb.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self._build_log_tab()
        self._build_chat_tab()
        self._build_settings_tab()
        self._build_tools_tab()
        self._build_screen_tab()
        self._build_status_tab()
        self._build_firmware_tab()
        self._build_radio_tab()  # appended last: other code refers to tab indices
        self._install_clipboard()
        self.refresh_ports(auto_connect=True)
        root.after(30, self._pump)
        root.after(3000, self._periodic)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------------------------------------------------- clipboard
    # Tk binds Ctrl+C/V/X/A by keysym, so with the Thai keyboard layout active (keysym = a Thai
    # letter) the shortcuts silently do nothing. Bind by Windows virtual-key code as well, and give
    # every text field a right-click menu.
    _VK_EVENTS = {67: "<<Copy>>", 86: "<<Paste>>", 88: "<<Cut>>", 65: "<<SelectAll>>"}
    _TEXT_CLASSES = ("TEntry", "Entry", "Text", "TCombobox", "TSpinbox")

    def _install_clipboard(self):
        for cls in self._TEXT_CLASSES:
            self.root.bind_class(cls, "<Control-KeyPress>", self._ctrl_key, add="+")
            self.root.bind_class(cls, "<Button-3>", self._context_menu, add="+")
        self._menu = tk.Menu(self.root, tearoff=0, bg=PANEL2, fg=FG, activebackground="#2f4a63",
                             activeforeground=FG, font=UI_FONT, borderwidth=0)

    def _ctrl_key(self, event):
        if event.keysym.lower() in ("c", "v", "x", "a"):
            return None  # Latin layout: Tk's own bindings already handle it
        ev = self._VK_EVENTS.get(event.keycode)
        if not ev:
            return None
        event.widget.event_generate(ev)
        return "break"

    @staticmethod
    def _readonly(w) -> bool:
        try:
            state = str(w.cget("state"))
        except tk.TclError:
            return False
        return state in ("disabled", "readonly")

    def _context_menu(self, event):
        w = event.widget
        try:
            w.focus_set()
        except tk.TclError:
            pass
        ro = self._readonly(w)
        m = self._menu
        m.delete(0, "end")
        if not ro:
            m.add_command(label="✂  ตัด", accelerator="Ctrl+X", command=lambda: w.event_generate("<<Cut>>"))
        m.add_command(label="📋  คัดลอก", accelerator="Ctrl+C", command=lambda: w.event_generate("<<Copy>>"))
        if not ro:
            m.add_command(label="📥  วาง", accelerator="Ctrl+V", command=lambda: w.event_generate("<<Paste>>"))
        m.add_separator()
        m.add_command(label="เลือกทั้งหมด", accelerator="Ctrl+A", command=lambda: w.event_generate("<<SelectAll>>"))
        if isinstance(w, tk.Text):
            m.add_command(label="คัดลอกทั้งหมด", command=lambda: self.copy_to_clipboard(w.get("1.0", "end-1c")))
        if not ro:
            m.add_command(label="ล้างช่องนี้", command=lambda: self._clear_widget(w))
        m.tk_popup(event.x_root, event.y_root)
        return "break"

    @staticmethod
    def _clear_widget(w):
        if isinstance(w, tk.Text):
            w.delete("1.0", "end")
        else:
            w.delete(0, "end")

    def copy_to_clipboard(self, text: str, note: str = ""):
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self.root.update()  # keep it on the Windows clipboard after the app exits
        if note:
            self.log_local(note, "ok")

    # ---------------------------------------------------------- style
    def _style(self):
        s = ttk.Style(self.root)
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=FG, fieldbackground=PANEL, font=UI_FONT,
                    bordercolor=PANEL2, lightcolor=PANEL2, darkcolor=PANEL2, troughcolor=PANEL)
        s.configure("TFrame", background=BG)
        s.configure("Panel.TFrame", background=PANEL)
        s.configure("TLabel", background=BG, foreground=FG)
        s.configure("Dim.TLabel", background=BG, foreground=DIM)
        s.configure("Title.TLabel", background=BG, foreground=FG, font=UI_FONT_BIG)
        s.configure("Status.TLabel", background=PANEL, foreground=FG, padding=(10, 4))
        s.configure("TLabelframe", background=BG, foreground=ACCENT, bordercolor=PANEL2)
        s.configure("TLabelframe.Label", background=BG, foreground=ACCENT, font=UI_FONT_B)
        s.configure("TButton", background=PANEL2, foreground=FG, padding=(10, 4), borderwidth=0)
        s.map("TButton", background=[("active", "#2a3440"), ("disabled", PANEL)],
              foreground=[("disabled", "#4d5866")])
        s.configure("Accent.TButton", background="#1f6fa8", foreground="#ffffff")
        s.map("Accent.TButton", background=[("active", "#2a86c8")])
        s.configure("Danger.TButton", background="#7a2626", foreground="#ffffff")
        s.map("Danger.TButton", background=[("active", "#9a3030")])
        s.configure("TNotebook", background=BG, borderwidth=0)
        s.configure("TNotebook.Tab", background=PANEL, foreground=DIM, padding=(14, 6), font=UI_FONT)
        s.map("TNotebook.Tab", background=[("selected", PANEL2)], foreground=[("selected", FG)])
        s.configure("TEntry", fieldbackground=PANEL, foreground=FG, insertcolor=FG)
        s.configure("TCombobox", fieldbackground=PANEL, foreground=FG, background=PANEL2, arrowcolor=FG)
        s.map("TCombobox", fieldbackground=[("readonly", PANEL)], foreground=[("readonly", FG)])
        s.configure("TCheckbutton", background=BG, foreground=FG)
        s.map("TCheckbutton", background=[("active", BG)])
        s.configure("TSpinbox", fieldbackground=PANEL, foreground=FG, arrowcolor=FG)
        s.configure("Horizontal.TScale", background=BG)
        s.configure("Treeview", background=PANEL, fieldbackground=PANEL, foreground=FG, rowheight=24)
        s.configure("Treeview.Heading", background=PANEL2, foreground=FG)
        s.configure("Horizontal.TProgressbar", background=OK, troughcolor=PANEL)
        self.root.option_add("*TCombobox*Listbox.background", PANEL)
        self.root.option_add("*TCombobox*Listbox.foreground", FG)
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#2f4a63")
        self.root.option_add("*TCombobox*Listbox.font", UI_FONT)

    # ---------------------------------------------------------- toolbar
    def _build_toolbar(self):
        bar = ttk.Frame(self.root)
        bar.pack(fill="x", padx=8, pady=8)
        ttk.Label(bar, text="⚡ ESP32-AI Studio", style="Title.TLabel").pack(side="left", padx=(0, 12))
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, width=34, state="readonly")
        self.port_box.pack(side="left")
        ttk.Button(bar, text="⟳", width=3, command=self.refresh_ports).pack(side="left", padx=4)
        self.conn_btn = ttk.Button(bar, text="เชื่อมต่อ", style="Accent.TButton", command=self.toggle_connect)
        self.conn_btn.pack(side="left", padx=(0, 10))

        # (screenshot lives in the 🖥️ tab only: from here the picture landed in a tab you could not see)
        quick = [("🎤 ไมค์", "mute"), ("💤 พัก", "sleep"), ("☀ ปลุก", "wake"), ("↻ ต่อ AI ใหม่", "retry"),
                 ("🆕 บทสนทนาใหม่", "new"), ("⟲ รีบูต", "reboot")]
        for label, cmd in quick:
            if cmd == "reboot":
                ttk.Button(bar, text=label, command=self.reboot).pack(side="left", padx=2)
            else:
                ttk.Button(bar, text=label, command=lambda c=cmd: self.send(c)).pack(side="left", padx=2)

        self.status_var = tk.StringVar(value="ยังไม่เชื่อมต่อ")
        status = ttk.Label(self.root, textvariable=self.status_var, style="Status.TLabel", anchor="w")
        status.pack(fill="x", padx=8, pady=(0, 6))

    def refresh_ports(self, auto_connect=False):
        ports = list_ports()
        self._ports = ports
        self.port_box["values"] = [label for _, label in ports]
        if ports and not self.port_var.get():
            self.port_box.current(0)
        if auto_connect and ports and not self.link.connected:
            self.root.after(300, self.toggle_connect)

    def selected_port(self):
        label = self.port_var.get()
        for dev, lab in getattr(self, "_ports", []):
            if lab == label:
                return dev
        return label.split(" ")[0] if label else ""

    def toggle_connect(self):
        if self.link.connected:
            self.link.close()
            self._on_disconnected("ตัดการเชื่อมต่อแล้ว")
            return
        port = self.selected_port()
        if not port:
            messagebox.showwarning(APP_TITLE, "ไม่พบพอร์ต COM — เสียบสาย USB แล้วกด ⟳")
            return
        try:
            self.link.open(port)
        except (serial.SerialException, OSError) as e:
            self.log_local(f"เปิด {port} ไม่ได้: {e}", "err")
            self.status_var.set(f"เปิด {port} ไม่ได้ (โปรแกรมอื่นใช้พอร์ตอยู่? เช่น pio device monitor)")
            return
        self.conn_btn.configure(text="ตัดการเชื่อมต่อ", style="TButton")
        self.status_var.set(f"เชื่อมต่อ {port} แล้ว • กำลังอ่านค่า...")
        self.log_local(f"เชื่อมต่อ {port} @ {BAUD}", "ok")
        for cmd in ("config", "options", "tools", "diag", "radiolist"):
            self.send(cmd, echo=False)

    def _on_disconnected(self, why):
        self.conn_btn.configure(text="เชื่อมต่อ", style="Accent.TButton")
        self.status_var.set(why)

    def send(self, cmd: str, echo=True, mask=False) -> bool:
        if not self.link.connected:
            self.log_local("ยังไม่ได้เชื่อมต่อบอร์ด", "err")
            return False
        ok = self.link.send(cmd)
        if echo:
            shown = cmd
            if mask:
                parts = cmd.split(" ", 2)
                shown = " ".join(parts[:2] + ["***"]) if len(parts) == 3 else cmd
            self.log_local(f">>> {mask_secrets(shown)}", "cmd")
        return ok

    def reboot(self):
        if messagebox.askyesno(APP_TITLE, "รีบูตบอร์ด?"):
            self.send("reboot")

    # ---------------------------------------------------------- event pump
    def call_in_ui(self, fn, *args, **kwargs):
        """Run fn on the Tk main loop (worker threads must not call root.after or widgets directly)."""
        self.events.put(("call", lambda: fn(*args, **kwargs)))

    def _pump(self):
        try:
            for _ in range(400):
                ev = self.events.get_nowait()
                try:
                    self._dispatch(ev)
                except Exception as e:  # one bad line must not stop the pump
                    self.log_local(f"app error: {type(e).__name__}: {e}", "err")
        except queue.Empty:
            pass
        finally:
            self.root.after(30, self._pump)

    def _dispatch(self, ev):
        kind = ev[0]
        if kind == "call":  # from a worker thread: Tk may only be touched from the main loop
            ev[1](*ev[2:])
        elif kind == "line":
            self.handle_line(ev[1], ev[2])
        elif kind == "shot":
            self._shot_retries = 0
            self.show_screenshot(ev[1], ev[2], ev[3])
        elif kind == "shot_bad":
            if self._shot_retries < 2:  # a log line hit the image stream: ask again
                self._shot_retries += 1
                self.root.after(300, lambda: self.capture_screen(echo=False))
            else:
                self.log_local("ภาพจอเสียหายซ้ำ — กดจับภาพอีกครั้ง", "warn")
        elif kind == "rec":
            self.show_recording(ev[1], ev[3])
        elif kind == "rec_bad":
            self.log_local("เสียงที่อัดเสียหาย (มี log แทรก) — อัดใหม่อีกครั้ง", "warn")
        elif kind == "lost":
            port = self.link.port
            self.link.close()
            self._on_disconnected(f"การเชื่อมต่อหลุด: {ev[1]}")
            self.log_local(f"การเชื่อมต่อหลุด ({ev[1]})", "err")
            if not self.fw_busy:
                self._reconnect_port = port or self.selected_port()
                self.root.after(2500, self._try_reconnect)

    def _try_reconnect(self):
        if self.link.connected or self.fw_busy or not self._reconnect_port:
            return
        try:
            self.link.open(self._reconnect_port)
            self.conn_btn.configure(text="ตัดการเชื่อมต่อ", style="TButton")
            self.log_local(f"เชื่อมต่อ {self._reconnect_port} ใหม่แล้ว", "ok")
            for cmd in ("config", "options", "tools", "diag", "radiolist"):
                self.send(cmd, echo=False)
        except (serial.SerialException, OSError):
            self.root.after(2500, self._try_reconnect)

    def _periodic(self):
        try:
            if self.link.connected and self.auto_diag.get():
                self.send("diag", echo=False)
                self._last_diag_req = time.time()
            if self.link.connected and self.auto_shot.get() and self.nb.index("current") == self.screen_tab_index:
                self.capture_screen(echo=False)
        finally:
            # a half-typed interval ("", "abc") used to raise here and stop every auto update for good
            try:
                interval = float(self.diag_interval.get())
            except (tk.TclError, ValueError):
                interval = 3.0
            self.root.after(max(1000, int(interval * 1000)), self._periodic)

    def handle_line(self, ts: float, text: str):
        m = TAG_RE.match(text)
        if m:
            tag, rest = m.groups()
            name = None
            if tag == "TOOL":
                name, _, rest = rest.partition(" ")
            try:
                data = json.loads(rest)
            except json.JSONDecodeError:
                data = None
            if data is not None:
                self.handle_tag(tag, name, data)
            if self.show_replies.get():
                self.append_log(ts, f"{tag}{' ' + name if name else ''} {mask_secrets(rest)[:400]}", "reply")
            return
        text = mask_secrets(text)
        self.append_log(ts, text, self.classify(text))
        if text.startswith("[Chat] "):
            self.chat_line(text[7:])

    @staticmethod
    def classify(text: str) -> str:
        if re.search(r"^E \(|ERROR|error|failed|FAIL|Disconnect reason", text):
            return "err"
        if text.startswith("[Mic]"):
            return "warn" if "⚠" in text else "mic"
        if text.startswith("[Fallback]") or "quota" in text or text.startswith("W ("):
            return "warn"
        if text.startswith("[Chat] user"):
            return "user"
        if text.startswith("[Chat]"):
            return "chat"
        if text.startswith("[Gemini]"):
            return "gemini"
        if text.startswith("[WiFi]"):
            return "wifi"
        if text.startswith("[Touch]"):
            return "touch"
        if text.startswith("[Health]"):
            return "health"
        if text.startswith("OK ") or "Connected" in text or "session ready" in text:
            return "ok"
        return "info"

    def handle_tag(self, tag, name, data):
        if tag == "CONFIG":
            self.config_data = data
            self.load_settings_form()
            self.update_status_bar()
        elif tag == "OPTIONS":
            self.options = data
            self.fill_option_lists()
            if self.config_data:
                self.load_settings_form()
        elif tag == "TOOLS":
            decls = []
            for block in data if isinstance(data, list) else []:
                decls += block.get("functionDeclarations", [])
            self.tools = decls
            self.tool_box["values"] = [d["name"] for d in decls]
            if decls and not self.tool_var.get():
                self.tool_box.current(0)
                self.on_tool_selected()
        elif tag == "DIAG":
            self.diag = data
            self.update_status_bar()
            self.update_status_tab()
        elif tag == "WIFISCAN":
            nets = sorted(data, key=lambda n: -n.get("rssi", -100))
            seen, values = set(), []
            for n in nets:
                if n["ssid"] and n["ssid"] not in seen:
                    seen.add(n["ssid"])
                    values.append(n["ssid"])
            self.ssid_box["values"] = values
            self.wifi_scan_label.configure(
                text="พบ: " + ", ".join(f"{n['ssid']} ({n['rssi']} dBm{'' if n.get('secure') else ', เปิด'})"
                                         for n in nets[:8]))
        elif tag == "APITEST":
            self.show_apitest(data)
        elif tag == "RECINFO":
            self.rec_info = data
        elif tag == "OTA":
            self.show_ota(data if isinstance(data, dict) else {})
        elif tag == "RADIOLIST":
            self.show_radio_list(data)
            if data.get("error"):
                messagebox.showwarning(APP_TITLE, f"บอร์ดตอบกลับ: {data['error']}")
        elif tag == "TOOL":
            self.show_tool_result(name, data)

    # ---------------------------------------------------------- log tab
    def _build_log_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="📜 Debug log")
        top = ttk.Frame(tab)
        top.pack(fill="x", pady=6)
        ttk.Label(top, text="กรอง:").pack(side="left")
        self.filter_var = tk.StringVar()
        ent = ttk.Entry(top, textvariable=self.filter_var, width=28)
        ent.pack(side="left", padx=4)
        ent.bind("<Return>", lambda e: self.refilter())
        ttk.Button(top, text="ใช้ตัวกรอง", command=self.refilter).pack(side="left")
        self.autoscroll = tk.BooleanVar(value=True)
        self.show_ts = tk.BooleanVar(value=True)
        self.hide_touch = tk.BooleanVar(value=False)
        self.hide_health = tk.BooleanVar(value=True)
        self.show_replies = tk.BooleanVar(value=False)
        for text, var in (("เลื่อนอัตโนมัติ", self.autoscroll), ("เวลา", self.show_ts),
                          ("ซ่อน Touch", self.hide_touch), ("ซ่อน Health", self.hide_health),
                          ("แสดงคำตอบ JSON", self.show_replies)):
            ttk.Checkbutton(top, text=text, variable=var, command=self.refilter).pack(side="left", padx=6)
        ttk.Button(top, text="บันทึก log...", command=self.save_log).pack(side="right")
        ttk.Button(top, text="📋 คัดลอก log", command=self.copy_log).pack(side="right", padx=(0, 4))
        ttk.Button(top, text="ล้าง", command=self.clear_log).pack(side="right", padx=4)

        frame, self.log = with_scroll(tab, styled_text, height=20, mono=True, wrap="char")
        frame.pack(fill="both", expand=True)
        colors = {"err": ERR, "warn": WARN, "user": USER, "chat": OK, "gemini": ACCENT, "wifi": "#c792ea",
                  "touch": "#6b7785", "health": "#56606b", "ok": OK, "info": FG, "cmd": "#ffd479",
                  "reply": "#56606b", "local": DIM, "ts": "#56606b", "mic": "#7fdbca"}
        for tag, col in colors.items():
            self.log.tag_configure(tag, foreground=col)
        self.log.configure(state="disabled")
        self.log_lines = []  # (ts, text, kind)

        bottom = ttk.Frame(tab)
        bottom.pack(fill="x", pady=6)
        ttk.Label(bottom, text="คำสั่ง:").pack(side="left")
        self.cmd_var = tk.StringVar()
        cmd = ttk.Entry(bottom, textvariable=self.cmd_var, font=MONO)
        cmd.pack(side="left", fill="x", expand=True, padx=4)
        cmd.bind("<Return>", lambda e: self.send_console())
        cmd.bind("<Up>", lambda e: self.history_nav(-1))
        cmd.bind("<Down>", lambda e: self.history_nav(1))
        ttk.Button(bottom, text="ส่ง", style="Accent.TButton", command=self.send_console).pack(side="left")
        ttk.Button(bottom, text="help", command=lambda: self.send("help")).pack(side="left", padx=4)

    def send_console(self):
        line = self.cmd_var.get().strip()
        if not line:
            return
        self.history.append(line)
        self.hist_pos = len(self.history)
        secret = re.match(r"^set\s+(key|pass)\s", line) is not None
        self.send(line, mask=secret)
        self.cmd_var.set("")

    def history_nav(self, step):
        if not self.history:
            return "break"
        self.hist_pos = max(0, min(len(self.history), self.hist_pos + step))
        self.cmd_var.set(self.history[self.hist_pos] if self.hist_pos < len(self.history) else "")
        return "break"

    def log_local(self, text, kind="local"):
        self.append_log(time.time(), f"• {text}", kind)

    def _visible(self, text, kind):
        if self.hide_touch.get() and kind == "touch":
            return False
        if self.hide_health.get() and kind == "health":
            return False
        f = self.filter_var.get().strip()
        if f:
            try:
                return re.search(f, text, re.IGNORECASE) is not None
            except re.error:
                return f.lower() in text.lower()
        return True

    def append_log(self, ts, text, kind):
        self.log_lines.append((ts, text, kind))
        if len(self.log_lines) > MAX_LOG_LINES:
            del self.log_lines[: len(self.log_lines) - MAX_LOG_LINES]
        if self._visible(text, kind):
            self._insert_log(ts, text, kind)

    def _insert_log(self, ts, text, kind):
        self.log.configure(state="normal")
        if self.show_ts.get():
            self.log.insert("end", time.strftime("%H:%M:%S ", time.localtime(ts)), "ts")
        self.log.insert("end", text + "\n", kind)
        lines = int(self.log.index("end-1c").split(".")[0])
        if lines > MAX_LOG_LINES:
            self.log.delete("1.0", f"{lines - MAX_LOG_LINES}.0")
        self.log.configure(state="disabled")
        if self.autoscroll.get():
            self.log.see("end")

    def refilter(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        for ts, text, kind in self.log_lines:
            if self._visible(text, kind):
                self._insert_log(ts, text, kind)

    def copy_log(self):
        """Copy the selection, or every visible line (as filtered) when nothing is selected."""
        try:
            text = self.log.get("sel.first", "sel.last")
        except tk.TclError:
            text = self.log.get("1.0", "end-1c")
        self.copy_to_clipboard(text, f"คัดลอก log แล้ว ({text.count(chr(10)) + 1} บรรทัด)")

    def clear_log(self):
        self.log_lines.clear()
        self.refilter()

    def save_log(self):
        path = filedialog.asksaveasfilename(defaultextension=".log", initialfile=time.strftime("esp32-ai_%Y%m%d_%H%M%S.log"),
                                            filetypes=[("Log", "*.log"), ("Text", "*.txt")])
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            for ts, text, _ in self.log_lines:
                f.write(time.strftime("%Y-%m-%d %H:%M:%S ", time.localtime(ts)) + text + "\n")
        self.log_local(f"บันทึก log แล้ว: {path}", "ok")

    # ---------------------------------------------------------- chat tab
    def _build_chat_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="💬 แชท")
        ttk.Label(tab, text="พิมพ์คุยกับผู้ช่วยแทนการพูด (คำตอบออกลำโพงบอร์ด + ซับไตเติลบนจอ)", style="Dim.TLabel").pack(
            anchor="w", pady=(8, 4))
        frame, self.chat = with_scroll(tab, styled_text, height=20, mono=False)
        frame.pack(fill="both", expand=True)
        self.chat.tag_configure("user", foreground=USER, font=UI_FONT_B, spacing1=6)
        self.chat.tag_configure("ai", foreground=FG, spacing1=2)
        self.chat.tag_configure("sys", foreground=DIM)
        self.chat.configure(state="disabled", font=("Leelawadee UI", 12))
        row = ttk.Frame(tab)
        row.pack(fill="x", pady=8)
        self.ask_var = tk.StringVar()
        ent = ttk.Entry(row, textvariable=self.ask_var, font=("Leelawadee UI", 12))
        ent.pack(side="left", fill="x", expand=True)
        ent.bind("<Return>", lambda e: self.ask())
        ttk.Button(row, text="ส่ง ➤", style="Accent.TButton", command=self.ask).pack(side="left", padx=6)

    def ask(self):
        text = self.ask_var.get().strip()
        if text and self.send(f"ask {text}"):
            self.ask_var.set("")

    def chat_line(self, text):
        self.chat.configure(state="normal")
        if text.startswith("user: "):
            self.chat.insert("end", "คุณ: " + text[6:] + "\n", "user")
        elif text.startswith("ai: "):
            self.chat.insert("end", "AI: " + text[4:] + "\n", "ai")
        else:
            self.chat.insert("end", "  · " + text + "\n", "sys")
        self.chat.configure(state="disabled")
        self.chat.see("end")

    # ---------------------------------------------------------- settings tab
    def _build_settings_tab(self):
        outer = ttk.Frame(self.nb)
        self.nb.add(outer, text="⚙️ ตั้งค่า")
        canvas = tk.Canvas(outer, bg=BG, highlightthickness=0)
        sb = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        tab = ttk.Frame(canvas)
        tab.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=tab, anchor="nw")
        canvas.configure(yscrollcommand=sb.set)
        canvas.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        def wheel(e):  # only when the pointer is over this page (not a combobox drop-down or another tab)
            if self.nb.index("current") == 2 and str(e.widget).startswith(str(canvas)):
                canvas.yview_scroll(int(-e.delta / 120), "units")
        canvas.bind_all("<MouseWheel>", wheel)

        left = ttk.Frame(tab)
        left.grid(row=0, column=0, sticky="nw", padx=(4, 16), pady=6)
        right = ttk.Frame(tab)
        right.grid(row=0, column=1, sticky="nw", pady=6)

        # Wi-Fi
        wf = ttk.LabelFrame(left, text="Wi-Fi", padding=10)
        wf.pack(fill="x", pady=(0, 10))
        row = ttk.Frame(wf)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text="ชื่อเครือข่าย (SSID)", width=18).pack(side="left")
        self.ssid_var = tk.StringVar()
        self.ssid_box = ttk.Combobox(row, textvariable=self.ssid_var, width=30)
        self.ssid_box.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="สแกน", command=lambda: self.send("wifiscan")).pack(side="left", padx=(4, 0))
        self.wifi_pass_var = tk.StringVar()
        LabeledEntry(wf, "รหัสผ่าน", self.wifi_pass_var, show="•", toggle=True).pack(fill="x", pady=2)
        self.wifi_pass_hint = ttk.Label(wf, text="", style="Dim.TLabel")
        self.wifi_pass_hint.pack(anchor="w")
        self.wifi_scan_label = ttk.Label(wf, text="", style="Dim.TLabel", wraplength=470)
        self.wifi_scan_label.pack(anchor="w", pady=(2, 4))
        ttk.Button(wf, text="บันทึก Wi-Fi ลงบอร์ด", style="Accent.TButton", command=self.save_wifi).pack(anchor="e")

        # API key
        kf = ttk.LabelFrame(left, text="Gemini API key", padding=10)
        kf.pack(fill="x", pady=(0, 10))
        self.key_var = tk.StringVar()
        LabeledEntry(kf, "API key ใหม่", self.key_var, show="•", toggle=True).pack(fill="x", pady=2)
        self.key_hint = ttk.Label(kf, text="", style="Dim.TLabel")
        self.key_hint.pack(anchor="w")
        row = ttk.Frame(kf)
        row.pack(fill="x", pady=(6, 0))
        ttk.Button(row, text="บันทึก key ลงบอร์ด", style="Accent.TButton", command=self.save_key).pack(side="right")
        ttk.Button(row, text="🔍 ทดสอบ API key บนบอร์ด", command=lambda: self.send("apitest")).pack(side="right", padx=6)
        ttk.Button(row, text="💾 ไฟล์สำหรับการ์ด SD...", command=self.make_sd_setup_file).pack(side="left")
        ttk.Label(kf, text="สร้าง key ที่ aistudio.google.com/api-keys • มีการ์ด SD: เก็บเข้ารหัสใน agent/secrets.json บนการ์ด "
                           "(ไม่มีการ์ด: ใน NVS ของบอร์ด) — ผูกกับบอร์ดตัวนั้น • ปุ่มซ้าย = สร้าง ai-setup.txt "
                           "จาก Wi-Fi / key ที่กรอกไว้ ไปวางที่การ์ดแล้วเสียบบอร์ด",
                  style="Dim.TLabel", wraplength=470).pack(anchor="w", pady=(6, 0))
        self.apitest_text = styled_text(kf, height=7, mono=False)
        self.apitest_text.pack(fill="x", pady=(6, 0))
        self.apitest_text.configure(state="disabled")
        self.apitest_text.tag_configure("ok", foreground=OK)
        self.apitest_text.tag_configure("err", foreground=ERR)
        self.apitest_text.tag_configure("warn", foreground=WARN)
        self.apitest_text.tag_configure("dim", foreground=DIM)

        # Assistant
        af = ttk.LabelFrame(right, text="ผู้ช่วย", padding=10)
        af.pack(fill="x", pady=(0, 10))
        self.agent_var = tk.StringVar()
        self.user_var = tk.StringVar()
        LabeledEntry(af, "ชื่อผู้ช่วย", self.agent_var).pack(fill="x", pady=2)
        LabeledEntry(af, "ชื่อผู้ใช้ (เรียกคุณว่า)", self.user_var).pack(fill="x", pady=2)
        self.profile_var = tk.StringVar()
        self.voice_var = tk.StringVar()
        self.pers_var = tk.StringVar()
        self.gender_var = tk.StringVar()
        self.eye_var = tk.StringVar()
        self.model_var = tk.StringVar()
        self.thinking_var = tk.StringVar()
        self.language_var = tk.StringVar()
        self.textmodel_var = tk.StringVar()
        self.profile_box = self._combo(af, "โปรไฟล์สำเร็จรูป", self.profile_var, 40)
        self.profile_box.bind("<<ComboboxSelected>>", lambda e: self.on_profile_selected())
        self.voice_box = self._combo(af, "เสียง", self.voice_var, 40)
        self.pers_box = self._combo(af, "นิสัย", self.pers_var, 40)
        self.gender_box = self._combo(af, "เพศ (ค่ะ/ครับ)", self.gender_var, 40)
        self.eye_box = self._combo(af, "สีดวงตา", self.eye_var, 40)
        self.eye_box.bind("<<ComboboxSelected>>", lambda e: self.update_eye_swatch())
        self.eye_swatch = tk.Canvas(af, width=60, height=14, bg=BG, highlightthickness=0)
        self.eye_swatch.pack(anchor="e")

        mf = ttk.LabelFrame(right, text="โมเดล AI", padding=10)
        mf.pack(fill="x", pady=(0, 10))
        self.model_box = self._combo(mf, "Live model หลัก", self.model_var, 40)
        self.thinking_box = self._combo(mf, "ระดับการคิด (Ext.)", self.thinking_var, 40)
        self.language_box = self._combo(mf, "ภาษาที่ AI พูด", self.language_var, 40)
        self.textmodel_box = self._combo(mf, "Text model (ความจำ)", self.textmodel_var, 40)
        self.search_var = tk.BooleanVar(value=False)
        self.consolidate_var = tk.BooleanVar(value=True)
        row = ttk.Frame(mf)
        row.pack(fill="x")
        ttk.Checkbutton(row, text="ค้นหาเว็บ (Google Search — มีโควตาแยก)", variable=self.search_var).pack(side="left")
        ttk.Button(row, text="ลองเปิด Search ใหม่", command=lambda: self.send("searchretry")).pack(side="right")
        ttk.Label(mf, text="Search ติดโควตา บอร์ดจะปิดเองชั่วคราว 6 ชม. • ปุ่มขวา = ลองใช้อีกครั้งเดี๋ยวนี้",
                  style="Dim.TLabel").pack(anchor="w")
        ttk.Checkbutton(mf, text="จัดระเบียบความจำอัตโนมัติ (Flash-Lite)", variable=self.consolidate_var).pack(anchor="w")

        df = ttk.LabelFrame(right, text="เครื่อง", padding=10)
        df.pack(fill="x", pady=(0, 10))
        self.volume_var = tk.IntVar(value=70)
        self.bright_var = tk.IntVar(value=80)
        self.sleep_var = tk.IntVar(value=3)
        self._scale(df, "ความดังวิทยุ", self.volume_var, 0, 100, lambda: self.send(f"set volume {self.volume_var.get()}"))
        # the assistant's voice is locked at its own level (default 80): turning the radio down leaves it alone
        self.aivol_var = tk.IntVar(value=80)
        self._scale(df, "ความดังเสียง AI", self.aivol_var, 10, 100, lambda: self.send(f"set aivolume {self.aivol_var.get()}"))
        self._scale(df, "ความสว่างจอ", self.bright_var, 5, 100, lambda: self.send(f"set brightness {self.bright_var.get()}"))
        self.micgain_var = tk.IntVar(value=5)
        self._scale(df, "ความไวไมค์ (0-7)", self.micgain_var, 0, 7, lambda: self.send(f"set micgain {self.micgain_var.get()}"))
        ttk.Label(df, text="ต้องพูดใกล้บอร์ด = เพิ่มค่า • เสียงแตก/รับเสียงรอบข้างมากไป = ลดค่า (ทีละ 6 dB)",
                  style="Dim.TLabel").pack(anchor="w")
        # what wakes it by voice while asleep (0 loudness, 1 wake word, 2 both) - wiki 14
        self.wake_var = tk.StringVar()
        self.wake_box = self._combo(df, "ปลุกด้วยเสียง", self.wake_var, 24)
        self.wake_box["values"] = WAKE_MODES
        self.wake_box.bind("<<ComboboxSelected>>",
                           lambda e: self.send(f"set wake {max(0, self.wake_box.current())}"))
        ttk.Label(df, text="คำปลุก = ESP-SR ในตัวบอร์ด (ต้องมี partition model) • เสียงวิทยุ/ทีวีไม่ปลุก",
                  style="Dim.TLabel").pack(anchor="w")
        row = ttk.Frame(df)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text="พักเมื่อเงียบ (นาที)", width=18).pack(side="left")
        ttk.Spinbox(row, from_=0, to=120, textvariable=self.sleep_var, width=6).pack(side="left")
        ttk.Label(row, text="0 = ไม่พัก", style="Dim.TLabel").pack(side="left", padx=6)
        self.clock_var = tk.IntVar(value=5)
        row = ttk.Frame(df)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text="นาฬิกาหลังพัก (นาที)", width=18).pack(side="left")
        ttk.Spinbox(row, from_=0, to=120, textvariable=self.clock_var, width=6).pack(side="left")
        ttk.Label(row, text="พักครบเวลานี้ → นาฬิกา + หรี่จอ (0 = ไม่ใช้)", style="Dim.TLabel").pack(side="left", padx=6)
        self.dim_var = tk.IntVar(value=8)
        self._scale(df, "แสงจอตอนเป็นนาฬิกา", self.dim_var, 1, 60, lambda: self.send(f"set dimlevel {self.dim_var.get()}"))
        self.rot_var = tk.BooleanVar()
        ttk.Checkbutton(df, text="หมุนจอ 180°", variable=self.rot_var).pack(anchor="w")
        ttk.Label(df, text="สีจอบอร์ดไม่ตรงกับภาพในแอป (พื้นขาว/ตาน้ำเงิน) → แท็บ 🖥️ หน้าจอ → 🌓 สลับสีจอบอร์ด",
                  style="Dim.TLabel").pack(anchor="w")

        ff = ttk.LabelFrame(right, text="การเงิน (ข้อมูลจาก TradingView)", padding=10)
        ff.pack(fill="x", pady=(0, 10))
        self.finance_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(ff, text="เปิด tool การเงิน (วิเคราะห์เทคนิค, สแกนหุ้น, ปฏิทินเศรษฐกิจ, แจ้งเตือนราคา)",
                        variable=self.finance_var).pack(anchor="w")

        btns = ttk.Frame(tab)
        btns.grid(row=1, column=0, columnspan=2, sticky="ew", padx=4, pady=(0, 12))
        ttk.Button(btns, text="💾 บันทึกการตั้งค่าทั้งหมดลงบอร์ด", style="Accent.TButton",
                   command=self.save_settings).pack(side="left")
        ttk.Button(btns, text="อ่านค่าจากบอร์ด", command=lambda: self.send("config")).pack(side="left", padx=6)
        ttk.Button(btns, text="ส่งออกไฟล์...", command=self.export_settings).pack(side="left")
        ttk.Button(btns, text="นำเข้าไฟล์...", command=self.import_settings).pack(side="left", padx=6)
        self.settings_note = ttk.Label(btns, text="", style="Dim.TLabel")
        self.settings_note.pack(side="left", padx=10)

    def _combo(self, parent, label, var, width):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text=label, width=18).pack(side="left")
        box = ttk.Combobox(row, textvariable=var, width=width, state="readonly")
        box.pack(side="left", fill="x", expand=True)
        return box

    def _scale(self, parent, label, var, lo, hi, on_release):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text=label, width=18).pack(side="left")
        val = ttk.Label(row, width=4, text=str(var.get()))
        # bound straight to the IntVar (IntVar.get() rounds the scale's float); no -command, so no
        # set -> command -> set feedback loop
        scale = ttk.Scale(row, from_=lo, to=hi, orient="horizontal", variable=var)
        scale.pack(side="left", fill="x", expand=True)
        val.pack(side="left", padx=4)
        scale.bind("<ButtonRelease-1>", lambda e: on_release())
        var.trace_add("write", lambda *a: val.configure(text=str(var.get())))

    # option lists <-> labels
    def fill_option_lists(self):
        o = self.options
        self.profile_box["values"] = [f"{p['name']} — {p['summary']}" for p in o.get("profiles", [])] + ["(กำหนดเอง)"]
        gname = {g["id"]: g["name"] for g in o.get("genders", [])}
        self.voice_box["values"] = [f"{v['name']} — {gname.get(v['gender'], v['gender'])} — {v['style']}"
                                    for v in o.get("voices", [])]
        self.pers_box["values"] = [p["name"] for p in o.get("personalities", [])]
        self.gender_box["values"] = [g["name"] for g in o.get("genders", [])]
        self.eye_box["values"] = [e["name"] for e in o.get("eyes", [])]
        self.model_box["values"] = [m["label"] for m in o.get("liveModels", [])]
        self.thinking_box["values"] = o.get("thinking", [])
        self.language_box["values"] = o.get("languages", [])  # T116 (older firmware: none)
        self.textmodel_box["values"] = o.get("textModels", [])

    def _idx(self, key, field, value):
        for i, item in enumerate(self.options.get(key, [])):
            if (item.get(field) if isinstance(item, dict) else item) == value:
                return i
        return -1

    def _set_combo(self, box, index):
        if 0 <= index < len(box["values"]):
            box.current(index)

    def load_settings_form(self):
        c = self.config_data
        if not c:
            return
        self.ssid_var.set(c.get("ssid", ""))
        self.wifi_pass_var.set("")
        self.wifi_pass_hint.configure(text="มีรหัสผ่านอยู่แล้ว (เว้นว่าง = ไม่เปลี่ยน)" if c.get("passSet") else "ยังไม่มีรหัสผ่าน")
        self.key_var.set("")
        if c.get("keySet"):
            where = "การ์ด SD" if c.get("secretsStore") == "sd" else "แฟลชบอร์ด"
            sealed = f" • 🔒 เข้ารหัสใน{where}" if c.get("secretsSealed") else " • ⚠ ยังเก็บแบบไม่เข้ารหัส"
            self.key_hint.configure(text=f"บนบอร์ด: {c.get('keyHint', '')} ({c.get('keyLen', '?')} ตัวอักษร) • เว้นว่าง = ไม่เปลี่ยน{sealed}")
        else:
            self.key_hint.configure(text="ยังไม่ได้ใส่ API key")
        self.agent_var.set(c.get("agent", ""))
        self.user_var.set(c.get("user", ""))
        if self.options:
            pi = self._idx("profiles", "id", c.get("profile"))
            self._set_combo(self.profile_box, pi if pi >= 0 else len(self.profile_box["values"]) - 1)
            self._set_combo(self.voice_box, self._idx("voices", "name", c.get("voice")))
            self._set_combo(self.pers_box, self._idx("personalities", "id", c.get("personality")))
            self._set_combo(self.gender_box, self._idx("genders", "id", c.get("gender")))
            self._set_combo(self.eye_box, self._idx("eyes", "id", c.get("eye")))
            self._set_combo(self.model_box, int(c.get("model", 0)))
            self._set_combo(self.thinking_box, int(c.get("thinking", 0)))
            if self.language_box["values"]:
                self._set_combo(self.language_box, int(c.get("language", 0)))
            self._set_combo(self.textmodel_box, int(c.get("textmodel", 0)))
            self.update_eye_swatch()
        self.search_var.set(bool(c.get("search", False)))
        self.consolidate_var.set(bool(c.get("consolidate", True)))
        self.volume_var.set(int(c.get("volume", 70)))
        self.aivol_var.set(int(c.get("aivolume", 80)))
        self.bright_var.set(int(c.get("brightness", 80)))
        self.micgain_var.set(int(c.get("micgain", 5)))
        self._set_combo(self.wake_box, int(c.get("wake", 0)))
        self.sleep_var.set(int(c.get("sleep", 3)))
        self.clock_var.set(int(c.get("clock", 5)))
        self.dim_var.set(int(c.get("dimlevel", 8)))
        self.rot_var.set(c.get("rotation", 1) == 3)
        self.finance_var.set(bool(c.get("finance", True)))
        self.profile_baseline = self.form_values()
        self.settings_note.configure(text=f"อ่านค่าล่าสุด {time.strftime('%H:%M:%S')} • เฟิร์มแวร์ {c.get('fw', '?')}")

    def update_eye_swatch(self):
        i = self.eye_box.current()
        eyes = self.options.get("eyes", [])
        self.eye_swatch.delete("all")
        if 0 <= i < len(eyes):
            self.eye_swatch.create_rectangle(0, 0, 60, 14, fill=eyes[i]["rgb"], outline="")

    def on_profile_selected(self):
        i = self.profile_box.current()
        profiles = self.options.get("profiles", [])
        if not (0 <= i < len(profiles)):
            return
        p = profiles[i]
        self.agent_var.set(p["name"])
        self._set_combo(self.voice_box, self._idx("voices", "name", p["voice"]))
        self._set_combo(self.pers_box, self._idx("personalities", "id", p["personality"]))
        self._set_combo(self.gender_box, self._idx("genders", "id", p["gender"]))
        self._set_combo(self.eye_box, self._idx("eyes", "id", p["eye"]))
        self.update_eye_swatch()

    def form_values(self) -> dict:
        """Current form as console `set` fields -> value strings."""
        o = self.options
        v = {"agent": self.agent_var.get().strip(), "user": self.user_var.get().strip()}

        def pick(box, key, field):
            i = box.current()
            items = o.get(key, [])
            if 0 <= i < len(items):
                return items[i][field] if isinstance(items[i], dict) else str(i)
            return None

        pi = self.profile_box.current()
        profiles = o.get("profiles", [])
        v["profile"] = profiles[pi]["id"] if 0 <= pi < len(profiles) else "custom"
        v["voice"] = pick(self.voice_box, "voices", "name")
        v["personality"] = pick(self.pers_box, "personalities", "id")
        v["gender"] = pick(self.gender_box, "genders", "id")
        v["eye"] = pick(self.eye_box, "eyes", "id")
        v["model"] = str(max(0, self.model_box.current()))
        v["thinking"] = str(max(0, self.thinking_box.current()))
        if self.language_box["values"]:
            v["language"] = str(max(0, self.language_box.current()))
        v["textmodel"] = str(max(0, self.textmodel_box.current()))
        v["search"] = "1" if self.search_var.get() else "0"
        v["consolidate"] = "1" if self.consolidate_var.get() else "0"
        v["volume"] = str(self.volume_var.get())
        v["aivolume"] = str(self.aivol_var.get())
        v["brightness"] = str(self.bright_var.get())
        v["micgain"] = str(self.micgain_var.get())
        v["wake"] = str(max(0, self.wake_box.current()))
        v["sleep"] = str(self.sleep_var.get())
        try:
            v["clock"] = str(max(0, min(120, int(self.clock_var.get()))))
        except (tk.TclError, ValueError):
            v["clock"] = None
        v["dimlevel"] = str(self.dim_var.get())
        v["rotation"] = "3" if self.rot_var.get() else "1"
        v["finance"] = "1" if self.finance_var.get() else "0"
        return {k: val for k, val in v.items() if val is not None}

    def save_settings(self):
        if not self.link.connected:
            messagebox.showwarning(APP_TITLE, "ยังไม่ได้เชื่อมต่อบอร์ด")
            return
        new = self.form_values()
        base = self.profile_baseline
        sent = 0
        if new.get("profile") != base.get("profile") and new.get("profile") != "custom":
            self.send(f"set profile {new['profile']}")
            sent += 1
            # the preset already set these on the board; only send what the user changed after picking it
            p = next(p for p in self.options["profiles"] if p["id"] == new["profile"])
            base = dict(base, agent=p["name"], voice=p["voice"], personality=p["personality"],
                        gender=p["gender"], eye=p["eye"], profile=p["id"])
        for k, val in new.items():
            if k == "profile" or base.get(k) == val:
                continue
            if k in ("agent", "user") and not val:
                continue
            self.send(f"set {k} {val}")
            sent += 1
        self.settings_note.configure(text=f"ส่ง {sent} ค่าแล้ว {time.strftime('%H:%M:%S')}" if sent else "ไม่มีค่าที่เปลี่ยน")
        self.root.after(800, lambda: self.send("config", echo=False))

    def save_wifi(self):
        ssid = self.ssid_var.get().strip()
        if not ssid:
            messagebox.showwarning(APP_TITLE, "ใส่ชื่อ Wi-Fi ก่อน")
            return
        self.send(f"set ssid {ssid}")
        if self.wifi_pass_var.get():
            self.send(f"set pass {self.wifi_pass_var.get()}", mask=True)
            self.wifi_pass_var.set("")
        self.root.after(800, lambda: self.send("config", echo=False))

    def make_sd_setup_file(self):
        """Write ai-setup.txt (plain text) for the SD card root; the board imports, encrypts and deletes it."""
        ssid = self.ssid_var.get().strip()
        pwd = self.wifi_pass_var.get()
        key = self.key_var.get().strip()
        if not ssid and not key:
            messagebox.showwarning(APP_TITLE, "กรอกชื่อ Wi-Fi / รหัสผ่าน และ/หรือ API key ในช่องด้านบนก่อน")
            return
        if key and (len(key) < 20 or any(ch.isspace() for ch in key)):
            messagebox.showwarning(APP_TITLE, "API key ดูไม่ถูกต้อง — ตรวจการคัดลอกอีกครั้ง")
            return
        if not messagebox.askyesno(APP_TITLE, "ไฟล์นี้เก็บรหัสเป็นข้อความธรรมดา\n\n"
                                              "• บันทึกลงการ์ด SD โดยตรง (หน้าแรกของการ์ด)\n"
                                              "• เสียบการ์ดเข้าบอร์ดแล้วเปิดเครื่อง — บอร์ดเข้ารหัสแล้วลบไฟล์ให้\n"
                                              "• อย่าเก็บสำเนาไว้ในเครื่อง\n\nสร้างไฟล์?"):
            return
        path = filedialog.asksaveasfilename(initialfile="ai-setup.txt", defaultextension=".txt",
                                            filetypes=[("Text", "*.txt")], title="บันทึกที่หน้าแรกของการ์ด SD")
        if not path:
            return
        lines = ["# PersonalAIBot-ESP32 setup - the board imports, encrypts and deletes this file"]
        if ssid:
            lines += [f"WIFI_SSID={ssid}", f"WIFI_PASSWORD={pwd}"]
        if key:
            lines.append(f"GEMINI_API_KEY={key}")
        try:
            Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
        except OSError as e:
            messagebox.showerror(APP_TITLE, f"บันทึกไม่ได้: {e}")
            return
        if Path(path).name.lower() != "ai-setup.txt":
            messagebox.showwarning(APP_TITLE, "ชื่อไฟล์ต้องเป็น ai-setup.txt บอร์ดจึงจะอ่าน")
        self.log_local(f"สร้าง {Path(path).name} แล้ว — เสียบการ์ดเข้าบอร์ดแล้วเปิดเครื่อง", "ok")

    def save_key(self):
        key = self.key_var.get().strip()
        if len(key) < 20:
            messagebox.showwarning(APP_TITLE, "API key สั้นเกินไป")
            return
        if any(ch.isspace() for ch in key):
            messagebox.showwarning(APP_TITLE, "API key มีช่องว่าง — ตรวจการคัดลอกอีกครั้ง")
            return
        self.send(f"set key {key}", mask=True)
        self.key_var.set("")
        self.root.after(800, lambda: self.send("config", echo=False))

    def export_settings(self):
        if not self.config_data:
            messagebox.showinfo(APP_TITLE, "อ่านค่าจากบอร์ดก่อน")
            return
        path = filedialog.asksaveasfilename(defaultextension=".json", initialfile="esp32-ai-settings.json",
                                            filetypes=[("JSON", "*.json")])
        if path:
            data = {"app": APP_TITLE, "fw": self.config_data.get("fw"), "ssid": self.ssid_var.get(),
                    "settings": self.form_values(), "note": "ไม่มี API key / รหัส Wi-Fi ในไฟล์นี้"}
            Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
            self.settings_note.configure(text=f"ส่งออกแล้ว: {Path(path).name}")

    def import_settings(self):
        path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
            s = data["settings"]
        except (OSError, ValueError, KeyError) as e:
            messagebox.showerror(APP_TITLE, f"อ่านไฟล์ไม่ได้: {e}")
            return
        cfg = dict(self.config_data)
        cfg.update({"agent": s.get("agent", cfg.get("agent")), "user": s.get("user", cfg.get("user")),
                    "profile": s.get("profile"), "voice": s.get("voice"), "personality": s.get("personality"),
                    "gender": s.get("gender"), "eye": s.get("eye"), "model": int(s.get("model", 0)),
                    "thinking": int(s.get("thinking", 0)), "language": int(s.get("language", cfg.get("language", 0))), "textmodel": int(s.get("textmodel", 0)),
                    "search": s.get("search") == "1", "consolidate": s.get("consolidate") == "1",
                    "volume": int(s.get("volume", 70)), "brightness": int(s.get("brightness", 80)),
                    "micgain": int(s.get("micgain", 5)), "wake": int(s.get("wake", 2)),
                    "sleep": int(s.get("sleep", 3)), "rotation": int(s.get("rotation", 1)),
                    "clock": int(s.get("clock", 5)), "dimlevel": int(s.get("dimlevel", 8)),
                    "finance": s.get("finance", "1") == "1"})
        baseline = self.profile_baseline
        self.config_data, saved = cfg, self.config_data
        self.load_settings_form()
        self.config_data, self.profile_baseline = saved, baseline
        self.settings_note.configure(text="นำเข้าแล้ว — กด 'บันทึกการตั้งค่าทั้งหมดลงบอร์ด' เพื่อส่ง")

    def show_apitest(self, d):
        t = self.apitest_text
        t.configure(state="normal")
        t.delete("1.0", "end")
        if "error" in d and isinstance(d["error"], str):
            t.insert("end", f"ทดสอบไม่ได้: {d['error']}\n", "err")
        else:
            t.insert("end", f"key บนบอร์ด {d.get('keyLen', '?')} ตัวอักษร\n", "dim")
            for m in d.get("models", []):
                ok = m.get("http") == 200
                t.insert("end", f"{'✔' if ok else '✘'} มองเห็น {m['id']} (HTTP {m.get('http')})\n", "ok" if ok else "err")
            for g in d.get("generate", []):
                h = g.get("http")
                err = g.get("error", {})
                if h == 200:
                    t.insert("end", f"✔ เรียก {g['id']} ได้\n", "ok")
                elif h == 400 and "live" in g["id"]:
                    t.insert("end", f"• {g['id']}: ใช้ได้เฉพาะ Live (WebSocket) — ปกติ\n", "dim")
                else:
                    q = err.get("quota") or []
                    extra = f" • โควตา {q[0].get('id')} limit {q[0].get('limit')}" if q else ""
                    t.insert("end", f"✘ {g['id']} HTTP {h} {err.get('status', '')}{extra}\n   {err.get('message', '')[:160]}\n",
                             "warn" if h in (429, 503) else "err")
            s = d.get("search")
            if s:
                h = s.get("http")
                if h == 200:
                    t.insert("end", f"✔ Google Search (grounding) ใช้ได้{' — มีผลค้นหา' if s.get('grounded') else ''}\n", "ok")
                else:
                    err = s.get("error", {})
                    q = err.get("quota") or []
                    extra = f" • โควตา {q[0].get('id')} limit {q[0].get('limit')}" if q else ""
                    t.insert("end", f"✘ Google Search (grounding) กับ {s.get('id')} HTTP {h} {err.get('status', '')}{extra}\n"
                                    f"   {err.get('message', '')[:200]}\n", "warn")
                    if h == 429:  # free tier: no Search grounding for the REST 3.x models at all
                        t.insert("end", "   ถ้า key เป็น free tier: Flash-Lite (REST) ใช้ Search ไม่ได้เลยตามเงื่อนไขของ Google\n"
                                        "   — ไม่ได้แปลว่า Search ตอนคุยด้วยเสียง (Live) ใช้ไม่ได้ ดูสถานะ Search ในแท็บสถานะ\n", "dim")
        t.configure(state="disabled")

    # ---------------------------------------------------------- tools tab
    def _build_tools_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="🧰 เครื่องมือ")
        left = ttk.Frame(tab)
        left.pack(side="left", fill="y", padx=(4, 12), pady=6)
        right = ttk.Frame(tab)
        right.pack(side="left", fill="both", expand=True, pady=6)

        # one box for sound: the recording replaces the old 5-s mic meter (same purpose, more detail)
        rf = ttk.LabelFrame(left, text="เสียง / ไมค์ (อัดเสียงที่ AI ได้ยิน)", padding=10)
        rf.pack(fill="x", pady=(0, 10))
        row = ttk.Frame(rf)
        row.pack(fill="x")
        self.rec_sec = tk.IntVar(value=5)
        ttk.Spinbox(row, from_=1, to=8, textvariable=self.rec_sec, width=3).pack(side="left")
        ttk.Label(row, text="วิ").pack(side="left", padx=(2, 6))
        ttk.Button(row, text="⏺ อัด", style="Accent.TButton", command=self.record_mic).pack(side="left")
        ttk.Button(row, text="▶ เล่น", command=self.play_recording).pack(side="left", padx=4)
        ttk.Button(row, text="📁", width=3, command=self.open_recordings).pack(side="left")
        self.rec_canvas = tk.Canvas(rf, width=300, height=70, bg=PANEL, highlightthickness=0)
        self.rec_canvas.pack(fill="x", pady=(6, 2))
        self.rec_label = ttk.Label(rf, text="กดอัดแล้วพูดใส่บอร์ดตามปกติ • ฟ้า = ส่งให้ AI • ส้ม = ได้ยินแต่ไม่ได้ส่ง",
                                   style="Dim.TLabel", wraplength=300, justify="left")
        self.rec_label.pack(anchor="w")
        row = ttk.Frame(rf)
        row.pack(fill="x", pady=(6, 0))
        self.tone_hz = tk.IntVar(value=1000)
        self.tone_ms = tk.IntVar(value=400)
        ttk.Label(row, text="ลำโพง Hz").pack(side="left")
        ttk.Spinbox(row, from_=100, to=8000, increment=100, textvariable=self.tone_hz, width=6).pack(side="left", padx=4)
        ttk.Label(row, text="ms").pack(side="left")
        ttk.Spinbox(row, from_=50, to=3000, increment=50, textvariable=self.tone_ms, width=6).pack(side="left", padx=4)
        ttk.Button(row, text="🔊 ทดสอบ", command=self.play_tone).pack(side="left", padx=4)

        # status -> 📊 tab, API key test -> ⚙️ tab (its result is shown there)
        hf = ttk.LabelFrame(left, text="ฮาร์ดแวร์", padding=10)
        hf.pack(fill="x", pady=(0, 10))
        row = ttk.Frame(hf)
        row.pack(fill="x")
        for text, cmd in (("สแกน I2C", "i2c"), ("รีจิสเตอร์ ES8311", "regs"), ("สถิติเสียง I2S", "audio")):
            ttk.Button(row, text=text, command=lambda c=cmd: self.send(c)).pack(side="left", padx=(0, 4))
        ttk.Button(hf, text="ตั้งเวลาบอร์ดตามเครื่อง PC (ใช้เมื่อไม่มีเน็ต)",
                   command=lambda: self.send(f"time {int(time.time())}")).pack(fill="x", pady=(4, 0))


        ef = ttk.LabelFrame(left, text="อารมณ์ดวงตา", padding=8)
        ef.pack(fill="x", pady=(0, 10))
        for i, emo in enumerate(EMOTIONS):
            ttk.Button(ef, text=emo, width=10, command=lambda e=emo: self.send(f"expr {e}")).grid(
                row=i // 3, column=i % 3, padx=1, pady=1)

        tf = ttk.LabelFrame(right, text="รัน tool ของ AI บนบอร์ด (เหมือนที่ Gemini เรียก)", padding=10)
        tf.pack(fill="both", expand=True)
        row = ttk.Frame(tf)
        row.pack(fill="x")
        self.tool_var = tk.StringVar()
        self.tool_box = ttk.Combobox(row, textvariable=self.tool_var, state="readonly", width=30)
        self.tool_box.pack(side="left")
        self.tool_box.bind("<<ComboboxSelected>>", lambda e: self.on_tool_selected())
        ttk.Button(row, text="▶ รัน", style="Accent.TButton", command=self.run_selected_tool).pack(side="left", padx=6)
        ttk.Button(row, text="โหลดรายการ tools", command=lambda: self.send("tools", echo=False)).pack(side="left")
        self.tool_desc = ttk.Label(tf, text="", style="Dim.TLabel", wraplength=560, justify="left")
        self.tool_desc.pack(anchor="w", pady=6)
        ttk.Label(tf, text="arguments (JSON):").pack(anchor="w")
        self.tool_args = styled_text(tf, height=5)
        self.tool_args.pack(fill="x")
        ttk.Label(tf, text="ผลลัพธ์:").pack(anchor="w", pady=(8, 0))
        frame, self.tool_out = with_scroll(tf, styled_text, height=6)
        frame.pack(fill="both", expand=True)

        mf = ttk.LabelFrame(right, text="ความจำ / งาน (ผลขึ้นในช่องผลลัพธ์ด้านบน)", padding=10)
        mf.pack(fill="x", pady=(10, 0))
        row = ttk.Frame(mf)
        row.pack(fill="x")
        self.mem_query = tk.StringVar()
        ttk.Entry(row, textvariable=self.mem_query, width=20).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="ค้นความจำ", command=lambda: self.run_tool(
            "recall_memory", {"query": self.mem_query.get() or "ผู้ใช้", "limit": 10})).pack(side="left", padx=4)
        ttk.Button(row, text="งานทั้งหมด", command=lambda: self.run_tool("list_tasks", {})).pack(side="left")
        ttk.Button(row, text="การแจ้งเตือน", command=lambda: self.run_tool("list_reminders", {})).pack(side="left", padx=4)
        ttk.Button(row, text="ล้างความจำ...", style="Danger.TButton", command=lambda: self.danger(
            "ล้างความจำ (facts + กราฟ) ทั้งหมด?", "memclear facts")).pack(side="left", padx=(12, 0))
        ttk.Button(row, text="ล้างประวัติแชท...", style="Danger.TButton", command=lambda: self.danger(
            "ล้างประวัติบทสนทนารายวันทั้งหมด?", "memclear history")).pack(side="left", padx=4)

        df = ttk.LabelFrame(right, text="วินิจฉัยการเชื่อมต่อ Gemini Live (ขั้นสูง ไม่บันทึกถาวร)", padding=10)
        df.pack(fill="x", pady=(10, 0))
        row = ttk.Frame(df)
        row.pack(fill="x")
        # Google Search on/off + "try Search again" live in ⚙️ (saved setting); these are temporary probes
        for text, cmd in (("setup ปกติ", "setupmode full"), ("setup ขั้นต่ำ", "setupmode min"),
                          ("ทดสอบ Search", "setupmode searchonly"),  # [Diag] line: accepted / refused (quota)
                          ("key ใน URL", "keymode query"), ("key ใน header", "keymode header")):
            ttk.Button(row, text=text, command=lambda c=cmd: self.send(c)).pack(side="left", padx=2)

    def play_tone(self):
        try:
            hz, ms = int(self.tone_hz.get()), int(self.tone_ms.get())
        except (tk.TclError, ValueError):
            messagebox.showwarning(APP_TITLE, "ใส่ Hz และ ms เป็นตัวเลข")
            return
        self.send(f"tone {hz} {ms}")

    # ---- recording: what the board uploads to Gemini (after AGC), with per-chunk VAD flags ----
    REC_DIR = PROJECT_DIR / "recordings"

    def record_mic(self):
        try:
            sec = max(1, min(8, int(self.rec_sec.get())))
        except (tk.TclError, ValueError):
            sec = 5
        self.rec_info = {}
        if self.send(f"rec {sec}"):
            self.rec_label.configure(text=f"กำลังอัด {sec} วิ... พูดใส่บอร์ดได้เลย")

    def show_recording(self, rate: int, data: bytes):
        import array
        import wave
        samples = array.array("h")
        samples.frombytes(data[: len(data) // 2 * 2])
        if sys.byteorder != "little":
            samples.byteswap()
        self.REC_DIR.mkdir(exist_ok=True)
        path = self.REC_DIR / time.strftime("rec_%Y%m%d_%H%M%S.wav")
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(samples.tobytes())
        self.rec_path = path

        info = self.rec_info or {}
        chunk = max(1, rate * int(info.get("chunkMs", 40)) // 1000)
        flags = info.get("flags", "")
        levels = []
        for i in range(0, len(samples), chunk):
            seg = samples[i: i + chunk]
            levels.append((sum(v * v for v in seg) / max(1, len(seg))) ** 0.5 / 32768.0)
        peak = max((abs(v) for v in samples), default=0) / 32768.0
        clipped = sum(1 for v in samples if v >= 32700 or v <= -32700)
        voiced = [lv for k, lv in enumerate(levels) if k < len(flags) and int(flags[k]) & 1]
        quiet = sorted(lv for k, lv in enumerate(levels) if not (k < len(flags) and int(flags[k]) & 1))
        sent = sum(1 for f in flags if int(f) & 2)
        speech = (sum(voiced) / len(voiced)) if voiced else 0.0
        noise = quiet[len(quiet) // 2] if quiet else 0.0

        c = self.rec_canvas
        c.delete("all")
        c.update_idletasks()
        w, h = max(300, c.winfo_width()), 70
        n = max(1, len(levels))
        bw = w / n
        for k, lv in enumerate(levels):
            f = int(flags[k]) if k < len(flags) else 0
            db = 20 * math.log10(max(lv, 1e-4))  # -80..0 dBFS
            bh = max(1, (db + 60) / 60 * h)
            col = ACCENT if f & 2 else (WARN if f & 1 else "#3a4552")
            c.create_rectangle(k * bw, h - bh, (k + 1) * bw - 1, h, fill=col, outline="")
        thr = float(info.get("threshold", 0)) * float(info.get("gain", 1))
        if thr > 0:
            y = h - (20 * math.log10(max(thr, 1e-4)) + 60) / 60 * h
            c.create_line(0, y, w, y, fill=ERR, dash=(3, 3))

        def db(x):
            return 20 * math.log10(max(x, 1e-5))
        verdict = []
        if not samples:
            verdict.append("ไม่ได้เสียงเลย (ไมค์ปิด / AI กำลังพูด?)")
        elif not voiced:
            verdict.append("ไม่มีช่วงที่นับเป็นเสียงพูด — พูดดังขึ้น/ใกล้ขึ้น")
        elif speech < 0.03:
            verdict.append("เสียงพูดยังเบา")
        else:
            verdict.append("ระดับเสียงพูดดี")
        if clipped > len(samples) * 0.001:
            verdict.append(f"มีเสียงแตก (clip) {clipped} จุด")
        text = (f"{len(samples) / rate:.1f} วิ • เสียงพูด {db(speech):.0f} dBFS • พื้นหลัง {db(noise):.0f} dBFS • "
                f"ดังสุด {db(peak):.0f} dBFS • ส่งให้ AI {sent}/{len(flags)} ช่วง • ขยาย x{float(info.get('gain', 1)):.1f}\n"
                + " • ".join(verdict) + f"\nไฟล์: {path.name}")
        self.rec_label.configure(text=text)
        self.log_local(f"อัดเสียงแล้ว: {path} — " + " • ".join(verdict), "ok")
        self.play_recording()

    def play_recording(self):
        if not self.rec_path or not Path(self.rec_path).exists():
            messagebox.showinfo(APP_TITLE, "ยังไม่มีเสียงที่อัด — กด ⏺ อัด ก่อน")
            return
        try:
            import winsound
            winsound.PlaySound(str(self.rec_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
        except (ImportError, RuntimeError) as e:
            self.log_local(f"เล่นเสียงไม่ได้: {e}", "warn")

    def open_recordings(self):
        self.REC_DIR.mkdir(exist_ok=True)
        if hasattr(os, "startfile"):
            os.startfile(self.REC_DIR)

    def danger(self, question, cmd):
        if messagebox.askyesno(APP_TITLE, question + "\n\nย้อนกลับไม่ได้"):
            self.send(cmd)

    def on_tool_selected(self):
        name = self.tool_var.get()
        d = next((t for t in self.tools if t["name"] == name), None)
        if not d:
            return
        self.tool_desc.configure(text=d.get("description", ""))
        props = d.get("parameters", {}).get("properties", {})
        required = set(d.get("parameters", {}).get("required", []))
        template = {}
        for k, p in props.items():
            if p.get("enum"):
                template[k] = p["enum"][0]
            elif p.get("type") == "INTEGER":
                template[k] = 1
            elif p.get("type") == "NUMBER":  # was "" -> the board read it as missing / 0
                template[k] = 1.0
            elif p.get("type") == "BOOLEAN":
                template[k] = False
            elif k in required:
                template[k] = ""
        missing = [k for k in required if template.get(k) == ""]
        if missing:
            self.tool_desc.configure(text=d.get("description", "") + f"\n(ต้องกรอก: {', '.join(missing)} — ช่องที่ไม่บังคับลบออกได้)")
        self.tool_args.delete("1.0", "end")
        self.tool_args.insert("1.0", json.dumps(template, ensure_ascii=False, indent=1))

    def run_selected_tool(self):
        try:
            args = json.loads(self.tool_args.get("1.0", "end").strip() or "{}")
        except json.JSONDecodeError as e:
            messagebox.showerror(APP_TITLE, f"JSON ไม่ถูกต้อง: {e}")
            return
        self.run_tool(self.tool_var.get(), args)

    def run_tool(self, name, args):
        if name:
            self.send(f"tool {name} {json.dumps(args, ensure_ascii=False, separators=(',', ':'))}")

    def show_tool_result(self, name, data):
        self.tool_out.configure(state="normal")
        self.tool_out.delete("1.0", "end")
        self.tool_out.insert("1.0", f"{name}\n{json.dumps(data, ensure_ascii=False, indent=2)}")
        self.tool_out.configure(state="disabled")
        if self.nb.index("current") != 3:
            self.log_local(f"ผล tool {name}: {json.dumps(data, ensure_ascii=False)[:200]}", "ok")

    # ---------------------------------------------------------- screen tab
    def _build_screen_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="🖥️ หน้าจอ")
        self.screen_tab_index = 4
        bar = ttk.Frame(tab)
        bar.pack(fill="x", pady=6)
        ttk.Button(bar, text="📷 จับภาพ", style="Accent.TButton", command=self.capture_screen).pack(side="left")
        self.auto_shot = tk.BooleanVar(value=False)
        ttk.Checkbutton(bar, text="อัปเดตอัตโนมัติ", variable=self.auto_shot).pack(side="left", padx=8)
        ttk.Button(bar, text="บันทึก PNG...", command=self.save_screenshot).pack(side="left")
        for text, cmd in (("หน้าหลัก", "ui home"), ("ย้อนกลับ", "ui back"), ("เมนู", "ui open menu"),
                          ("ข้อมูลระบบ", "ui open info")):
            ttk.Button(bar, text=text, command=lambda c=cmd: (self.send(c), self.root.after(600, self.capture_screen))
                       ).pack(side="left", padx=2)
        ttk.Button(bar, text="🌓 สลับสีจอบอร์ด", command=self.toggle_board_invert).pack(side="right")
        ttk.Label(tab, text="คลิกบนภาพ = แตะจอ • ลาก = เลื่อน • คลิกขวา = กดค้าง (ผ่านคำสั่ง ui ใน serial)",
                  style="Dim.TLabel").pack(anchor="w")
        ttk.Label(tab, text="ภาพนี้คือสีที่ถูกต้อง (โหมดมืด: พื้นดำ ตาเหลือง) — ถ้าจอบอร์ดจริงเป็นพื้นขาว/ตาน้ำเงิน "
                            "ให้กด 🌓 สลับสีจอบอร์ด", style="Dim.TLabel").pack(anchor="w")
        self.shot_label = ttk.Label(tab, text="ยังไม่มีภาพ", style="Dim.TLabel")
        self.shot_label.pack(anchor="w")
        self.scale = 2
        self.screen = tk.Canvas(tab, width=320 * self.scale, height=240 * self.scale, bg="#000000",
                                highlightthickness=1, highlightbackground=PANEL2, cursor="hand2")
        self.screen.pack(pady=8)
        self.screen.create_text(320, 240, text="กด 📷 จับภาพ", fill=DIM, font=UI_FONT_BIG, tags="hint")
        self.screen.bind("<ButtonPress-1>", self._screen_down)
        self.screen.bind("<ButtonRelease-1>", self._screen_up)
        self.screen.bind("<Button-3>", self._screen_long)
        self._drag_start = None

    def toggle_board_invert(self):
        """The mirror shows the framebuffer; the IPS panel shows it inverted unless INVON is on."""
        cur = bool(self.config_data.get("invert", True))
        if self.send(f"set invert {0 if cur else 1}"):
            self.root.after(600, lambda: self.send("config", echo=False))

    def capture_screen(self, echo=True):
        self.send("screenshot", echo=echo)

    def show_screenshot(self, w, h, data):
        ppm = rgb565be_to_ppm(w, h, data)
        img = tk.PhotoImage(data=ppm, format="PPM")  # Tk 8.6 takes binary PPM (not base64) for -data
        self.screen_raw = img
        self.screen_img = img.zoom(self.scale, self.scale)
        self.screen.delete("all")
        self.screen.create_image(0, 0, image=self.screen_img, anchor="nw")
        self.shot_time = time.time()
        self.shot_label.configure(text="ภาพเมื่อ " + time.strftime("%H:%M:%S") +
                                  " (ดวงตาขยับตลอด ภาพนิ่งอาจจับตอนกะพริบ/ทำท่าทาง)")

    def _dev_xy(self, e):
        return (max(0, min(319, e.x // self.scale)), max(0, min(239, e.y // self.scale)))

    def _screen_down(self, e):
        self._drag_start = self._dev_xy(e)

    def _screen_up(self, e):
        if not self._drag_start:
            return
        x0, y0 = self._drag_start
        x1, y1 = self._dev_xy(e)
        self._drag_start = None
        if abs(x1 - x0) > 8 or abs(y1 - y0) > 8:
            self.send(f"ui drag {x0} {y0} {x1} {y1}")
        else:
            self.send(f"ui tap {x0} {y0}")
        self._capture_after_touch()

    def _screen_long(self, e):
        x, y = self._dev_xy(e)
        self.send(f"ui long {x} {y}")
        self._capture_after_touch()

    def _capture_after_touch(self):
        # A tap on the face makes the eyes react for ~1.5 s (happy + blush): grab the reaction, then
        # the settled screen again so the mirror ends up matching what the board shows.
        self.root.after(600, lambda: self.capture_screen(echo=False))
        self.root.after(2300, lambda: self.capture_screen(echo=False))

    def save_screenshot(self):
        if not self.screen_img:
            messagebox.showinfo(APP_TITLE, "ยังไม่มีภาพ — กดจับภาพก่อน")
            return
        path = filedialog.asksaveasfilename(defaultextension=".png", initialfile=time.strftime("screen_%Y%m%d_%H%M%S.png"),
                                            filetypes=[("PNG", "*.png")])
        if path:
            self.screen_img.write(path, format="png")
            self.log_local(f"บันทึกภาพจอแล้ว: {path}", "ok")

    # ---------------------------------------------------------- radio tab
    def _build_radio_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="📻 วิทยุ")
        self.radio_list: list[dict] = []
        bar = ttk.Frame(tab)
        bar.pack(fill="x", pady=6)
        ttk.Button(bar, text="โหลดรายการ", command=lambda: self.send("radiolist", echo=False)).pack(side="left")
        ttk.Button(bar, text="▶ เล่น", style="Accent.TButton", command=self.radio_play).pack(side="left", padx=(12, 4))
        ttk.Button(bar, text="⏹ หยุด", command=self.radio_stop).pack(side="left", padx=4)
        ttk.Button(bar, text="⬆", width=3, command=lambda: self.radio_move(-1)).pack(side="left", padx=(12, 2))
        ttk.Button(bar, text="⬇", width=3, command=lambda: self.radio_move(1)).pack(side="left", padx=2)
        ttk.Button(bar, text="ลบ", style="Danger.TButton", command=self.radio_delete).pack(side="left", padx=(12, 4))
        ttk.Button(bar, text="คืนค่ารายการเริ่มต้น", command=self.radio_reset).pack(side="right")
        self.radio_status = ttk.Label(tab, text="ยังไม่ได้โหลดรายการ — กด \"โหลดรายการ\"", style="Dim.TLabel")
        self.radio_status.pack(anchor="w")

        body = ttk.Frame(tab)
        body.pack(fill="both", expand=True, pady=4)
        self.radio_tree = ttk.Treeview(body, columns=("name", "genre", "url"), show="headings", height=14,
                                       selectmode="browse")
        for col, text, w in (("name", "สถานี", 240), ("genre", "แนว", 150), ("url", "URL สตรีม", 520)):
            self.radio_tree.heading(col, text=text)
            self.radio_tree.column(col, width=w, stretch=col == "url")
        sb = ttk.Scrollbar(body, orient="vertical", command=self.radio_tree.yview)
        self.radio_tree.configure(yscrollcommand=sb.set)
        self.radio_tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="left", fill="y")
        self.radio_tree.tag_configure("playing", foreground=OK)
        self.radio_tree.bind("<<TreeviewSelect>>", lambda e: self.radio_fill_form())
        self.radio_tree.bind("<Double-1>", lambda e: self.radio_play())

        form = ttk.LabelFrame(tab, text="เพิ่ม / แก้ไขสถานี")
        form.pack(fill="x", pady=6)
        self.radio_name = tk.StringVar()
        self.radio_url = tk.StringVar()
        self.radio_genre = tk.StringVar()
        fields = (("ชื่อสถานี", self.radio_name, 40), ("URL สตรีม (http/https)", self.radio_url, 80),
                  ("แนว (เช่น ลูกทุ่ง, ข่าว)", self.radio_genre, 40))
        for r, (label, var, w) in enumerate(fields):
            ttk.Label(form, text=label).grid(row=r, column=0, sticky="w", padx=6, pady=3)
            ttk.Entry(form, textvariable=var, width=w).grid(row=r, column=1, sticky="w", padx=6, pady=3)
        btns = ttk.Frame(form)
        btns.grid(row=3, column=0, columnspan=2, sticky="w", padx=6, pady=6)
        ttk.Button(btns, text="➕ เพิ่มเป็นสถานีใหม่", style="Accent.TButton", command=self.radio_add).pack(side="left")
        ttk.Button(btns, text="💾 บันทึกทับสถานีที่เลือก", command=self.radio_edit).pack(side="left", padx=6)
        ttk.Button(btns, text="🔎 ทดสอบ URL จาก PC", command=self.radio_test_url).pack(side="left", padx=6)
        self.radio_test_label = ttk.Label(form, text="", style="Dim.TLabel")
        self.radio_test_label.grid(row=4, column=0, columnspan=2, sticky="w", padx=6, pady=(0, 6))
        ttk.Label(tab, text="AI เลือกสถานีจากรายการนี้ตามชื่อ / ลำดับ / แนวเพลง (\"เปิดวิทยุลูกทุ่ง\", \"เปิดสถานีที่ 3\")  •  "
                            "เก็บในการ์ด SD: agent/radio_stations.json (สูงสุด 40 สถานี, MP3 / AAC)",
                  style="Dim.TLabel").pack(anchor="w")

    def radio_selected(self) -> int:
        sel = self.radio_tree.selection()
        return int(sel[0]) if sel else -1

    def show_radio_list(self, data: dict):
        self.radio_list = data.get("stations", [])
        playing = int(data.get("playing", 0))
        keep = self.radio_selected()
        self.radio_tree.delete(*self.radio_tree.get_children())
        for i, s in enumerate(self.radio_list):
            mark = "▶ " if i + 1 == playing else ""
            self.radio_tree.insert("", "end", iid=str(i),
                                   values=(f"{mark}{i + 1}. {s.get('name', '')}", s.get("genre", ""), s.get("url", "")),
                                   tags=("playing",) if i + 1 == playing else ())
        if 0 <= keep < len(self.radio_list):
            self.radio_tree.selection_set(str(keep))
        now = (f"กำลังเล่น: {self.radio_list[playing - 1].get('name', '')}" if 0 < playing <= len(self.radio_list)
               else f"วิทยุ: {data.get('state', '-')}")
        self.radio_status.configure(text=f"{len(self.radio_list)}/{data.get('max', 40)} สถานี  •  {now}")

    def radio_fill_form(self):
        i = self.radio_selected()
        if 0 <= i < len(self.radio_list):
            s = self.radio_list[i]
            self.radio_name.set(s.get("name", ""))
            self.radio_url.set(s.get("url", ""))
            self.radio_genre.set(s.get("genre", ""))

    def radio_form_line(self) -> str | None:
        name, url, genre = (v.get().strip() for v in (self.radio_name, self.radio_url, self.radio_genre))
        err = None
        if not name:
            err = "ใส่ชื่อสถานีก่อน"
        elif "|" in name + url + genre:
            err = "ห้ามใช้เครื่องหมาย |"
        elif not re.match(r"^https?://\S+$", url) or not url.isascii():
            err = "URL ต้องขึ้นต้นด้วย http:// หรือ https:// และไม่มีช่องว่าง / ภาษาไทย"
        elif len(name.encode()) > 47 or len(genre.encode()) > 39 or len(url) > 199:
            err = "ยาวเกินไป (ชื่อ ≤ 47 ไบต์ ≈ ภาษาไทย 15 ตัว, แนว ≤ 39 ไบต์, URL ≤ 199 ตัว)"
        if err:
            messagebox.showwarning(APP_TITLE, err)
            return None
        return f"{name}|{url}|{genre}"

    def radio_add(self):
        line = self.radio_form_line()
        if line:
            self.send(f"radioadd {line}")

    def radio_edit(self):
        i = self.radio_selected()
        if i < 0:
            messagebox.showwarning(APP_TITLE, "เลือกสถานีในตารางก่อน")
            return
        line = self.radio_form_line()
        if line:
            self.send(f"radioedit {i + 1} {line}")

    def radio_delete(self):
        i = self.radio_selected()
        if 0 <= i < len(self.radio_list) and messagebox.askyesno(
                APP_TITLE, f"ลบสถานี \"{self.radio_list[i].get('name', '')}\" ออกจากรายการ?"):
            self.radio_tree.selection_remove(*self.radio_tree.selection())
            self.send(f"radiodel {i + 1}")

    def radio_move(self, step: int):
        i = self.radio_selected()
        j = i + step
        if 0 <= i < len(self.radio_list) and 0 <= j < len(self.radio_list):
            self.radio_tree.selection_set(str(j))  # the reply redraws the list and keeps this row selected
            self.send(f"radiomove {i + 1} {j + 1}", echo=False)

    def radio_play(self):
        i = self.radio_selected()
        if i < 0:
            messagebox.showwarning(APP_TITLE, "เลือกสถานีในตารางก่อน")
            return
        if self.send(f"radio {i + 1}"):
            self.root.after(2000, lambda: self.send("radiolist", echo=False))

    def radio_stop(self):
        if self.send("radio stop"):
            self.root.after(800, lambda: self.send("radiolist", echo=False))

    def radio_reset(self):
        if messagebox.askyesno(APP_TITLE, "แทนที่รายการทั้งหมดด้วยสถานีเริ่มต้น 16 สถานี?\n(สถานีที่เพิ่มเองจะหายไป)"):
            self.send("radioreset")

    def radio_test_url(self):
        url = self.radio_url.get().strip()
        if not re.match(r"^https?://", url):
            messagebox.showwarning(APP_TITLE, "ใส่ URL ก่อน")
            return
        self.radio_test_label.configure(text="กำลังทดสอบ...", foreground=DIM)

        def work():
            import ssl
            import urllib.request
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "ESP32-AI-Radio/1.0", "Icy-MetaData": "1"})
                ctx = ssl._create_unverified_context()  # the board does not verify stream certificates either
                t0 = time.time()
                with urllib.request.urlopen(req, timeout=8, context=ctx) as r:
                    ctype = (r.headers.get("Content-Type") or "?").lower()
                    icy = r.headers.get("icy-name") or ""
                    got = 0
                    while got < 32768 and time.time() - t0 < 6:
                        chunk = r.read(4096)
                        if not chunk:
                            break
                        got += len(chunk)
                fmt = "MP3" if ("mpeg" in ctype or "mp3" in ctype) else "AAC" if "aac" in ctype else ctype
                ok = got >= 8192 and fmt in ("MP3", "AAC")
                msg = (f"{'✅ ใช้ได้' if ok else '⚠ น่าสงสัย'}: {fmt}, ได้ข้อมูล {got // 1024} KB ใน {time.time() - t0:.1f} วิ"
                       + (f"  •  icy-name: {icy}" if icy else "")
                       + ("" if ok else "  (บอร์ดเล่นได้เฉพาะสตรีม MP3 / AAC)"))
                color = OK if ok else WARN
            except Exception as e:  # noqa: BLE001 - any network error goes to the label
                msg, color = f"❌ เปิดไม่ได้: {e}", ERR
            self.call_in_ui(self.radio_test_label.configure, text=msg, foreground=color)

        threading.Thread(target=work, daemon=True).start()

    # ---------------------------------------------------------- status tab
    def _build_status_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="📊 สถานะ")
        bar = ttk.Frame(tab)
        bar.pack(fill="x", pady=6)
        self.auto_diag = tk.BooleanVar(value=True)
        self.diag_interval = tk.DoubleVar(value=3.0)
        ttk.Checkbutton(bar, text="อัปเดตทุก", variable=self.auto_diag).pack(side="left")
        ttk.Spinbox(bar, from_=1, to=30, textvariable=self.diag_interval, width=4).pack(side="left")
        ttk.Label(bar, text="วินาที").pack(side="left", padx=4)
        ttk.Button(bar, text="อัปเดตตอนนี้", command=lambda: self.send("diag", echo=False)).pack(side="left", padx=8)

        body = ttk.Frame(tab)
        body.pack(fill="both", expand=True)
        self.diag_tree = ttk.Treeview(body, columns=("v",), show="tree headings", height=22)
        self.diag_tree.heading("#0", text="รายการ")
        self.diag_tree.heading("v", text="ค่า")
        self.diag_tree.column("#0", width=230)
        self.diag_tree.column("v", width=520)
        self.diag_tree.pack(side="left", fill="both", expand=True)
        right = ttk.Frame(body)
        right.pack(side="left", fill="y", padx=10)
        ttk.Label(right, text="โทเคน/นาที (ลิมิต Live 65k TPM)", style="Dim.TLabel").pack(anchor="w")
        self.tok_canvas = tk.Canvas(right, width=320, height=160, bg=PANEL, highlightthickness=0)
        self.tok_canvas.pack()
        ttk.Label(right, text="โมเดล Live (cooldown)", style="Dim.TLabel").pack(anchor="w", pady=(12, 0))
        self.model_tree = ttk.Treeview(right, columns=("cd",), show="tree headings", height=4)
        self.model_tree.heading("#0", text="model")
        self.model_tree.heading("cd", text="พัก (วิ)")
        self.model_tree.column("#0", width=230)
        self.model_tree.column("cd", width=80)
        self.model_tree.pack(fill="x")

    STATE_TH = {"boot": "กำลังบูต", "connecting": "กำลังเชื่อมต่อ", "idle": "พร้อมคุย", "listening": "กำลังฟัง",
                "thinking": "กำลังคิด", "speaking": "กำลังพูด", "error": "มีปัญหา", "sleep": "พักอยู่",
                "setup": "รอตั้งค่า"}

    def update_status_bar(self):
        d, c = self.diag, self.config_data
        if not self.link.connected:
            return
        parts = [f"🔌 {self.link.port}"]
        if d:
            parts.append(f"fw {d.get('fw', '?')}")
            parts.append(self.STATE_TH.get(d.get("state"), d.get("state", "?")))
            parts.append(f"Wi-Fi {d.get('rssi', '')} dBm" if d.get("wifi") else "Wi-Fi ✘")
            parts.append(f"AI: {d.get('session')} • {d.get('active_model', '')}")
            st = d.get("search_state")
            if st == "suspect":
                parts.append("Search: กำลังตรวจ")
            elif st == "quota" or (st is None and not d.get("google_search", True) and d.get("search_off_s")):
                parts.append("Search ปิดชั่วคราว (โควตาหมด)")
            if d.get("hold_s"):
                parts.append(f"⏸ พักการเชื่อมต่อ {d['hold_s']} วิ ({d.get('last_failure')})")
            if d.get("muted"):
                parts.append("🎤 ปิดไมค์")
            parts.append(f"{d.get('tokens_per_min', 0)} tok/min")
        elif c:
            parts.append(f"fw {c.get('fw', '?')}")
        self.status_var.set("   •   ".join(parts))

    def update_status_tab(self):
        d = self.diag
        labels = {
            "fw": "เฟิร์มแวร์", "state": "สถานะผู้ช่วย", "session": "เซสชัน Gemini", "wifi": "Wi-Fi",
            "rssi": "สัญญาณ (dBm)", "ip": "IP", "muted": "ปิดไมค์", "sleeping": "พักเครื่อง",
            "auth_error": "API key ผิด", "error_text": "ข้อความผิดพลาด", "hold_s": "พักการเชื่อมต่อ (วิ)",
            "last_failure": "ความผิดพลาดล่าสุด", "last_close": "ปิดการเชื่อมต่อล่าสุด", "active_model": "โมเดลที่ใช้",
            "preferred_model": "โมเดลหลัก", "google_search": "Google Search", "search_off_s": "Search ปิดอีก (วิ)", "search_state": "สถานะ Search (ok/suspect/quota)",
            "setup_mode": "setup mode", "text_model": "Text model", "tokens_per_min": "โทเคน/นาที",
            "consolidations_today": "จัดความจำวันนี้", "memories": "ความจำ (facts)", "relations": "ความสัมพันธ์ (กราฟ)",
            "open_tasks": "งานค้าง", "reminders": "การแจ้งเตือน", "volume": "ความดัง", "mic_level": "ระดับไมค์",
            "heap_kb": "RAM ว่าง (KB)", "heap_min_kb": "RAM ต่ำสุด (KB)", "heap_largest_kb": "RAM ก้อนใหญ่สุด (KB)", "psram_kb": "PSRAM ว่าง (KB)",
            "storage": "ที่เก็บข้อมูล", "storage_free_mb": "พื้นที่ว่าง (MB)", "battery": "แบตเตอรี่ (%)",
            "uptime_s": "เปิดมา (วิ)", "epoch": "เวลา"}
        self.diag_tree.delete(*self.diag_tree.get_children())
        for k, v in d.items():
            if k == "live_models":
                continue
            if k == "epoch" and v:
                v = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(v))
            if k == "battery" and v == -1:
                v = "USB"
            if isinstance(v, bool):
                v = "✔" if v else "—"
            self.diag_tree.insert("", "end", text=labels.get(k, k), values=(str(v),))
        self.model_tree.delete(*self.model_tree.get_children())
        for m in d.get("live_models", []):
            self.model_tree.insert("", "end", text=m["id"], values=(m.get("cooldown_s", 0),))
        self.tok_history.append(int(d.get("tokens_per_min", 0)))
        self.tok_history = self.tok_history[-100:]
        self.draw_tokens()

    def draw_tokens(self):
        c = self.tok_canvas
        c.delete("all")
        w, h, limit = 320, 160, 65000
        top = max(limit * 1.1, max(self.tok_history or [0]) * 1.1)
        y_lim = h - limit / top * h
        c.create_line(0, y_lim, w, y_lim, fill=ERR, dash=(4, 3))
        c.create_text(w - 4, y_lim - 8, text="65k", fill=ERR, anchor="e", font=("Consolas", 8))
        if len(self.tok_history) > 1:
            step = w / (len(self.tok_history) - 1)
            pts = []
            for i, v in enumerate(self.tok_history):
                pts += [i * step, h - v / top * h]
            c.create_line(*pts, fill=ACCENT, width=2)
        c.create_text(4, 10, text=f"ล่าสุด {self.tok_history[-1] if self.tok_history else 0}", fill=FG, anchor="w",
                      font=("Consolas", 9))

    # ---------------------------------------------------------- firmware tab
    def _build_firmware_tab(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="🔧 เฟิร์มแวร์")
        info = ttk.Frame(tab)
        info.pack(fill="x", pady=6)
        self.pio = find_pio()
        ttk.Label(info, text=f"โปรเจกต์: {PROJECT_DIR}", style="Dim.TLabel").pack(anchor="w")
        ttk.Label(info, text=f"PlatformIO: {self.pio or 'ไม่พบ (ติดตั้ง PlatformIO / VS Code extension)'}",
                  style="Dim.TLabel").pack(anchor="w")
        bar = ttk.Frame(tab)
        bar.pack(fill="x", pady=6)
        ttk.Button(bar, text="🔨 Build", command=lambda: self.run_fw(["run"], "build")).pack(side="left")
        ttk.Button(bar, text="⬆ Build + Upload", style="Accent.TButton",
                   command=lambda: self.run_fw(["run", "-t", "upload"], "upload")).pack(side="left", padx=6)
        ttk.Button(bar, text="🧪 Host unit tests", command=self.run_tests).pack(side="left")
        ttk.Button(bar, text="📁 เปิดโฟลเดอร์โปรเจกต์", command=lambda: os.startfile(PROJECT_DIR)
                   if hasattr(os, "startfile") else None).pack(side="left", padx=6)
        self.fw_status = ttk.Label(bar, text="", style="Dim.TLabel")
        self.fw_status.pack(side="left", padx=10)
        # over-the-air: the board downloads the latest GitHub release itself (settings + memory stay)
        ota = ttk.Frame(tab)
        ota.pack(fill="x", pady=(0, 6))
        ttk.Label(ota, text="อัปเดตผ่าน Wi-Fi (GitHub Releases):").pack(side="left")
        ttk.Button(ota, text="🔍 ตรวจเวอร์ชันใหม่", command=lambda: self.send("ota check")).pack(side="left", padx=6)
        ttk.Button(ota, text="⬇ ติดตั้งบนบอร์ด", command=self.ota_install).pack(side="left")
        ttk.Button(ota, text="↩ กลับเวอร์ชันก่อน", command=lambda: messagebox.askyesno(
            APP_TITLE, "บูตเฟิร์มแวร์ในอีกช่อง (เวอร์ชันก่อนอัปเดตล่าสุด)? บอร์ดจะรีสตาร์ท") and self.send("ota rollback")
        ).pack(side="left", padx=6)
        self.ota_status = ttk.Label(ota, text="", style="Dim.TLabel")
        self.ota_status.pack(side="left", padx=10)
        # release signing key (tools/ota_keygen.py): boards built with its public key install only signed updates
        keys = ttk.Frame(tab)
        keys.pack(fill="x", pady=(0, 6))
        ttk.Label(keys, text="กุญแจเซ็นอัปเดต (OTA):").pack(side="left")
        ttk.Button(keys, text="🔑 สร้างกุญแจ", command=self.key_create).pack(side="left", padx=6)
        ttk.Button(keys, text="💾 สำรองกุญแจ...", command=self.key_backup).pack(side="left")
        ttk.Button(keys, text="📂 นำกุญแจสำรองกลับมา...", command=self.key_restore).pack(side="left", padx=6)
        self.key_status = ttk.Label(keys, text="", style="Dim.TLabel")
        self.key_status.pack(side="left", padx=10)
        self.key_refresh()
        frame, self.fw_out = with_scroll(tab, styled_text, height=24, wrap="char")
        frame.pack(fill="both", expand=True)
        self.fw_out.tag_configure("err", foreground=ERR)
        self.fw_out.tag_configure("ok", foreground=OK)

    def ota_install(self):
        if not self.link.connected:
            messagebox.showwarning(APP_TITLE, "ยังไม่ได้เชื่อมต่อบอร์ด")
            return
        if messagebox.askyesno(APP_TITLE, "ให้บอร์ดดาวน์โหลดและติดตั้งเฟิร์มแวร์ล่าสุดจาก GitHub?\n"
                                          "ค่าที่ตั้งไว้และความจำอยู่ครบ • ใช้เวลา 1-3 นาที ห้ามถอดสาย\n"
                                          "(กด 'ตรวจเวอร์ชันใหม่' ก่อน)"):
            self.send("ota install")

    # ---- release signing key: the same code as tools/ota_keygen.py (the private key stays on this PC) ----
    @staticmethod
    def _keygen():
        tools = str(PROJECT_DIR / "tools")
        if tools not in sys.path:
            sys.path.insert(0, tools)
        import ota_keygen
        return ota_keygen

    def key_refresh(self):
        try:
            st = self._keygen().status()
        except Exception as e:  # header missing / unreadable: say so instead of breaking the tab
            self.key_status.configure(text=f"อ่านสถานะไม่ได้: {e}", foreground=ERR)
            return None
        if st["header_key"] and st["exists"] and st["match"] is not False:
            text, color = f"พร้อม: release ถูกเซ็นด้วย {st['key']}", OK
        elif st["header_key"] and st["exists"]:
            text, color = "กุญแจในเครื่องนี้ไม่ตรงกับ OtaPublicKey.h — release ที่เซ็นจะถูกบอร์ดปฏิเสธ", ERR
        elif st["header_key"]:
            text, color = "เฟิร์มแวร์มีกุญแจแล้วแต่ไม่พบกุญแจส่วนตัวในเครื่องนี้ — นำกุญแจสำรองกลับมา", WARN
        elif st["exists"]:
            text, color = "มีกุญแจส่วนตัวแล้ว แต่ OtaPublicKey.h ยังว่าง — กด 'สร้างกุญแจ' เพื่อเขียน header", WARN
        else:
            text, color = "ยังไม่มีกุญแจ: อัปเดตตรวจแค่ sha256", DIM
        self.key_status.configure(text=text, foreground=color)
        return st

    def _key_need_crypto(self) -> bool:
        kg = self._keygen()
        if kg.have_crypto():
            return False
        if self.fw_busy:
            return True
        if messagebox.askyesno(APP_TITLE, "ต้องใช้แพ็กเกจ Python 'cryptography' สำหรับสร้าง/ตรวจกุญแจ\n\n"
                                          "ติดตั้งตอนนี้? (pip install cryptography)"):
            self._run_process([sys.executable, "-m", "pip", "install", "cryptography"], "ติดตั้ง cryptography", None)
        return True

    def key_create(self):
        kg = self._keygen()
        st = self.key_refresh()
        if not st or self._key_need_crypto():
            return
        try:
            if st["header_key"] and st["exists"]:
                messagebox.showinfo(APP_TITLE, f"มีกุญแจแล้ว ({'ตรงกัน' if st['match'] else 'ไม่ตรงกัน!'}):\n{st['key']}\n\n"
                                               "ไม่สร้างใหม่: บอร์ดที่มีกุญแจเดิมจะอัปเดตไม่ได้อีก")
                return
            if st["header_key"]:
                messagebox.showwarning(APP_TITLE, "OtaPublicKey.h มีกุญแจสาธารณะแล้ว แต่เครื่องนี้ไม่มีกุญแจส่วนตัวของมัน\n\n"
                                                  "ใช้ 'นำกุญแจสำรองกลับมา...' — สร้างใหม่จะทำให้บอร์ดที่มีกุญแจเดิมอัปเดตผ่าน Wi-Fi ไม่ได้")
                return
            if st["exists"]:
                if messagebox.askyesno(APP_TITLE, f"มีกุญแจส่วนตัวอยู่แล้ว:\n{st['key']}\n\nเขียนกุญแจสาธารณะของมันลง "
                                                  "src/app/OtaPublicKey.h ?"):
                    kg.write_header_from_key()
                    self.key_refresh()
                    messagebox.showinfo(APP_TITLE, "เขียน OtaPublicKey.h แล้ว — commit + push ก่อนออก release")
                return
            if not messagebox.askyesno(APP_TITLE, "สร้างกุญแจเซ็นอัปเดต (ECDSA P-256)?\n\n"
                                                  f"• กุญแจส่วนตัว → {kg.key_path()}\n"
                                                  "  (อยู่ในเครื่องนี้เท่านั้น ห้าม commit / ห้ามส่งให้ใคร)\n"
                                                  "• กุญแจสาธารณะ → src/app/OtaPublicKey.h (commit ไฟล์นี้)\n\n"
                                                  "เฟิร์มแวร์ที่ build หลังจากนี้จะติดตั้งเฉพาะอัปเดตที่เซ็นด้วยกุญแจนี้\n"
                                                  "ถ้ากุญแจหาย บอร์ดจะอัปเดตผ่าน Wi-Fi ไม่ได้ (ต้องแฟลช USB) — ต้องสำรองไว้"):
                return
            path = kg.create_key()
        except kg.KeygenError as e:
            self.key_refresh()
            messagebox.showerror(APP_TITLE, str(e))
            return
        self.key_refresh()
        self.log_local(f"[Studio] สร้างกุญแจเซ็นอัปเดตแล้ว: {path} + src/app/OtaPublicKey.h")
        if messagebox.askyesno(APP_TITLE, f"สร้างกุญแจแล้ว\n{path}\n\n"
                                          "ขั้นต่อไป: commit + push src/app/OtaPublicKey.h แล้วออก release ตามปกติ\n\n"
                                          "สำรองกุญแจส่วนตัวไว้ที่อื่นตอนนี้เลย? (USB / password manager)"):
            self.key_backup()

    def key_backup(self):
        kg = self._keygen()
        src = kg.key_path()
        if not src.exists():
            messagebox.showwarning(APP_TITLE, f"ยังไม่มีกุญแจส่วนตัว ({src})")
            return
        dst = filedialog.asksaveasfilename(title="สำรองกุญแจเซ็นอัปเดต (เก็บนอกโฟลเดอร์โปรเจกต์)",
                                           initialfile="ota_signing_key.pem", defaultextension=".pem",
                                           filetypes=[("PEM", "*.pem")])
        if not dst:
            return
        dst = Path(dst).resolve()
        if PROJECT_DIR in dst.parents:  # would sit next to the code: one `git add -A` away from GitHub
            messagebox.showerror(APP_TITLE, "อย่าเก็บไว้ในโฟลเดอร์โปรเจกต์ — เลือกที่อื่น (เช่น USB)")
            return
        try:
            shutil.copyfile(src, dst)
        except OSError as e:
            messagebox.showerror(APP_TITLE, f"สำรองไม่สำเร็จ: {e}")
            return
        self.log_local(f"[Studio] สำรองกุญแจไว้ที่ {dst}")
        messagebox.showinfo(APP_TITLE, f"สำรองแล้ว: {dst}\nเก็บไว้ในที่ปลอดภัย ไม่ส่งให้ใคร")

    def key_restore(self):
        kg = self._keygen()
        if self._key_need_crypto():
            return
        src = filedialog.askopenfilename(title="เลือกไฟล์กุญแจที่สำรองไว้", filetypes=[("PEM", "*.pem"), ("ทุกไฟล์", "*.*")])
        if not src:
            return
        try:
            path = kg.restore_key(Path(src))
            if not kg.header_pem():  # a clone with an empty header: put the matching public key back too
                kg.write_header_from_key()
        except kg.KeygenError as e:
            self.key_refresh()
            messagebox.showerror(APP_TITLE, str(e))
            return
        self.key_refresh()
        messagebox.showinfo(APP_TITLE, f"นำกุญแจกลับมาแล้ว: {path}")

    def show_ota(self, d: dict):
        state = d.get("state", "")
        text = {"checking": "กำลังตรวจ...", "up_to_date": f"ล่าสุดแล้ว (v{d.get('current')})",
                "available": f"มี v{d.get('latest')} (บอร์ด v{d.get('current')})",
                "downloading": f"กำลังดาวน์โหลด {d.get('progress', 0)}%", "ready": "ติดตั้งแล้ว กำลังรีสตาร์ท",
                "error": f"ไม่สำเร็จ: {d.get('error', '')}"}.get(state, f"v{d.get('current')} ({d.get('partition')})")
        if d.get("pending_verify"):
            text += " • กำลังยืนยันเวอร์ชันใหม่"
        self.ota_status.configure(text=text, foreground=ERR if state == "error" else (OK if state == "available" else DIM))
        if state in ("checking", "downloading"):
            self.root.after(1500, lambda: self.send("ota", echo=False))

    def run_fw(self, args, kind):
        if self.fw_busy:
            return
        if not self.pio:
            messagebox.showerror(APP_TITLE, "ไม่พบ PlatformIO (pio)")
            return
        cmd = [self.pio] + args
        reopen = None
        if kind == "upload":
            port = self.link.port if self.link.connected else self.selected_port()
            if port:
                cmd += ["--upload-port", port]
            if self.link.connected:  # the uploader needs the port
                reopen = self.link.port
                self.link.close()
                self._on_disconnected("ปิดพอร์ตชั่วคราวเพื่ออัปโหลดเฟิร์มแวร์...")
        self._run_process(cmd, kind, reopen)

    def run_tests(self):
        bat = PROJECT_DIR / "host_tests" / "run_tests.bat"
        if not bat.exists():
            messagebox.showerror(APP_TITLE, f"ไม่พบ {bat}")
            return
        self._run_process(["cmd", "/c", str(bat)], "tests", None)

    def _run_process(self, cmd, kind, reopen_port):
        self.fw_busy = True
        self.fw_out.delete("1.0", "end")
        self.fw_status.configure(text=f"กำลังทำงาน: {kind} ...")
        self.nb.select(6)

        def worker():
            rc = -1
            try:
                p = subprocess.Popen(cmd, cwd=PROJECT_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                for raw in p.stdout:
                    line = raw.decode("utf-8", "replace").rstrip()
                    self.call_in_ui(self._fw_line, line)
                rc = p.wait()
            except OSError as e:
                self.call_in_ui(self._fw_line, f"ERROR: {e}")
            self.call_in_ui(self._fw_done, kind, rc, reopen_port)

        threading.Thread(target=worker, daemon=True).start()

    def _fw_line(self, line):
        tag = "err" if re.search(r"error|FAILED|failures: [1-9]", line, re.I) else ("ok" if "SUCCESS" in line or "0 failures" in line else "")
        self.fw_out.insert("end", line + "\n", tag)
        self.fw_out.see("end")

    def _fw_done(self, kind, rc, reopen_port):
        self.fw_busy = False
        self.fw_status.configure(text=f"{kind}: {'สำเร็จ ✔' if rc == 0 else f'ล้มเหลว (exit {rc})'}")
        if kind == "ติดตั้ง cryptography" and rc == 0:
            import importlib
            importlib.invalidate_caches()  # the package installed a moment ago is importable in this process
            self.key_refresh()
            messagebox.showinfo(APP_TITLE, "ติดตั้ง cryptography แล้ว — กดปุ่มกุญแจอีกครั้ง")
        if reopen_port:
            self._reconnect_port = reopen_port
            self.root.after(2500, self._try_reconnect)

    def _on_close(self):
        self.link.close()
        self.root.destroy()


def main():
    if sys.platform == "win32":
        try:  # crisp text on high-DPI screens
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    root = tk.Tk()
    Studio(root)
    root.mainloop()


if __name__ == "__main__":
    main()
