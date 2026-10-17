"""Check Internet radio stations the way the board's RadioPlayer would play them.

usage: python tools/radio_check.py <stations.json> <out.json>
  stations.json: [{"Title", "Genre", "Source1".."Source6"}] (radio-browser style export, "-" = no source)
  out.json:      per station {"ok", "results": [{url, final, codec, kbps, rate, error}]} - "final" is the URL
                 to put in the board's list (radioadd name|url|genre, or the Studio radio tab)

For every source: playlists (.pls / .m3u / served as text) are resolved to stream URLs (the board does not
read playlists), then the stream is opened like the board does (HTTP/1.1, Icy-MetaData, redirects, ICY 200
accepted, TLS without verification) and ~3 s of audio is read: MP3 / AAC-ADTS frames must be found and the
data must arrive at least as fast as it plays. HLS (.m3u8), Ogg, Opus and FLAC are reported as unsupported.
"""
import concurrent.futures as cf
import json
import re
import socket
import ssl
import sys
import time
from urllib.parse import urljoin, urlsplit, urlunsplit

TIMEOUT = 8.0
READ_BYTES = 48 * 1024
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE

MP3_BR = {  # MPEG1 / MPEG2(.5) layer III kbps
    1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0],
    2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0],
}
MP3_SR = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}
AAC_SR = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]


def open_stream(url, hops=5):
    """-> (status, headers{lower}, sock, first_body_bytes, final_url)"""
    for _ in range(hops):
        u = urlsplit(url)
        host, port = u.hostname, u.port or (443 if u.scheme == "https" else 80)
        path = (u.path or "/") + (("?" + u.query) if u.query else "")
        s = socket.create_connection((host, port), timeout=TIMEOUT)
        if u.scheme == "https":
            s = CTX.wrap_socket(s, server_hostname=host)
        req = (f"GET {path} HTTP/1.1\r\nHost: {u.netloc}\r\nUser-Agent: Mozilla/5.0 (ESP32-AI radio)\r\n"
               "Icy-MetaData: 1\r\nAccept: */*\r\nConnection: close\r\n\r\n")
        s.sendall(req.encode())
        buf = b""
        t0 = time.time()
        while b"\r\n\r\n" not in buf and b"\n\n" not in buf:
            if time.time() - t0 > TIMEOUT or len(buf) > 16384:
                raise RuntimeError("no headers")
            d = s.recv(4096)
            if not d:
                break
            buf += d
        sep = b"\r\n\r\n" if b"\r\n\r\n" in buf else b"\n\n"
        head, _, body = buf.partition(sep)
        lines = head.decode("latin-1").splitlines()
        if not lines:
            raise RuntimeError("empty reply")
        parts = lines[0].split()
        status = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0
        headers = {}
        for ln in lines[1:]:
            if ":" in ln:
                k, v = ln.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        if status in (301, 302, 303, 307, 308) and headers.get("location"):
            s.close()
            url = urljoin(url, headers["location"])
            continue
        return status, headers, s, body, url
    raise RuntimeError("too many redirects")


def dechunk(data):
    out, i = b"", 0
    while i < len(data):
        j = data.find(b"\r\n", i)
        if j < 0:
            break
        try:
            n = int(data[i:j].split(b";")[0], 16)
        except ValueError:
            return data  # not chunked after all
        if n == 0:
            break
        out += data[j + 2:j + 2 + n]
        i = j + 2 + n + 2
    return out


def strip_icy(data, metaint):
    if not metaint:
        return data
    out, i = b"", 0
    while i < len(data):
        out += data[i:i + metaint]
        i += metaint
        if i >= len(data):
            break
        i += 1 + data[i] * 16
    return out


def find_frames(d):
    """-> (codec, kbps, rate, channels) once 3 consecutive frames line up, else None"""
    for i in range(min(len(d) - 8, 20000)):
        if d[i] != 0xFF or (d[i + 1] & 0xE0) != 0xE0:
            continue
        b1, b2, b3 = d[i + 1], d[i + 2], d[i + 3]
        if (b1 & 0xF6) == 0xF0:  # AAC ADTS (layer 00)
            sri = (b2 >> 2) & 0xF
            if sri >= len(AAC_SR):
                continue
            pos, ok = i, 0
            while ok < 3 and pos + 7 < len(d) and d[pos] == 0xFF and (d[pos + 1] & 0xF6) == 0xF0:
                flen = ((d[pos + 3] & 3) << 11) | (d[pos + 4] << 3) | (d[pos + 5] >> 5)
                if flen < 7:
                    break
                pos += flen
                ok += 1
            if ok >= 3:
                prof = ((b2 >> 6) & 3) + 1
                ch = ((b2 & 1) << 2) | (b3 >> 6)
                return ("AAC" if prof == 2 else f"AAC(profile {prof})", None, AAC_SR[sri], ch)
            continue
        ver = (b1 >> 3) & 3
        layer = (b1 >> 1) & 3
        if ver == 1 or layer != 1:  # layer III only (Helix mp3 decodes III)
            continue
        bri, sri = (b2 >> 4) & 0xF, (b2 >> 2) & 3
        if bri in (0, 15) or sri == 3:
            continue
        pos, ok = i, 0
        while ok < 3 and pos + 4 < len(d) and d[pos] == 0xFF and (d[pos + 1] & 0xE0) == 0xE0:
            v2 = (d[pos + 1] >> 3) & 3
            br = MP3_BR[1 if v2 == 3 else 2][(d[pos + 2] >> 4) & 0xF] * 1000
            sr = MP3_SR[v2][(d[pos + 2] >> 2) & 3] if v2 in MP3_SR and ((d[pos + 2] >> 2) & 3) < 3 else 0
            if not br or not sr:
                break
            pad = (d[pos + 2] >> 1) & 1
            flen = (144 if v2 == 3 else 72) * br // sr + pad
            pos += flen
            ok += 1
        if ok >= 3:
            return ("MP3", MP3_BR[1 if ver == 3 else 2][bri], MP3_SR[ver][sri], 1 if (b3 >> 6) == 3 else 2)
    return None


def parse_playlist(text, base):
    urls = re.findall(r"(?im)^\s*File\d+\s*=\s*(\S+)", text)
    if not urls:
        urls = [ln.strip() for ln in text.splitlines() if ln.strip() and not ln.strip().startswith("#")]
    return [urljoin(base, u) for u in urls if u.lower().startswith("http")][:4]


def probe(url, depth=0):
    r = {"url": url}
    low = url.lower().split("?")[0]
    if low.endswith(".m3u8"):
        r["error"] = "HLS (.m3u8) - board cannot play"
        return r
    try:
        status, h, s, body, final = open_stream(url)
    except Exception as e:  # noqa: BLE001
        r["error"] = f"connect: {type(e).__name__}: {e}"[:120]
        return r
    r["final"] = final
    r["status"] = status
    ctype = h.get("content-type", "").lower()
    r["ctype"] = ctype
    if status != 200:
        s.close()
        r["error"] = f"HTTP {status}"
        return r
    t0 = time.time()
    data = body
    s.settimeout(3.0)
    try:
        while len(data) < READ_BYTES and time.time() - t0 < 6.0:
            d = s.recv(8192)
            if not d:
                break
            data += d
    except Exception:  # noqa: BLE001
        pass
    dt = max(time.time() - t0, 0.05)
    s.close()
    head = data[:400].lstrip()
    is_playlist = (low.endswith((".pls", ".m3u")) or "mpegurl" in ctype or "scpls" in ctype
                   or head.startswith((b"[playlist]", b"#EXTM3U", b"http")))
    if is_playlist and depth == 0:
        if b"#EXT-X-" in data[:2000]:
            r["error"] = "HLS playlist - board cannot play"
            return r
        subs = parse_playlist(data.decode("utf-8", "replace"), final)
        if not subs:
            r["error"] = "empty playlist"
            return r
        tried = []
        for sub in subs:
            res = probe(sub, 1)
            res["playlist"] = url
            if res.get("ok"):
                return res
            tried.append(res)
        tried[0]["playlist"] = url
        return tried[0]
    if "ogg" in ctype or "opus" in ctype or "flac" in ctype:
        r["error"] = f"unsupported format ({ctype})"
        return r
    if "transfer-encoding" in h and "chunked" in h["transfer-encoding"].lower():
        data = dechunk(data)
    audio = strip_icy(data, int(h.get("icy-metaint", "0") or 0))
    fr = find_frames(audio)
    if not fr:
        r["error"] = f"no MP3/AAC frames in {len(audio)} bytes ({ctype or 'no type'})"
        return r
    codec, kbps, rate, ch = fr
    if kbps is None:
        try:
            kbps = int(h.get("icy-br", "0").split(",")[0])
        except ValueError:
            kbps = 0
    r.update(codec=codec, kbps=kbps, rate=rate, ch=ch, icy_name=h.get("icy-name", "")[:60],
             kBps=round(len(data) / dt / 1024, 1))
    need = (kbps or 128) / 8
    if len(data) < READ_BYTES and len(data) / dt / 1024 < need * 0.9:
        r["error"] = f"too slow: {r['kBps']} KB/s for {kbps} kbps"
        return r
    if kbps and kbps > 192:
        r["error"] = f"{kbps} kbps > 192 (board limit)"
        return r
    if codec.startswith("AAC(") :
        r["error"] = f"{codec} not supported"
        return r
    # what the board would do with the content type (RadioPlayer::session)
    board_url = r.get("final", url).lower()
    if not any(k in ctype for k in ("aac", "mp4a", "mpeg", "mp3")) and ".aac" not in board_url and ".mp3" not in board_url \
            and ctype and "octet" not in ctype:
        r["error"] = f"board refuses content-type {ctype}"
        return r
    if codec == "AAC" and not any(k in ctype for k in ("aac", "mp4a")) and ".aac" not in board_url:
        r["error"] = f"AAC sent as {ctype or 'no type'}: board would decode it as MP3"
        return r
    r["ok"] = True
    return r


def variants(u):
    """SHOUTcast v1 serves its HTML page to a "Mozilla" user agent (the board's): ';' after the path gets the
    stream itself - the URL form the board can play as is (wiki 12, 2026-10-18: 29 more stations)."""
    p = urlsplit(u)
    if p.path in ("", "/") and not p.query:
        return [urlunsplit((p.scheme, p.netloc, "/;", "", "")), urlunsplit((p.scheme, p.netloc, "/;stream.mp3", "", ""))]
    if not p.path.endswith((".pls", ".m3u", ".m3u8")) and ";" not in p.path:
        return [u.rstrip("/") + "/;"]
    return []


def check(st):
    srcs = [st.get(f"Source{i}", "-") for i in range(1, 7)]
    srcs = [s.strip() for s in srcs if s and s.strip() not in ("-", "")]
    results = []
    for u in srcs:
        res = probe(u)
        results.append(res)
        if res.get("ok"):
            break
    if not any(r.get("ok") for r in results):
        for r in list(results):
            if "gaierror" in r.get("error", "") or "HLS" in r.get("error", ""):
                continue
            for v in variants(r.get("final", r["url"])):
                res = probe(v)
                if res.get("ok"):
                    res["fixed_from"] = r["url"]
                    results.append(res)
                    break
            if results[-1].get("ok"):
                break
    return {"title": st["Title"], "genre": st.get("Genre", ""), "desc": st.get("Description", ""),
            "ok": any(r.get("ok") for r in results), "results": results}


def main():
    stations = json.load(open(sys.argv[1], encoding="utf-8"))
    out = []
    with cf.ThreadPoolExecutor(24) as ex:
        for i, res in enumerate(ex.map(check, stations), 1):
            out.append(res)
            if i % 50 == 0:
                print(i, "checked", flush=True)
    json.dump(out, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    ok = [o for o in out if o["ok"]]
    print(f"{len(ok)} / {len(out)} playable on the board")


if __name__ == "__main__":
    main()
