# PlatformIO extra script: keep the builder's home folder (e.g. C:/Users/<name>/...) out of the firmware.
# __FILE__ strings from framework asserts would otherwise embed absolute paths in every published image.
Import("env")  # noqa: F821  (provided by PlatformIO / SCons)

import os

flags = []
for path in {env.subst("$PROJECT_PACKAGES_DIR"), env.subst("$PROJECT_DIR"), os.path.expanduser("~")}:  # noqa: F821
    if not path:
        continue
    for form in {path, path.replace("\\", "/")}:
        flags.append("-fmacro-prefix-map=%s=." % form)
env.Append(CCFLAGS=sorted(flags))  # noqa: F821
