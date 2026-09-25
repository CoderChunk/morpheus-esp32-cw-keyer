# Testing the MORPHEUS BLE Client

A practical, step-by-step guide to testing the desktop app in
`tools/ble_client/` — word telemetry, in-app pairing, Training, and
Games — against real MORPHEUS hardware.

---

## 1. Prerequisites

- A MORPHEUS board flashed with current firmware (`firmware/MORPHEUS`)
  and BLE turned on:
  **On the OLED:** Menu → Connectivity → Bluetooth → **BLE** toggle → **ON**
  (persists across reboots — one-time step)
- A computer with a real Bluetooth adapter
- Linux, for the full in-app pairing flow (see §3). On Windows/macOS
  you can still test everything except that one dialog — pair once via
  your OS's Bluetooth settings instead, then skip to §4.

---

## 2. Install dependencies

```sh
cd morpheus-esp32-cw-keyer/tools/ble_client
pip install -r requirements.txt
```

On Linux this also installs `dbus-next`, needed for in-app pairing.
Confirm it landed:

```sh
python3 -c "import dbus_next; print('OK')"
```

---

## 3. Test in-app pairing (Linux)

This is the new feature — it should feel like pairing a smartwatch:
everything happens inside the app, no terminal.

### 3a. Start from a clean slate (optional but recommended for a real test)

If your computer has paired with MORPHEUS before, the "Pair New
Device" flow will just confirm the existing bond instead of exercising
the passkey dialog. To force a genuinely fresh pairing:

```sh
bluetoothctl remove <MORPHEUS-CW address>
```

and, on the MORPHEUS OLED: Menu → Connectivity → Bluetooth → **Bond Reset**
(clears *all* remembered devices on the firmware side — fine for a
single-device test bench).

### 3b. Run the app and pair

```sh
python3 morpheus_ble_client.py
```

1. Click the device pill (top bar, next to the MORPHEUS logo) to open
   the connection dialog, then click **Pair New Device**. If it's greyed
   out, `dbus-next` isn't installed or you're not on Linux — see §1.
2. Click **Start Pairing** in the dialog that opens.
3. **Expected:** the dialog shows *"Looking for MORPHEUS-CW..."*, then
   either:
   - A text field: *"Enter the code shown on your MORPHEUS display"* → read
     the 6-digit number off the OLED, type it, click **Submit**, **or**
   - Yes/No buttons: *"Does your MORPHEUS display show: 123456?"* → compare
     against the OLED and click **Yes, it matches**
4. **Expected result:** *"Paired successfully."* within a few seconds.
5. Verify independently (optional):
   ```sh
   bluetoothctl info <address>
   # Paired: yes / Bonded: yes / Trusted: yes
   ```

### What to watch for (real failure modes, not just "did it work")

| Symptom | Likely cause |
|---|---|
| *"MORPHEUS-CW not found"* | BLE toggle is off on the device, or it's already connected elsewhere (only one connection at a time) |
| Dialog hangs on *"Looking for..."* past ~15s | Same as above — discovery timeout |
| *"Pairing failed: ..."* with a DBus error | Wrong code entered, or you clicked **No** — click **Start Pairing** again |
| **Pair New Device** button disabled/greyed | `dbus-next` missing or non-Linux — expected, not a bug (see §1) |

---

## 4. Test the normal connection

Whether you just paired in-app or already had a bond:

1. Click the device pill (top bar) to open the connection dialog, leave
   **Address** blank (auto-discovers by name), and click **Connect**
2. **Expected:** the device pill's dot turns green and shows
   `Connected`; the status bar shows the full
   `Connected: MORPHEUS-CW (XX:XX:XX:XX:XX:XX)` detail, within ~10s
3. Click **Disconnect** in the same dialog — dot turns red, shows
   `Disconnected`

If this hangs on *"Scanning..."*, the same BLE-enabled/already-connected
causes from §3 apply.

---

## 5. Test CW Keyer (word telemetry)

1. Sidebar → **CW Keyer**
2. On the physical device, key a short word on the paddle/straight key
   and let it finish (pause past the word gap)
3. **Expected:** the word is appended to the **Live Transcript** panel,
   the **Mode**/**Words Received**/**Last Word** pills update, and the
   WPM dial shows the word's reported speed.

---

## 6. Test Training

1. Sidebar → **Training**
2. Pick a mode from the dropdown (e.g. **CHARACTERS**), click **Start**
3. **Expected:** a target character appears in the big display; **Stop**
   becomes enabled, **Start** disables, mode dropdown locks
4. Click into the **"HOLD TO KEY"** button (or press **Space** while it
   has focus) — hold briefly for a dit, longer for a dah, release
5. **Expected:** **Correct / Attempts** increments, **Phase** cycles
   PLAYING → LISTENING → FEEDBACK → PLAYING (new target), roughly matching
   what you'd see on the OLED during a real drill
6. Try **EXAM** mode specifically: complete a full exam run (25 chars) and
   confirm the **Exam Result** panel appears with score/pass-fail once
   `phase` reaches `EXAM_DONE`
7. Click **Stop** — session state resets to idle

---

## 7. Test Games

1. Sidebar → **Games**
2. Pick a game (start with **COPY**), click **Start**
3. **Expected:** a falling character appears with a progress bar filling
   up in real time; **Score / Lives / High Score** shown
4. Use the virtual key to answer before the bar fills — **Score** should
   increase on a correct hit; missing should cost a life
5. Try **Pause/Resume** mid-game — the progress bar should freeze, not
   keep advancing while paused
6. Try **Restart** — resets score/lives without leaving the game
7. Let lives reach 0, then click **Confirm** — should restart the game
   from Game Over
8. Repeat briefly for **MEMORY** (chain length / input progress fields)
   and **SPEED** (combo / beat-remaining fields) to confirm each game's
   distinct field set renders correctly
9. Click **Stop**

---

## 8. Sanity-check the placeholder pages

Sidebar → **Statistics**, **Connectivity**, **Profiles**, **Settings**,
**Diagnostics**, **Tools**. Each should show a clear title and a note
explaining what BLE command is still missing to wire it up — not a
blank page, not a crash, not something pretending to work.

---

## 9. Quick regression pass (headless, no hardware)

Useful after any code change, before touching real hardware — catches
import errors, missing signal wiring, and enum/type mistakes:

```sh
cd tools/ble_client
QT_QPA_PLATFORM=offscreen python3 -c "
from PySide6.QtWidgets import QApplication
from morpheus_ble_client import MainWindow
import sys
app = QApplication(sys.argv)
w = MainWindow()
w.show()
w.keyer_page.on_word_received({'word':'CQ','wpm':18,'mode':'STRAIGHT','timestamp':1})
w.training_page.on_train_state({'active':True,'mode':'KOCH','phase':'LISTENING','target':'K','kochLevel':5,'correct':1,'attempts':1,'adaptiveWpm':18,'examScorePercent':0,'examPassed':False,'examCorrect':0,'examTotal':0})
w.games_page.on_game_state({'game':'COPY','active':True,'paused':False,'phase':'FALLING','target':'M','score':0,'lives':3,'highScore':0,'fallProgressPct':10})
print('OK - handlers ran without error')
"
```

This does **not** touch Bluetooth — it drives the page widgets directly
with synthetic data, so it verifies the GUI code itself is sound
independent of hardware being present.

---

## Troubleshooting

- **Only one thing can hold the connection at a time.** Close any other
  app, `bluetoothctl` session, or previous run of this client before
  starting a new test.
- **Command seems to do nothing in Training/Games** → check the status
  bar at the bottom for `Device error: ...` — that's a real error
  response from the firmware (`{"evt":"error",...}`), not a silent no-op.
- **GATT characteristic not found right after a firmware reflash** →
  BlueZ caches the device's characteristic table per-bond. If the
  firmware's BLE service structure changed (new characteristics), the
  cache goes stale. Fix: `bluetoothctl remove <address>` and re-pair
  (§3) — this is expected whenever the firmware's BLE layout changes,
  not a bug in the client.
