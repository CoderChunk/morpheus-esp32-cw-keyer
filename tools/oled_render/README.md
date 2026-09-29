# OLED host renderer

Renders MORPHEUS's real OLED screens on this machine, without any ESP32
or physical display - for visually checking layout/content changes to
`ui_renderer.cpp`/`ui_screens.cpp` before flashing hardware.

It links the **real, unmodified** `firmware/MORPHEUS/*.cpp` UI and core
logic (everything except `transport.cpp`/`ble_control.cpp`, which need
NimBLE) against the **real** U8g2 library, on the host. Only hardware I/O
(GPIO/LEDC/NVS/BLE) is stubbed - see `host_stub/`. The firmware itself
gets four tiny `#ifdef MORPHEUS_HOST_RENDER`-gated additions (two debug
accessors in `ui_renderer.cpp`/`ui_state.cpp`) that compile to nothing in
a real build; `MORPHEUS_HOST_RENDER` is only ever defined by `build.sh`.

## Build & run

Requires the U8g2 Arduino library (same one `arduino-cli` uses) and
ImageMagick's `convert`.

```sh
./build.sh                                  # -> ./morpheus_oled_render
./morpheus_oled_render                      # walks the whole menu tree
./morpheus_oled_render --max-depth 4         # shallower walk
./morpheus_oled_render --max-shots 50        # cap total screenshots
./morpheus_oled_render /custom/output/dir
```

Output defaults to `GitHub/oled_debug/` - a sibling of both repos, not
inside either one's tracked tree, so screenshots never land in firmware
source or get committed. Point it elsewhere with a positional argument
if you want them somewhere else.

## How it navigates

`render_main.cpp` drives `ui_state.cpp`'s real state machine with
synthetic `UiEvent`s (rotate/select/back - the same vocabulary
`ui_input.cpp` produces from a real encoder/button), BFS over the menu
tree by index path (e.g. `[1, 2]` = carousel item 1, its list's row 2).
Every node is reached by a **fresh replay from HOME**, not by
navigating-then-backing-out: some screens (e.g. `UI_SCREEN_DIAG_INPUT`)
deliberately consume BACK as diagnostic input rather than exiting, so
there's no single "go back" convention that works everywhere. A replay
resets every core module's state each time (not just `ui_state`) so one
path's session (training/tune/game) can't leave a stuck "busy" flag that
blocks a sibling path.

## What this does and doesn't prove

Proves: the real drawing code executes without crashing, and produces
the byte-for-byte buffer content it would send to a real SH1106 panel
for that state - genuinely the same `u8g2_t` buffer format
(`u8g2_ll_hvline_vertical_top_lsb`) the chip receives over I2C.

Doesn't prove: real button/encoder timing, real BLE-driven screens
(BLE status/pairing overlays are never exercised - the BLE stack isn't
linked), or that in-flight session state during a *live* game/exam looks
right frame-to-frame (each capture is a single static snapshot after a
fresh session start, not a running one).
