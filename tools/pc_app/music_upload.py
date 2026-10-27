"""
Send a song to the board's SD card over the serial console (T123) - used by ESP32-AI Studio's music tab.

    musicput <size> <name>            -> MUSICPUT READY <window> | MUSICPUT ERR <why>
    musicdata <offset> <len> <b64>    -> MUSICPUT ACK <bytes in so far>   (1 KB per line; an ACK per 4 KB)
    musicend <sha256 hex>             -> MUSICPUT OK | MUSICPUT ERR <why>
    musicabort                        -> MUSICPUT ABORTED

Up to <window> chunk lines are in flight (the board grows its RX queue for the upload, and answers every 4 KB, a
refused chunk and the last one). Measured: ~140 KB/s for real songs (~1 min per 8 MB; the board queues one byte per RX item).
The board only writes the chunk at the offset it expects, so a line that lost bytes (refused: wrong length) or came out of order makes it
answer the same count again: then everything from that count is sent again (go-back-N). The sha256 at the end
proves the file. `replies` gets the text after "MUSICPUT " (SerialLink.music_q).
"""

from __future__ import annotations

import base64
import hashlib
import queue
from typing import Callable

CHUNK = 1024


def _get(replies: "queue.Queue[str]", timeout: float) -> str | None:
    try:
        return replies.get(timeout=timeout)
    except queue.Empty:
        return None


def upload(send: Callable[[str], bool], replies: "queue.Queue[str]", name: str, data: bytes,
           progress: Callable[[int], None] | None = None, cancelled: Callable[[], bool] = lambda: False) -> str | None:
    """Returns None when the board saved the file, else why not (Thai / the board's English reason)."""
    while not replies.empty():
        replies.get_nowait()
    if not send(f"musicput {len(data)} {name}"):
        return "ส่งไม่ได้ (การเชื่อมต่อหลุด)"
    r = _get(replies, 5)
    if not r or not r.startswith("READY"):
        return r[4:] if r and r.startswith("ERR ") else (r or "บอร์ดไม่ตอบ")
    parts = r.split()
    window = max(1, min(8, int(parts[1]))) if len(parts) > 1 and parts[1].isdigit() and int(parts[1]) > 0 else 1
    acked = nxt = 0
    stalls = 0
    resent = False  # went back: the replies of the lines still in flight repeat the count - not another loss
    while acked < len(data):
        if cancelled():
            send("musicabort")
            return "ยกเลิก"
        while nxt < len(data) and nxt - acked < window * CHUNK:
            chunk = data[nxt:nxt + CHUNK]
            if not send(f"musicdata {nxt} {len(chunk)} {base64.b64encode(chunk).decode('ascii')}"):
                return "ส่งไม่ได้ (การเชื่อมต่อหลุด)"
            nxt += len(chunk)
        r = _get(replies, 3)
        if r is None:  # lines or replies lost: from the board's count again
            stalls += 1
            if stalls > 5:
                send("musicabort")
                return "บอร์ดไม่ตอบ"
            nxt = acked
            resent = True
            continue
        if r.startswith("ERR"):
            return r[4:]
        if not r.startswith("ACK "):
            continue
        a = int(r[4:])
        if a > acked:
            acked = a
            stalls = 0
            resent = False
            if progress:
                progress(acked)
        elif a == acked and nxt > acked and not resent:
            nxt = acked  # a chunk was refused (and every one after it): go back
            resent = True
    send(f"musicend {hashlib.sha256(data).hexdigest()}")
    r = _get(replies, 15)
    if r == "OK":
        return None
    return r[4:] if r and r.startswith("ERR ") else (r or "บอร์ดไม่ตอบ")
