#!/usr/bin/env bash
# Builds and runs the native tests against the REAL firmware/MORPHEUS/*.cpp
# modules they cover (host g++, not the ESP32 toolchain). Run from
# anywhere - paths below are resolved relative to this script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

g++ -std=c++17 -Wall -Wextra \
  -I "$SCRIPT_DIR/arduino_stub" \
  -I "$REPO_ROOT/firmware/MORPHEUS" \
  "$SCRIPT_DIR/test_core_decoder.cpp" \
  "$REPO_ROOT/firmware/MORPHEUS/core_decoder.cpp" \
  -o "$OUT_DIR/test_core_decoder"

"$OUT_DIR/test_core_decoder"

g++ -std=c++17 -Wall -Wextra \
  -I "$SCRIPT_DIR/arduino_stub" \
  -I "$REPO_ROOT/firmware/MORPHEUS" \
  "$SCRIPT_DIR/test_core_trainer.cpp" \
  "$REPO_ROOT/firmware/MORPHEUS/core_trainer.cpp" \
  -o "$OUT_DIR/test_core_trainer"

"$OUT_DIR/test_core_trainer"

g++ -std=c++17 -Wall -Wextra \
  -I "$SCRIPT_DIR/arduino_stub" \
  -I "$REPO_ROOT/firmware/MORPHEUS" \
  "$SCRIPT_DIR/test_core_keyer.cpp" \
  "$REPO_ROOT/firmware/MORPHEUS/core_keyer.cpp" \
  -o "$OUT_DIR/test_core_keyer"

"$OUT_DIR/test_core_keyer"
