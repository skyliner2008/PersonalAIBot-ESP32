#!/usr/bin/env bash
# Host unit tests (Linux / macOS / CI) - same sources as run_tests.bat, built with g++ or clang++.
# ArduinoJson is taken from .pio/libdeps (run "pio run" once first).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/host_tests/build"
AJSON="$ROOT/.pio/libdeps/esp32s3/ArduinoJson/src"
CXX="${CXX:-g++}"

if [ ! -f "$AJSON/ArduinoJson.h" ]; then
    echo "ArduinoJson not found in .pio/libdeps - run \"pio run\" once first." >&2
    exit 2
fi
mkdir -p "$OUT"

"$CXX" -std=c++17 -O2 -Wall -Wno-unused-function -I "$ROOT/src" -I "$AJSON" \
    "$ROOT/host_tests/test_main.cpp" \
    "$ROOT/host_tests/test_assistant.cpp" \
    "$ROOT/src/util/Base64.cpp" \
    "$ROOT/src/memory/TextFeatures.cpp" \
    "$ROOT/src/memory/MemoryStore.cpp" \
    "$ROOT/src/memory/EpisodicLog.cpp" \
    "$ROOT/src/agent/AgentTools.cpp" \
    "$ROOT/src/agent/Personas.cpp" \
    "$ROOT/src/agent/Scheduler.cpp" \
    "$ROOT/src/agent/TaskList.cpp" \
    "$ROOT/src/agent/Consolidation.cpp" \
    "$ROOT/src/ai/ModelFallback.cpp" \
    -o "$OUT/host_tests"

"$OUT/host_tests"
