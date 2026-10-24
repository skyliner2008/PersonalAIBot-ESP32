#!/usr/bin/env python3
"""
Publish the factory firmware as a GitHub Release - manual path (GitHub Actions cannot run on this account).

    python tools/publish_release.py            build (make_release.py) + host tests + publish v<FIRMWARE_VERSION>
    python tools/publish_release.py --dry-run  only check: version, files, tag free
    python tools/publish_release.py --skip-tests   (only on a PC without a C++ compiler)

The repository uses IMMUTABLE releases, so the order matters and is fixed here:
    1. create the release as a DRAFT        (assets can only be attached to drafts)
    2. upload the .bin, .sha256 (+ the -app.bin.sig only if a key was made with tools/ota_keygen.py - optional)
       + dist/PersonalAIBot-Mobile.apk, built here by tools/build_mobile_apk.py when an Android SDK is installed
    3. publish (and mark it latest)
    4. download it again and compare the sha256
A tag that ever belonged to a published release can never be reused (v3.2.0 is burned that way):
bump FIRMWARE_VERSION in src/app/AppConfig.h for every release, commit and push first.

Auth: the token Git already uses for github.com (Git Credential Manager); it is never printed.
"""

from __future__ import annotations

import hashlib
import json
import re
import ssl
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = "skyliner2008/PersonalAIBot-ESP32"
API = f"https://api.github.com/repos/{REPO}"


def version() -> str:
    text = (ROOT / "src" / "app" / "AppConfig.h").read_text(encoding="utf-8")
    return re.search(r'FIRMWARE_VERSION\s+"([^"]+)"', text).group(1)


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()


def token() -> str:
    r = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                       capture_output=True, text=True, cwd=ROOT, env={**__import__("os").environ, "GIT_TERMINAL_PROMPT": "0"})
    m = re.search(r"^password=(.+)$", r.stdout, re.M)
    if not m:
        sys.exit("No GitHub credential: run `git push` once so Git Credential Manager signs in.")
    return m.group(1).strip()


def _tls() -> ssl.SSLContext:
    """certifi's CA bundle when installed: a fresh Windows fetches root certificates on first use, which Python does not
    trigger - uploads.github.com failed with CERTIFICATE_VERIFY_FAILED while api.github.com worked (T095)."""
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


TLS = _tls()


class GitHub:
    def __init__(self, tok: str):
        self.h = {"Authorization": f"Bearer {tok}", "Accept": "application/vnd.github+json",
                  "X-GitHub-Api-Version": "2022-11-28"}

    def call(self, method, url, data=None, raw=None, ctype=None):
        body = raw if raw is not None else (json.dumps(data).encode() if data is not None else None)
        headers = dict(self.h)
        if ctype:
            headers["Content-Type"] = ctype
        req = urllib.request.Request(url, data=body, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=300, context=TLS) as r:
                return r.status, json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read() or b"{}")
        except urllib.error.URLError as e:  # network / TLS: report it with the draft id instead of a traceback
            return 0, {"message": str(e.reason)}


def notes(ver: str) -> str:
    return f"""## Flash it / แฟลชลงบอร์ด
1. Download **PersonalAIBot-ESP32-v{ver}-factory.bin** below. / ดาวน์โหลดไฟล์ด้านล่าง
2. Open https://espressif.github.io/esptool-js/ in Chrome or Edge → **Connect** (not found? hold BOOT, tap RESET, release BOOT).
   เปิดเว็บด้วย Chrome/Edge → Connect (หาไม่เจอ: กด BOOT ค้าง แตะ RESET แล้วปล่อย BOOT)
3. Flash address **0x0** → choose the file → **Program** → press RESET.
4. Enter your own Wi-Fi + Gemini API key on the device — or put `ai-setup.txt` on the SD card ([template](https://github.com/{REPO}/blob/main/tools/sd_card/ai-setup.txt)).
   ใส่ Wi-Fi + Gemini API key ของคุณบนบอร์ด หรือวาง `ai-setup.txt` ไว้ในการ์ด SD

**📱 From an Android phone / แฟลชจากมือถือ Android:** install **PersonalAIBot-Mobile.apk** below (when attached), open
the board screen (chip icon on top), plug the board into the phone with a USB-C cable (OTG) → **แฟลชลงบอร์ด**.
"อัปเดต" keeps settings, "ติดตั้งใหม่ทั้งหมด" = this factory image.
ติดตั้งแอป PersonalAIBot-Mobile.apk กดไอคอนชิปด้านบนเพื่อเปิดหน้าบอร์ด ESP32 เสียบสาย USB-C จากมือถือเข้าบอร์ด แล้วกด "แฟลชลงบอร์ด" (iPhone ใช้ไม่ได้)
If Android says the app cannot be installed over the old one ("conflicts with an existing package"), the new APK was
signed on another PC: uninstall the old app once, then install. / ถ้าติดตั้งทับแอปเดิมไม่ได้ (เซ็นจาก PC เครื่องอื่น)
ให้ถอนแอปเดิมครั้งเดียวแล้วติดตั้งใหม่ (ข้อมูลในแอปเดิมจะหาย)

> The factory image starts at 0x0 and resets saved settings. A board on v3.5.0 or later updates itself and keeps its
> settings and memory: Settings → อัปเดตเฟิร์มแวร์ (it downloads the `-app.bin` below).
> ไฟล์ factory ล้างค่าตั้งค่าเดิม — บอร์ดที่เป็น v3.5.0 ขึ้นไปอัปเดตเองผ่านเมนู "อัปเดตเฟิร์มแวร์" ได้ ค่าตั้งค่าและความจำอยู่ครบ
>
> **v4.0.0 and later — offline wake word ("Hi ESP" / "Jarvis"):** the flash layout gained a `model` partition. A board
> updated over Wi-Fi from v3.x works as before but wakes on loudness only, until the factory image is flashed once
> over USB (an update over the air cannot change partitions). Memory kept on an SD card is not touched.
> **v4.0.0 ขึ้นไป — คำปลุกออฟไลน์:** มี partition `model` ใหม่ บอร์ดที่อัปเดตผ่าน Wi-Fi จาก v3.x ใช้งานได้ตามเดิมแต่ปลุกด้วย
> ระดับเสียงเท่านั้น จนกว่าจะแฟลชไฟล์ factory ผ่าน USB หนึ่งครั้ง (อัปเดตผ่าน Wi-Fi เปลี่ยน partition ไม่ได้) ความจำบนการ์ด SD ไม่หาย
>
> No passwords or API keys are included in this image. / ไฟล์นี้ไม่มีรหัสหรือ API key ใดๆ
"""


def main() -> None:
    dry = "--dry-run" in sys.argv
    ver = version()
    tag = f"v{ver}"
    if git("status", "--porcelain", "--untracked-files=no"):
        sys.exit("Uncommitted changes: commit and push first, so the release matches the code on GitHub.")
    head = git("rev-parse", "HEAD")
    if git("rev-parse", "origin/main") != head:
        sys.exit("HEAD is not pushed to origin/main yet: run `git push` first.")

    gh = GitHub(token())
    st, _ = gh.call("GET", f"{API}/git/ref/tags/{tag}")
    if st == 200:
        sys.exit(f"Tag {tag} already exists (immutable releases): bump FIRMWARE_VERSION, commit, push, run again.")
    st, rel = gh.call("GET", f"{API}/releases/tags/{tag}")
    if st == 200:
        sys.exit(f"A release {tag} already exists: bump FIRMWARE_VERSION.")
    print(f"Version {ver}, commit {head[:7]}, tag {tag} is free.")
    if dry:
        return

    subprocess.run([sys.executable, str(ROOT / "tools" / "make_release.py")], cwd=ROOT, check=True)
    if "--skip-tests" not in sys.argv:  # after the build: the tests use its ArduinoJson copy
        tests = (["cmd", "/c", str(ROOT / "host_tests" / "run_tests.bat")] if sys.platform == "win32"
                 else ["bash", str(ROOT / "host_tests" / "run_tests.sh")])
        if subprocess.run(tests, cwd=ROOT).returncode != 0:
            sys.exit("Host unit tests failed: nothing was published (--skip-tests only if no compiler is installed).")
    files = [ROOT / "dist" / f"PersonalAIBot-ESP32-{tag}-factory.bin",
             ROOT / "dist" / f"PersonalAIBot-ESP32-{tag}-factory.bin.sha256",
             ROOT / "dist" / f"PersonalAIBot-ESP32-{tag}-app.bin",  # what boards download to update themselves
             ROOT / "dist" / f"PersonalAIBot-ESP32-{tag}-app.bin.sha256"]
    sig = ROOT / "dist" / f"PersonalAIBot-ESP32-{tag}-app.bin.sig"  # boards with the release key require it
    sys.path.insert(0, str(ROOT / "tools"))
    import ota_keygen
    try:
        signed = bool(ota_keygen.header_pem())
    except ota_keygen.KeygenError as e:
        sys.exit(str(e))
    if signed:  # optional (tools/ota_keygen.py); without a key the board checks sha256 + version marker
        files.append(sig)
    for f in files:
        if not f.exists():
            sys.exit(f"missing {f}")
    apk = ROOT / "dist" / "PersonalAIBot-Mobile.apk"  # same name every release: .../releases/latest/download/<it>
    import build_mobile_apk
    if build_mobile_apk.find_sdk():  # fresh phone app for every release (no keystore needed)
        apk_cmd = [sys.executable, str(ROOT / "tools" / "build_mobile_apk.py")]
        if "--skip-tests" in sys.argv:
            apk_cmd.append("--skip-tests")
        if subprocess.run(apk_cmd, cwd=ROOT).returncode != 0:
            sys.exit("Phone app build failed: nothing was published.")
    if apk.exists() and (ROOT / "dist" / (apk.name + ".sha256")).exists():
        files += [apk, ROOT / "dist" / (apk.name + ".sha256")]
    else:
        print("note: no dist/PersonalAIBot-Mobile.apk (python tools/build_mobile_apk.py) - the release has no phone app")

    # a draft left by a failed run (drafts have no tag yet, so the checks above do not see it): start clean
    st, existing = gh.call("GET", f"{API}/releases?per_page=30")
    for r in existing if st == 200 else []:
        if r.get("draft") and r.get("tag_name") == tag:
            gh.call("DELETE", f"{API}/releases/{r['id']}")
            print(f"removed the unpublished draft {tag} left by an earlier run ({len(r.get('assets', []))} files)")

    st, rel = gh.call("POST", f"{API}/releases", {"tag_name": tag, "target_commitish": head,
                                                  "name": f"PersonalAIBot-ESP32 {tag}", "body": notes(ver),
                                                  "draft": True, "generate_release_notes": True})
    if st != 201:
        sys.exit(f"create draft failed: {st} {rel}")
    print("1/3 draft created")
    upload = rel["upload_url"].split("{")[0]
    for f in files:
        ctype = {".bin": "application/octet-stream", ".apk": "application/vnd.android.package-archive"}
        st, a = gh.call("POST", f"{upload}?name={f.name}", raw=f.read_bytes(),
                        ctype=ctype.get(f.suffix, "text/plain"))  # .sha256 / .sig
        if st != 201:
            sys.exit(f"upload {f.name} failed: {st} {a} (draft {rel['id']} left for inspection)")
    print("2/3 files uploaded")
    st, pub = gh.call("PATCH", f"{API}/releases/{rel['id']}", {"draft": False, "make_latest": "true"})
    if st != 200:
        sys.exit(f"publish failed: {st} {pub}")
    print(f"3/3 published: {pub['html_url']}")

    url = next(a["browser_download_url"] for a in pub["assets"] if a["name"].endswith("-factory.bin"))
    with urllib.request.urlopen(url, timeout=300, context=TLS) as r:
        remote = hashlib.sha256(r.read()).hexdigest()
    local = hashlib.sha256(files[0].read_bytes()).hexdigest()
    print(f"download check: {'OK' if remote == local else 'MISMATCH'}  sha256 {remote}")


if __name__ == "__main__":
    main()
