#!/usr/bin/env bash
# Builds morpheus_oled_render: the real firmware/MORPHEUS/*.cpp UI logic
# and drawing code (everything except transport.cpp/ble_control.cpp,
# which need NimBLE/ESP32) linked against the real u8g2 library, on the
# host. See README.md for what this does and doesn't prove.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FW_DIR="$REPO_ROOT/firmware/MORPHEUS"
STUB_DIR="$SCRIPT_DIR/host_stub"

U8G2_DIR="${U8G2_DIR:-$HOME/Arduino/libraries/U8g2/src}"
if [ ! -f "$U8G2_DIR/U8g2lib.h" ]; then
  echo "error: U8g2 Arduino library not found at $U8G2_DIR" >&2
  echo "  install it (Arduino IDE/arduino-cli Library Manager: 'U8g2' by olikraus)" >&2
  echo "  or set U8G2_DIR to point at its src/ directory." >&2
  exit 1
fi

OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

FW_SOURCES=()
for f in "$FW_DIR"/*.cpp; do
  base="$(basename "$f")"
  [ "$base" = "transport.cpp" ] && continue
  [ "$base" = "ble_control.cpp" ] && continue
  FW_SOURCES+=("$f")
done

CLIB_SOURCES=("$U8G2_DIR"/clib/*.c)

echo "Compiling u8g2 clib (${#CLIB_SOURCES[@]} files)..."
CLIB_OBJS=()
for f in "${CLIB_SOURCES[@]}"; do
  obj="$OUT_DIR/$(basename "${f%.c}").o"
  gcc -c -O1 -std=c99 -I "$U8G2_DIR/clib" "$f" -o "$obj"
  CLIB_OBJS+=("$obj")
done

echo "Compiling firmware UI/core sources (${#FW_SOURCES[@]} files) + host glue..."
CXX_SOURCES=("${FW_SOURCES[@]}" "$STUB_DIR/glue.cpp" "$STUB_DIR/transport_stub.cpp" "$SCRIPT_DIR/render_main.cpp")
CXX_OBJS=()
i=0
for f in "${CXX_SOURCES[@]}"; do
  i=$((i + 1))
  obj="$OUT_DIR/$(printf '%03d' "$i")_$(basename "${f%.cpp}").o"
  g++ -c -O1 -std=c++17 -DMORPHEUS_HOST_RENDER \
    -I "$STUB_DIR" -I "$FW_DIR" -I "$U8G2_DIR" -I "$U8G2_DIR/clib" \
    "$f" -o "$obj"
  CXX_OBJS+=("$obj")
done

echo "Linking..."
BIN="$SCRIPT_DIR/morpheus_oled_render"
g++ -O1 "${CXX_OBJS[@]}" "${CLIB_OBJS[@]}" -o "$BIN"

echo "Built: $BIN"
