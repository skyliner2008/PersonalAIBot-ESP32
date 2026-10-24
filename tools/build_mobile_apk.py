#!/usr/bin/env python3
"""
Build the phone app (tools/mobile_app: "Personal AI Bot" = the Jarvis assistant + the ESP32 board screen with the
console, USB flasher and GPS navigation for the board) -> dist/PersonalAIBot-Mobile.apk (+ .sha256, + .json: the
version / sha256 / signer the app's own update check reads).

    python tools/build_mobile_apk.py                    ESP32 module unit tests + release APK
    python tools/build_mobile_apk.py --debug            debug APK (debuggable, for testing)
    python tools/build_mobile_apk.py --create-keystore  optional: a keystore of your own (then back it up)
    python tools/build_mobile_apk.py --skip-tests

Needs a JDK 17-24 (found in JAVA_HOME, Android Studio's jbr, ~/.gradle/jdks, ...; Gradle provisions the JDK 21 the
build itself runs on) and the Android SDK (Android Studio installs both; ANDROID_HOME, or tools/mobile_app/
local.properties with sdk.dir=...). tools/publish_release.py attaches dist/PersonalAIBot-Mobile.apk to the release
when it is there.

Local-only, git-ignored inputs (the build works without them): composeApp/google-services.json (Firebase / Vertex AI
provider), composeApp/libs/godot-lib.template_release.aar (else Gradle takes Godot from Maven Central) and
composeApp/src/androidMain/godot-assets/ (the 3D robot of Avatar mode, exported from the PersonalAIBot repo's
pet_godot/; without it the app shows the robot's 2D faces).

Signing: without a keystore of your own the release APK is signed with the Android SDK's debug key
(~/.android/debug.keystore, made by the SDK). An APK update installs only over an APK signed with the same key, so
updates keep working while you build on this PC; from another PC, uninstall the app on the phone once.
An own keystore (--create-keystore) lives OUTSIDE the repository (~/.pab-esp32/flasher-release.jks +
flasher-keystore.properties, or PAB_FLASHER_KEYSTORE_PROPS) and is created only when you run that yourself.
"""

from __future__ import annotations

import hashlib
import json
import os
import secrets
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tools" / "mobile_app"
DIST = ROOT / "dist"
OUT = DIST / "PersonalAIBot-Mobile.apk"
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


def java_major(home: Path) -> int:
    """Major version from <home>/release (JAVA_VERSION="21.0.10"), 0 if unknown."""
    try:
        text = (home / "release").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0
    for line in text.splitlines():
        if line.startswith("JAVA_VERSION="):
            v = line.split("=", 1)[1].strip('"')
            return int(v.split(".")[0]) if v.split(".")[0].isdigit() else 0
    return 0


def find_jdk() -> str | None:
    """A JDK 17-24 for Gradle 8.14 (Android Studio now ships JDK 25 in jbr, which Gradle 8.14 cannot run on)."""
    homes = [Path(os.environ["JAVA_HOME"])] if os.environ.get("JAVA_HOME") else []
    homes += [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Android" / "Android Studio" / "jbr"]
    for parent in (Path.home() / ".gradle" / "jdks", Path.home() / ".jdks",
                   Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Eclipse Adoptium",
                   Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Java"):
        if parent.is_dir():
            homes += sorted((p for p in parent.iterdir() if p.is_dir()), reverse=True)
    return next((str(h) for h in homes if 17 <= java_major(h) <= 24), None)


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
                    "-storepass", pw, "-keypass", pw, "-dname", "CN=Personal AI Bot"], check=True)
    props.write_text(f"storeFile={store.as_posix()}\nstorePassword={pw}\nkeyAlias=flasher\nkeyPassword={pw}\n",
                     encoding="utf-8")
    print(f"OK  {store}\n    {props}\nBack both files up: phones only take updates signed with this key.")


def main() -> None:
    if "--create-keystore" in sys.argv:
        create_keystore()
        return
    debug = "--debug" in sys.argv
    sdk = find_sdk()
    if not sdk:
        sys.exit("Android SDK not found: install Android Studio, or set ANDROID_HOME.")
    jdk = find_jdk()
    if not jdk:
        sys.exit("No JDK 17-24 found (Gradle 8.14 cannot run on newer ones): install JDK 21, or set JAVA_HOME.")
    env = dict(os.environ, ANDROID_HOME=sdk, JAVA_HOME=jdk)
    gradlew = APP / ("gradlew.bat" if sys.platform == "win32" else "gradlew")
    tasks = [] if "--skip-tests" in sys.argv else [":esp32:testDebugUnitTest"]
    tasks.append(":composeApp:assembleDebug" if debug else ":composeApp:assembleRelease")
    cmd = ([str(gradlew)] if sys.platform == "win32" else ["sh", str(gradlew)]) + tasks
    if subprocess.run(cmd, cwd=APP, env=env).returncode != 0:
        sys.exit("Gradle build failed.")
    kind = "debug" if debug else "release"
    key = "own keystore" if props_path().exists() and not debug else "Android debug key"
    apk = APP / "composeApp" / "build" / "outputs" / "apk" / kind / f"PersonalAIBot-{kind}.apk"
    if not apk.exists():
        sys.exit(f"missing {apk}")
    DIST.mkdir(exist_ok=True)
    shutil.copyfile(apk, OUT)
    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    (DIST / (OUT.name + ".sha256")).write_text(f"{digest}  {OUT.name}\n", encoding="ascii")
    write_manifest(sdk, digest, env)
    print(f"\nOK  {OUT}  ({OUT.stat().st_size:,} bytes, {kind}, {key})\n    sha256 {digest}")


def app_version() -> tuple[str, int]:
    """mobileVersionName / mobileVersionCode from tools/mobile_app/gradle.properties (what the APK carries)."""
    props = dict(line.split("=", 1) for line in (APP / "gradle.properties").read_text(encoding="utf-8").splitlines()
                 if "=" in line and not line.lstrip().startswith("#"))
    return props["mobileVersionName"].strip(), int(props["mobileVersionCode"].strip())


def signer_sha256(sdk: str, apk: Path, env: dict) -> str | None:
    """SHA-256 of the APK's signing certificate (apksigner from the SDK build-tools, needs JAVA_HOME), lowercase hex."""
    tools = sorted((Path(sdk) / "build-tools").glob("*"), key=lambda p: p.name)
    for bt in reversed(tools):
        exe = bt / ("apksigner.bat" if sys.platform == "win32" else "apksigner")
        if exe.exists():
            r = subprocess.run([str(exe), "verify", "--print-certs", str(apk)], capture_output=True, text=True, env=env)
            for line in r.stdout.splitlines():
                if "certificate SHA-256 digest:" in line:
                    return line.rsplit(":", 1)[1].strip().replace(":", "").lower()
    return None


def write_manifest(sdk: str, digest: str, env: dict) -> None:
    """
    dist/PersonalAIBot-Mobile.json, attached to the release next to the APK: the app's own update check
    (Settings -> อัปเดตแอป, T097) reads the version from here - the release tag is the firmware's - and the signer,
    so a phone whose app was signed on another PC learns it must uninstall first before downloading ~200 MB.
    """
    name, code = app_version()
    manifest = {"versionName": name, "versionCode": code, "sha256": digest, "size": OUT.stat().st_size,
                "file": OUT.name}
    signer = signer_sha256(sdk, OUT, env)
    if signer:
        manifest["signerSha256"] = signer
    else:
        print("note: apksigner not found - the update manifest has no signer (the app then skips that check)")
    (DIST / "PersonalAIBot-Mobile.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
