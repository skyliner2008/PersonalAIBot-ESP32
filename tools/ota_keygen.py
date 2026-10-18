#!/usr/bin/env python3
"""
Create the release signing key for updates over Wi-Fi (ECDSA P-256) - run ONCE, on the PC that publishes releases.

    python tools/ota_keygen.py              new key pair: private -> ~/.pab-esp32/ota_signing_key.pem,
                                            public -> src/app/OtaPublicKey.h (commit that file)
    python tools/ota_keygen.py --header     only rewrite src/app/OtaPublicKey.h from the existing private key
                                            (a fresh clone, or the header was reset)
    ESP32-AI Studio, tab 🔧 เฟิร์มแวร์          the same with buttons (create / back up / restore)

The private key never goes into the repository. Set PAB_OTA_SIGNING_KEY to keep it somewhere else.
Firmware built with the public key installs only updates signed with the private one (tools/make_release.py
signs the -app.bin). KEEP A BACKUP of the private key (password manager / USB stick): without it, boards that
carry the public key cannot update over Wi-Fi any more - only a USB flash of a new image fixes that.

Needs: pip install cryptography
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "app" / "OtaPublicKey.h"


class KeygenError(Exception):
    """Why a step was refused (the message is shown as is: command line or ESP32-AI Studio)."""


def key_path() -> Path:
    return Path(os.environ.get("PAB_OTA_SIGNING_KEY", Path.home() / ".pab-esp32" / "ota_signing_key.pem"))


def have_crypto() -> bool:
    try:
        import cryptography  # noqa: F401
    except ImportError:
        return False
    return True


def crypto():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError:
        raise KeygenError("pip install cryptography") from None
    return serialization, ec


def public_pem(private_key) -> str:
    serialization, _ = crypto()
    return private_key.public_key().public_bytes(serialization.Encoding.PEM,
                                                 serialization.PublicFormat.SubjectPublicKeyInfo).decode("ascii")


def load_private(path: Path):
    serialization, ec = crypto()
    try:
        key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    except (ValueError, TypeError) as e:
        raise KeygenError(f"{path} is not a PEM private key without a password ({e})") from None
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise KeygenError(f"{path} is not an ECDSA P-256 private key")
    return key


def header_pem(text: str | None = None) -> str:
    """The public key compiled into the firmware ("" = none yet)."""
    import re
    text = HEADER.read_text(encoding="utf-8") if text is None else text
    m = re.search(r"#define\s+OTA_SIGNING_PUBKEY_PEM[\s\\]+((?:\"(?:[^\"\\]|\\.)*\"[\s\\]*)+)", text)
    if not m:
        raise KeygenError(f"{HEADER} has no OTA_SIGNING_PUBKEY_PEM")
    parts = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(1))
    return "".join(parts).replace("\\n", "\n")


def render_header(pem: str) -> str:
    """src/app/OtaPublicKey.h with `pem` as OTA_SIGNING_PUBKEY_PEM (read back before it is used)."""
    import re
    text = HEADER.read_text(encoding="utf-8")
    lines = "".join(f'    "{line}\\n" \\\n' for line in pem.strip().splitlines())
    define = "#define OTA_SIGNING_PUBKEY_PEM \\\n" + lines.rstrip(" \\\n") + "\n"
    new = re.sub(r"#define\s+OTA_SIGNING_PUBKEY_PEM[\s\\]+(?:\"(?:[^\"\\]|\\.)*\"[\s\\]*)+", lambda _: define, text, count=1)
    if header_pem(new).strip() != pem.strip():
        raise KeygenError("internal error: the header does not read back the same key - nothing written")
    return new


def status() -> dict:
    """What is set up: key (path, exists), header_key (a public key is compiled in), match (None = unknown)."""
    path = key_path()
    st = {"key": path, "exists": path.exists(), "header_key": bool(header_pem()), "match": None, "crypto": have_crypto()}
    if st["exists"] and st["header_key"] and st["crypto"]:
        try:
            st["match"] = public_pem(load_private(path)).strip() == header_pem().strip()
        except KeygenError:
            st["match"] = False
    return st


def write_header_from_key() -> Path:
    """src/app/OtaPublicKey.h from the existing private key (a fresh clone, or the header was reset)."""
    path = key_path()
    if not path.exists():
        raise KeygenError(f"no private key at {path}")
    HEADER.write_text(render_header(public_pem(load_private(path))), encoding="utf-8", newline="\n")
    return path


def create_key() -> Path:
    """New key pair: private -> key_path() (never replaces one), public -> src/app/OtaPublicKey.h."""
    path = key_path()
    if path.exists():
        raise KeygenError(f"{path} already exists - not replaced (boards trust its public key).\n"
                          "Use --header to rewrite src/app/OtaPublicKey.h from it.")
    if header_pem():
        raise KeygenError(f"{HEADER.relative_to(ROOT)} already holds a public key but {path} is missing.\n"
                          "Restore the private key from your backup (or set PAB_OTA_SIGNING_KEY). A new key would lock out\n"
                          "every board that carries the old one.")
    serialization, ec = crypto()
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    header = render_header(public_pem(key))  # before anything is written
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    HEADER.write_text(header, encoding="utf-8", newline="\n")
    return path


def restore_key(backup: Path) -> Path:
    """Copy a backed-up private key back to key_path(): it must be P-256 and match the compiled-in public key."""
    key = load_private(backup)
    compiled = header_pem()
    if compiled and public_pem(key).strip() != compiled.strip():
        raise KeygenError(f"{backup} does not match the public key in {HEADER.relative_to(ROOT)}")
    path = key_path()
    if path.exists():
        if public_pem(load_private(path)).strip() == public_pem(key).strip():
            return path  # already there
        raise KeygenError(f"{path} already holds another key - not replaced")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(backup.read_bytes())
    return path


def main() -> None:
    try:
        if "--header" in sys.argv:
            path = write_header_from_key()
            print(f"{HEADER.relative_to(ROOT)} written from {path}")
            return
        path = create_key()
    except KeygenError as e:
        sys.exit(str(e))
    print(f"private key: {path}  (BACK IT UP - never commit it)")
    print(f"public key : {HEADER.relative_to(ROOT)}  (commit this)")
    print("From the next release on, boards running that firmware install only updates signed with this key.")


if __name__ == "__main__":
    main()
