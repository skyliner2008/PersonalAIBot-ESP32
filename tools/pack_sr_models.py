"""
Pack / inspect ESP-SR model packs (srmodels.bin) for the "model" partition (wiki 14, phase 2).

    python tools/pack_sr_models.py list  <srmodels.bin>
    python tools/pack_sr_models.py unpack <srmodels.bin> <out_dir>
    python tools/pack_sr_models.py pack  <out.bin> <model_dir> [<model_dir> ...]

A model dir is named after the model (e.g. wn9_jarvis_tts) and holds its files (_MODEL_INFO_, wn9_data,
wn9_index) as published in github.com/espressif/esp-sr/tree/master/model. Same layout as esp-sr's
model/pack_model.py (re-implemented here, nothing downloaded is executed):

    u32 model_num
    model_num x { char name[32]; u32 file_num; file_num x { char name[32]; u32 start; u32 len } }
    file data, in header order (start = offset from the beginning of the pack)
"""

import struct
import sys
from pathlib import Path


def _name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")


def read_pack(path):
    data = Path(path).read_bytes()
    (count,) = struct.unpack_from("<I", data, 0)
    pos = 4
    models = {}
    for _ in range(count):
        name = _name(data[pos:pos + 32])
        (files,) = struct.unpack_from("<I", data, pos + 32)
        pos += 36
        models[name] = {}
        for _ in range(files):
            fname = _name(data[pos:pos + 32])
            start, length = struct.unpack_from("<II", data, pos + 32)
            pos += 40
            models[name][fname] = data[start:start + length]
    return models


def write_pack(path, models):
    """models: {model_name: {file_name: bytes}} - order kept."""
    for name in list(models) + [f for m in models.values() for f in m]:
        if len(name.encode("ascii")) > 32:
            raise ValueError(f"name longer than 32 bytes: {name}")
    header_len = 4 + sum(36 + 40 * len(files) for files in models.values())
    head = struct.pack("<I", len(models))
    body = b""
    for name, files in models.items():
        head += name.encode("ascii").ljust(32, b"\0") + struct.pack("<I", len(files))
        for fname, blob in files.items():
            head += fname.encode("ascii").ljust(32, b"\0") + struct.pack("<II", header_len + len(body), len(blob))
            body += blob
    assert len(head) == header_len
    Path(path).write_bytes(head + body)
    return len(head) + len(body)


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        for name, files in read_pack(argv[1]).items():
            info = files.get("_MODEL_INFO_", b"").decode("ascii", "replace").strip().splitlines()
            size = sum(len(b) for b in files.values())
            print(f"{name:28s} {size // 1024:6d} KB  {info[-1] if info else ''}")
        return 0
    if len(argv) == 3 and argv[0] == "unpack":
        out = Path(argv[2])
        for name, files in read_pack(argv[1]).items():
            (out / name).mkdir(parents=True, exist_ok=True)
            for fname, blob in files.items():
                (out / name / fname).write_bytes(blob)
        return 0
    if len(argv) >= 3 and argv[0] == "pack":
        models = {}
        for d in map(Path, argv[2:]):
            # esp-sr reads the files by name; keep a stable order (the reference tool used os.walk order)
            models[d.name] = {f.name: f.read_bytes() for f in sorted(d.iterdir()) if f.is_file()}
        size = write_pack(argv[1], models)
        print(f"{argv[1]}: {len(models)} model(s), {size} bytes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
