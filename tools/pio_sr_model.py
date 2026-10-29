# PlatformIO extra script (post): flash the ESP-SR models (srmodels.bin) into the "model" partition with
# every USB upload, so the wake word works right after `pio run -t upload` (wiki 14, phase 2).
#   models/srmodels.bin   - our own pack (e.g. "Jarvis"), if present
#   otherwise the pack that comes with the framework (WakeNet "Hi ESP" + VADNet + MultiNet)
# The offset is read from the partition table, so the two cannot disagree.
Import("env")  # noqa: F821  (provided by PlatformIO / SCons)

import csv
import os


def model_offset(table):
    with open(table, newline="") as f:
        for row in csv.reader(f):
            cells = [c.strip() for c in row]
            if len(cells) >= 5 and cells[0] == "model":
                return cells[3], int(cells[4], 0)
    return None, 0


project = env.subst("$PROJECT_DIR")  # noqa: F821
table = os.path.join(project, env.GetProjectOption("board_build.partitions", ""))  # noqa: F821
offset, size = model_offset(table) if os.path.isfile(table) else (None, 0)
if offset:
    own = os.path.join(project, "models", "srmodels.bin")
    if not os.path.isfile(own):  # firmware-p4/ uses the repo's pack too ("Jarvis": the ESP32-P4 SDK pack has only Hi ESP)
        own = os.path.join(project, os.pardir, "models", "srmodels.bin")
    bundled = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "framework-arduinoespressif32-libs",  # noqa: F821
                           env.BoardConfig().get("build.mcu", "esp32s3"), "esp_sr", "srmodels.bin")  # noqa: F821
    image = own if os.path.isfile(own) else bundled
    if not os.path.isfile(image):
        print("[sr-model] no srmodels.bin found - the wake word stays off")
    elif os.path.getsize(image) > size:
        print("[sr-model] %s (%d bytes) does not fit the model partition (%d) - not flashed" %
              (image, os.path.getsize(image), size))
    else:
        env.Append(FLASH_EXTRA_IMAGES=[(offset, image)])  # noqa: F821  (factory / merged image)
        # the builder copied FLASH_EXTRA_IMAGES into the esptool command before this post script ran:
        # without this the upload left the model partition empty (seen on the board)
        if "uploadfs" not in COMMAND_LINE_TARGETS:  # noqa: F821
            env.Append(UPLOADERFLAGS=[offset, image])  # noqa: F821
        print("[sr-model] %s -> %s (%d KB)" % (os.path.basename(os.path.dirname(image)) + "/srmodels.bin", offset,
                                               os.path.getsize(image) // 1024))
