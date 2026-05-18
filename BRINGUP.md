# PetBot — UART transport bring-up checklist

End-to-end smoke test for the S3-CAM ↔ C6 link. Run the steps **in order**.
Don't move on until the success criteria pass.

If you change the transport (UART → USB CDC) later, restart this checklist
from step 1.

---

## Hardware

| Item | Notes |
|------|-------|
| ESP32-S3-CAM (brain) | Any S3-CAM carrier with the OV2640. Must expose its UART pins. |
| ESP32-C6-LCD-1.47 (Waveshare) | Onboard ST7789 + BOOT button. |
| 3 jumper wires | S3-CAM TX → C6 RX, S3-CAM RX → C6 TX, GND ↔ GND |
| USB-UART for serial monitor on each board | Optional but recommended for both |
| Logic analyzer or scope | Optional, useful at step 1 |

UART pins (defaults — change in `petbot_s3.ino` / `petbot_c6.ino` if your carrier conflicts):

| S3-CAM (`firmware/s3_cam_brain/petbot_s3.ino`) | C6 (`firmware/c6_display_client/petbot_c6.ino`) |
|----|----|
| `Serial1`, RX = GPIO18, TX = GPIO17 | `Serial1`, RX = GPIO16, TX = GPIO17 |

Both run **921 600 8N1**. Common ground is mandatory.

The C6's reserved display pins (do not reuse, ever): **6, 7, 14, 15, 21, 22**.

---

## Step 1 — Flash the C6 first, confirm splash and HELLO

1. From the repo root: `pio run -e petbot_c6 -t upload`.
2. Open a serial monitor on the C6's USB CDC port at 115200.
3. Reset the C6.

**Success criteria:**

- The 320×172 ST7789 shows the splash:
  > **PetBot**
  > waiting for brain...
- Serial log prints:
  ```
  === PetBot C6 client booting ===
  === PetBot C6 client ready ===
  ```
- The C6's TX pin is emitting one short burst on boot — that's `PB_HELLO`.
  Verify with a logic analyzer if available, or trust the next step to
  confirm.

**If it fails:**

- Black screen → check the reserved display pins, the backlight pin
  (`PB_LCD_PIN_BL = GPIO22`), and that you flashed `petbot_c6`, not the
  S3 env.
- Splash but no log lines → the USB CDC port may not be the one you
  opened; try the other port if the C6 board has multiple.

---

## Step 2 — Wire and flash the S3, see the root menu appear

1. Wire S3 GPIO17 → C6 RX, S3 GPIO18 → C6 TX, GND ↔ GND.
2. From the repo root: `pio run -e petbot_s3 -t upload`.
3. Open a serial monitor on the S3 at 115200.
4. Reset the S3.

**Success criteria:**

- S3 serial log prints:
  ```
  === PetBot S3 brain booting ===
  [BLE] Advertising as "PetBot"
  === PetBot S3 brain ready ===
  [menu] C6 said HELLO — pushing root menu
  ```
- The C6's screen replaces the splash with a menu titled **PetBot** and
  three rows: `Modes`, `Faces`, `Drive`. The first row is highlighted
  (red bar).

**If it fails:**

- C6 keeps showing the splash → wiring is wrong (TX↔RX swap is the most
  common cause), baud mismatch, or no common ground. Check S3 logs for
  `[s3] frame dropped: CRC mismatch` (a sign of inverted polarity or wrong
  baud).
- C6 shows garbled glyphs → one side is at the wrong baud. Both must be
  921 600.
- S3 log shows `[transport] begin() FAILED` → the build flag selected
  the USB CDC stub. Check `platformio.ini` has `-D PB_TRANSPORT_UART=1`.

---

## Step 3 — Press BOOT on the C6, watch the menu navigate

1. Press the C6's **BOOT** button (short tap).

**Success criteria:**

- S3 serial log prints something like:
  ```
  [c6][lvl=...]   (none yet — only at PB_LOG)
  ```
  …and the menu's highlighted row on the C6 advances by one.
- Each subsequent short tap cycles `Modes → Faces → Drive → Modes → …`.

**If it fails:**

- No movement → the BOOT button GPIO might differ on your C6 carrier.
  Edit `firmware/c6_display_client/petbot_c6.ino` (`kButtons[]`) and
  change the pin to match your board.
- Multiple advances per press → debounce window too short; raise
  `PB_INPUT_DEBOUNCE_MS` in `firmware/c6_display_client/petbot_c6.ino`.

---

## Step 4 — Long-press BOOT to descend into Faces, select Happy

1. Cycle until **Faces** is highlighted.
2. **Long-press** BOOT (≥ 500 ms). Menu replaces with the Faces submenu.
3. Cycle to **Happy**.
4. Long-press BOOT again.

**Success criteria:**

- S3 serial log prints `[menu] activate 'Happy'` and `[face] render mode=1 blink=0`.
- The big face TFT (when wired) shows the happy expression. Until the face
  TFT is wired, the log line is the proof.
- The C6 menu re-renders (the title bar still says **Faces**, selection
  may stay on **Happy**).

**If it fails:**

- Long-press never fires → raise / lower `PB_INPUT_LONGPRESS_MS` in
  `firmware/c6_display_client/petbot_c6.ino`.
- `[face]` line missing → check that `app_state::setExpression()` actually
  called `face_render::onExpressionChanged()` (build error / link order).

---

## Step 5 — Confirm BLE and menu share one code path

1. Pair a phone or laptop to **PetBot** in Chrome / Edge using the BLE
   web app (`web/robot_webapp.html` if you have it, or any Web Bluetooth
   tool that talks NUS).
2. Send the literal string `FACE:SEARCH` over the NUS RX characteristic.

**Success criteria:**

- S3 log prints:
  ```
  [cmd] FACE:SEARCH
  [face] render mode=2 blink=0
  ```
- The C6 menu re-renders (because `app_state::setExpression()` calls
  `menu_controller::onStateChanged()` which pushes a fresh `PB_SET_MENU`).
- The big face TFT shows the search expression (when wired).

This is the proof that BLE and the C6 menu both terminate in
`app_state::setExpression()`. The code path is shared, no duplication.

**If it fails:**

- BLE write succeeds but no S3 log → likely a NimBLE vs classic mismatch.
  Confirm `<NimBLEDevice.h>` exists in your library path; otherwise the
  classic stack is used.

---

## Step 6 — Push a small PNG over `BLOB_PNG_*`

Add a small (≤ 20 KB) PNG fixture to the S3 firmware as a C array (e.g.
generated by `xxd -i fixture.png > fixture_png.h`). Send it from a debug
hook (the easiest spot is to fire it from `menu_controller::onHelloFromC6()`
right after `send_set_menu()`, gated by a `#define PB_DEMO_PNG 1`).

The blob path:

1. S3 emits `PB_BLOB_PNG_BEGIN` with `total_len`, CRC32 of the bytes,
   destination `(x, y, w, h)`.
2. S3 emits N × `PB_BLOB_PNG_CHUNK` (≤ 1022 bytes payload each — chunk
   index increments from 0).
3. S3 emits `PB_BLOB_PNG_END`.

**Success criteria:**

- C6 log shows no `[png_blob]` warnings.
- The image renders at the requested `(x, y)` on the ST7789.

**If it fails:**

- `[png_blob] CRC32 mismatch` → the CRC the S3 sent doesn't match the
  bytes received. Recompute CRC32 against the exact bytes you stream.
- `[png_blob] out-of-order chunk` → the S3 must increment `chunk_idx`
  monotonically from 0, with no gaps.
- `[png_blob] >SRAM cap` → blob > 40 KB; either shrink it or implement
  the SD-card path (TODO in `png_blob.cpp`).

---

## When all six steps pass

The UART transport is fully working and the menu / BLE / face code paths
are unified. You're ready for:

- Phase 2 — wire real motors / face TFT / audio and start removing the
  stubs in `motor_driver.cpp`, `face_render.cpp`, `audio_player.cpp`.
- Task 8 in the working task list — USB-CDC transport. Re-run this
  checklist from step 1 with the build flag flipped to
  `-D PB_TRANSPORT_USBCDC=1` after the S3 host stack is implemented.

Document any board-specific deltas (pin substitutions, button mappings,
buttons added beyond BOOT) in this file as you make them, so the next
person bringing up a fresh board doesn't have to re-derive them.
