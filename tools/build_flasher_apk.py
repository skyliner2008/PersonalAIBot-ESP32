#!/usr/bin/env python3
"""
Build the Android phone flasher (tools/android_flasher, "ESP32-AI Flasher") -> dist/ESP32-AI-Flasher.apk (+ .sha256).

    python tools/build_flasher_apk.py                    unit tests + release APK (signed with your keystore)
    python tools/build_flasher_apk.py --debug            debug APK (no keystore needed, for your own phone)
    python tools/build_flasher_apk.py --create-keystore  once: make the release keystore (then back it up)
    python tools/build_flasher_apk.py --skip-tests

Needs a JDK 17+ and the Android SDK (Android Studio installs both; ANDROID_HOME, or tools/android_flasher/
local.properties with sdk.dir=...). tools/publish_release.py attaches dist/ESP32-AI-Flasher.apk to the release
when it is there.

The release keystore lives OUTSIDE the repository (~/.pab-esp32/flasher-release.jks + flasher-keystore.properties,
or PAB_FLASHER_KEYSTORE_PROPS): an APK update installs only over an APK signed with the same key, so keep a backup.
It is created only when you run --create-keystore yourself.
"""

from __future__ import annotations

import hashlib
import os
import secrets
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tools" / "android_flasher"
DIST = ROOT / "dist"
OUT = DIST / "ESP32-AI-Flasher.apk"
KEY_DIR = Path.home() / ".pab-esp32"


def props_path() -> Path:
    return Path(os.environ.get("PAB_FLASHER_KEYSTORE_PROPS", KEY_DIR / "flasher-keystore.properties"))


def find_sdk() -> str | None:
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(var) and Path(os.environ[var]).is_dir():
            return os.environ[var]
    local = APP / "local.properties"
    if local.exists():
        for line in local.read_text(encoding="utf-8").splitlines():
            if line.startswith("sdk.dir="):
                return line.split("=", 1)[1].replace("\\:", ":").replace("\\\\", "\\")
    guesses = [Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk",
               Path.home() / "Android" / "Sdk", Path.home() / "Library" / "Android" / "sdk"]
    return next((str(g) for g in guesses if g.is_dir()), None)


def create_keystore() -> None:
    props = props_path()
    if props.exists():
        sys.exit(f"{props} already exists - not replaced (an APK update needs the same key).")
    keytool = shutil.which("keytool")
    if not keytool:
        sys.exit("keytool not found: install a JDK 17+ (Android Studio has one in its jbr folder).")
    store = props.parent / "flasher-release.jks"
    if store.exists():
        sys.exit(f"{store} already exists - not replaced.")
    store.parent.mkdir(parents=True, exist_ok=True)
    pw = secrets.token_urlsafe(24)
    subprocess.run([keytool, "-genkeypair", "-keystore", str(store), "-storetype", "PKCS12", "-alias", "flasher",
                    "-keyalg", "EC", "-groupname", "secp256r1", "-validity", "10000",
                    "-storepass", pw, "-keypass", pw, "-dname", "CN=ESP32-AI Flasher"], check=True)
    props.write_text(f"storeFile={store.as_posix()}\nstorePassword={pw}\nkeyAlias=flasher\nkeyPassword={pw}\n",
                     encoding="utf-8")
    print(f"OK  {store}\n    {props}\nBack both files up: phones only take updates signed with this key.")


def main() -> None:
    if "--create-keystore" in sys.argv:
        create_keystore()
        return
    debug = "--debug" in sys.argv
    if not debug and not props_path().exists():
        sys.exit(f"No release keystore ({props_path()}).\n"
                 "  python tools/build_flasher_apk.py --create-keystore   (once, then back it up)\n"
                 "  python tools/build_flasher_apk.py --debug             (an APK for your own phone)")
    sdk = find_sdk()
    if not sdk:
        sys.exit("Android SDK not found: install Android Studio, or set ANDROID_HOME.")
    env = dict(os.environ, ANDROID_HOME=sdk)
    gradlew = APP / ("gradlew.bat" if sys.platform == "win32" else "gradlew")
    tasks = [] if "--skip-tests" in sys.argv else ["testDebugUnitTest"]
    tasks.append("assembleDebug" if debug else "assembleRelease")
    cmd = ([str(gradlew)] if sys.platform == "win32" else ["sh", str(gradlew)]) + tasks
    if subprocess.run(cmd, cwd=APP, env=env).returncode != 0:
        sys.exit("Gradle build failed.")
    kind = "debug" if debug else "release"
    apk = APP / "app" / "build" / "outputs" / "apk" / kind / f"app-{kind}.apk"
    if not apk.exists():
        sys.exit(f"missing {apk} (is the release signing set up?)")
    DIST.mkdir(exist_ok=True)
    shutil.copyfile(apk, OUT)
    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    (DIST / (OUT.name + ".sha256")).write_text(f"{digest}  {OUT.name}\n", encoding="ascii")
    print(f"\nOK  {OUT}  ({OUT.stat().st_size:,} bytes, {kind})\n    sha256 {digest}")


if __name__ == "__main__":
    main()
