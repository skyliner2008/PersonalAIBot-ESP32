#!/usr/bin/env python3
"""
Build a single "factory" firmware image for a GitHub release.

    python tools/make_release.py            -> dist/PersonalAIBot-ESP32-v<ver>-factory.bin (+ .sha256)

The image (bootloader + partition table + boot_app0 + app, merged at 0x0) can be flashed by anyone
without PlatformIO, e.g. with https://espressif.github.io/esptool-js/ (offset 0x0) or
    esptool.py --chip esp32s3 write_flash 0x0 PersonalAIBot-ESP32-v<ver>-factory.bin

Safety: refuses to build while include/secrets.h holds real values (they would be compiled into the
image) and scans the result for API-key-like strings. Wi-Fi / API key are entered on the device.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENV = "esp32s3"
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


def version() -> str:
    text = (ROOT / "src" / "app" / "AppConfig.h").read_text(encoding="utf-8")
    return re.search(r'FIRMWARE_VERSION\s+"([^"]+)"', text).group(1)


def main() -> None:
    check_secrets_header()
    pio = find_pio()
    subprocess.run([pio, "run", "-e", ENV], cwd=ROOT, check=True)

    boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    esptool = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
    DIST.mkdir(exist_ok=True)
    out = DIST / f"PersonalAIBot-ESP32-v{version()}-factory.bin"
    subprocess.run([sys.executable, str(esptool), "--chip", "esp32s3", "merge_bin", "-o", str(out),
                    "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
                    "0x0", str(BUILD / "bootloader.bin"), "0x8000", str(BUILD / "partitions.bin"),
                    "0xe000", str(boot_app0), "0x10000", str(BUILD / "firmware.bin")], check=True)

    data = out.read_bytes()
    if KEY_RE.search(data):
        out.unlink()
        sys.exit("An API-key-like string was found in the image - release deleted. Check include/secrets.h.")
    digest = hashlib.sha256(data).hexdigest()
    (out.parent / (out.name + ".sha256")).write_text(f"{digest}  {out.name}\n", encoding="ascii")
    print(f"\nOK  {out}  ({len(data) / 1024:.0f} KB)\nsha256 {digest}\nNo secrets found in the image.")


if __name__ == "__main__":
    main()
