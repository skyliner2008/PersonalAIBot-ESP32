#!/usr/bin/env python3
"""
Build a single "factory" firmware image for a GitHub release.

    python tools/make_release.py            -> dist/PersonalAIBot-ESP32-v<ver>-factory.bin (+ .sha256)
                                               dist/PersonalAIBot-ESP32-v<ver>-app.bin     (+ .sha256)

The app image (the application alone, for the next OTA slot) is what a board downloads when it
updates itself (src/system/OtaUpdater.cpp): its settings and memory stay. With a release key in
src/app/OtaPublicKey.h (tools/ota_keygen.py) it is also signed: dist/...-app.bin.sig (ECDSA P-256,
base64 DER), which boards built with that key require.

The image (bootloader + partition table + boot_app0 + app + the ESP-SR wake word models in the "model"
partition, merged at 0x0) can be flashed by anyone
without PlatformIO, e.g. with https://espressif.github.io/esptool-js/ (offset 0x0) or
    esptool.py --chip esp32s3 write_flash 0x0 PersonalAIBot-ESP32-v<ver>-factory.bin

Safety: refuses to build while include/secrets.h holds real values (they would be compiled into the
image) and scans the result for API-key-like strings. Wi-Fi / API key are entered on the device.
"""

from __future__ import annotations

import base64
import csv
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENV = "esp32s3-ble"  # T118: the release has OBD over Bluetooth LE (the user's choice; "esp32s3" = without)
BUILD = ROOT / ".pio" / "build" / ENV
DIST = ROOT / "dist"
PLACEHOLDERS = {"", "YourWiFiName", "YourWiFiPassword", "your-gemini-api-key-here"}
KEY_RE = re.compile(rb"AIza[0-9A-Za-z_\-]{30,}|AQ\.[A-Za-z0-9_\-]{30,}")


def find_pio() -> str:
    for c in (shutil.which("pio"), shutil.which("platformio"),
              str(Path.home() / ".platformio/penv/Scripts/pio.exe"), str(Path.home() / ".platformio/penv/bin/pio")):
        if c and Path(c).exists():
            return c
    sys.exit("PlatformIO (pio) not found")


def check_secrets_header() -> None:
    header = ROOT / "include" / "secrets.h"
    if not header.exists():
        return
    for name in ("WIFI_SSID", "WIFI_PASSWORD", "GEMINI_API_KEY"):
        m = re.search(rf'^#define\s+{name}\s+"([^"]*)"', header.read_text(encoding="utf-8", errors="replace"), re.M)
        if m and m.group(1) not in PLACEHOLDERS:
            sys.exit(f"include/secrets.h sets {name}: it would be compiled into the public image.\n"
                     "Rename/remove include/secrets.h (or restore the placeholders) and run again.")


def sr_model() -> list[str]:
    """[offset, path] of the ESP-SR model pack for the "model" partition (wiki 14), or [] without one.
    Same choice as tools/pio_sr_model.py: models/srmodels.bin, else the pack that comes with the framework."""
    ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    m = re.search(r"^board_build\.partitions\s*=\s*(\S+)", ini, re.M)
    table = ROOT / m.group(1) if m else None
    if not table or not table.exists():
        return []
    for row in csv.reader(table.read_text(encoding="utf-8").splitlines()):
        cells = [c.strip() for c in row]
        if len(cells) >= 5 and cells[0] == "model":
            pack = ROOT / "models" / "srmodels.bin"
            if not pack.exists():
                pack = Path.home() / ".platformio/packages/framework-arduinoespressif32-libs/esp32s3/esp_sr/srmodels.bin"
            if not pack.exists():
                sys.exit("The partition table has a model partition but no srmodels.bin was found (wiki 14).")
            if pack.stat().st_size > int(cells[4], 0):
                sys.exit(f"{pack} does not fit the model partition")
            print(f"model partition {cells[3]}: {pack.name} ({pack.stat().st_size // 1024} KB, {pack.parent.name})")
            return [cells[3], str(pack)]
    return []


def version() -> str:
    text = (ROOT / "src" / "app" / "AppConfig.h").read_text(encoding="utf-8")
    return re.search(r'FIRMWARE_VERSION\s+"([^"]+)"', text).group(1)


def sign_app(app: Path) -> None:
    """<app>.sig for boards that carry the release key (src/app/OtaPublicKey.h); none while there is no key."""
    sig_file = app.parent / (app.name + ".sig")
    sig_file.unlink(missing_ok=True)
    sys.path.insert(0, str(ROOT / "tools"))
    import ota_keygen
    try:
        _sign(app, sig_file, ota_keygen)
    except ota_keygen.KeygenError as e:
        sys.exit(str(e))


def _sign(app: Path, sig_file: Path, ota_keygen) -> None:
    public = ota_keygen.header_pem()
    if not public:  # signing is optional: boards without a built-in key check sha256 + version marker
        return
    key_path = ota_keygen.key_path()
    if not key_path.exists():
        sys.exit(f"This firmware carries a release key but the private key is not at {key_path}\n"
                 "(set PAB_OTA_SIGNING_KEY, or restore it from your backup): boards would refuse the update.")
    key = ota_keygen.load_private(key_path)
    if ota_keygen.public_pem(key).strip() != public.strip():
        sys.exit(f"{key_path} does not match the public key in src/app/OtaPublicKey.h")
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    data = app.read_bytes()
    sig = key.sign(data, ec.ECDSA(hashes.SHA256()))  # what OtaVerify.h checks: SHA-256 of the image, DER
    key.public_key().verify(sig, data, ec.ECDSA(hashes.SHA256()))
    sig_file.write_text(base64.b64encode(sig).decode("ascii") + "\n", encoding="ascii")
    print(f"\nOK  {sig_file}  (signed with {key_path.name})")


def main() -> None:
    check_secrets_header()
    pio = find_pio()
    subprocess.run([pio, "run", "-e", ENV], cwd=ROOT, check=True)

    boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    esptool = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
    # esptool 5 (pioarduino) needs packages (rich_click, ...) that only PlatformIO's own Python has
    penv = next((p for p in (Path.home() / ".platformio/penv/Scripts/python.exe",
                             Path.home() / ".platformio/penv/bin/python") if p.exists()), Path(sys.executable))
    esptool_cmd = [str(penv), str(esptool)] if esptool.exists() else [str(penv), "-m", "esptool"]
    DIST.mkdir(exist_ok=True)
    out = DIST / f"PersonalAIBot-ESP32-v{version()}-factory.bin"
    subprocess.run(esptool_cmd + ["--chip", "esp32s3", "merge-bin", "-o", str(out),
                    "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
                    "0x0", str(BUILD / "bootloader.bin"), "0x8000", str(BUILD / "partitions.bin"),
                    "0xe000", str(boot_app0), "0x10000", str(BUILD / "firmware.bin")] + sr_model(), check=True)

    app = DIST / f"PersonalAIBot-ESP32-v{version()}-app.bin"  # over-the-air updates (keeps settings)
    shutil.copyfile(BUILD / "firmware.bin", app)

    for image in (out, app):
        data = image.read_bytes()
        if KEY_RE.search(data):
            out.unlink(missing_ok=True)
            app.unlink(missing_ok=True)
            sys.exit("An API-key-like string was found in the image - release deleted. Check include/secrets.h.")
        digest = hashlib.sha256(data).hexdigest()
        (image.parent / (image.name + ".sha256")).write_text(f"{digest}  {image.name}\n", encoding="ascii")
        print(f"\nOK  {image}  ({len(data) / 1024:.0f} KB)\nsha256 {digest}")
    print("No secrets found in the image.")
    sign_app(app)


if __name__ == "__main__":
    main()
