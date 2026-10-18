#!/usr/bin/env bash
# Render the vehicle dash (src/ui/DashScreen.cpp, unchanged) for a few scenes into PNG files - layout check without
# a board. Linux / macOS; needs g++, SDL2 headers (LovyanGFX's desktop platform: apt install libsdl2-dev) and zlib.
# LovyanGFX + ArduinoJson come from .pio/libdeps (run "pio run" once first).
#   bash tools/dash_preview/run.sh [out-dir]      (default: tools/dash_preview/out)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/tools/dash_preview"
OUT="${1:-$HERE/out}"
LGFX="${LGFX:-$ROOT/.pio/libdeps/esp32s3/LovyanGFX/src}"
AJSON="${AJSON:-$ROOT/.pio/libdeps/esp32s3/ArduinoJson/src}"
BUILD="$HERE/build"
[ -f "$LGFX/LovyanGFX.hpp" ] || { echo "LovyanGFX not found in .pio/libdeps - run \"pio run\" once first." >&2; exit 2; }
mkdir -p "$BUILD" "$OUT"
FLAGS="-O1 -DLGFX_SDL -I $LGFX $(sdl2-config --cflags)"
for f in $(find "$LGFX/lgfx" -name "*.cpp"); do
    o="$BUILD/$(basename "$f").o"; [ -f "$o" ] || g++ -std=c++17 $FLAGS -c "$f" -o "$o"
done
for f in $(find "$LGFX/lgfx" -name "*.c"); do
    o="$BUILD/$(basename "$f").o"; [ -f "$o" ] || gcc $FLAGS -c "$f" -o "$o"
done
g++ -std=c++17 $FLAGS -I "$HERE/stub" -I "$ROOT/src" -I "$AJSON" "$HERE/dash_preview.cpp" \
    "$ROOT/src/ui/DashScreen.cpp" "$ROOT/src/ui/UiManager.cpp" "$ROOT/src/vehicle/DashModel.cpp" \
    "$ROOT/src/vehicle/SimEngine.cpp" "$ROOT/src/vehicle/HondaKline.cpp" \
    "$ROOT/src/display/fonts/ThaiFont20.cpp" "$ROOT/src/display/fonts/ThaiFont26.cpp" \
    "$BUILD"/*.o $(sdl2-config --libs) -lz -lpthread -o "$BUILD/dash_preview"
SDL_VIDEODRIVER=dummy "$BUILD/dash_preview" "$OUT"
