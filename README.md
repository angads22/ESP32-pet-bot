# PetBot — ESP32 Firmware

Two-board desktop robot: **ESP32-S3-CAM** (brain) + **ESP32-C6-LCD-1.47**
(thin display client). The S3 owns BLE, WiFi, vision, motors, audio,
and the big face TFT. The C6 mirrors menus / text / icons / PNGs the
S3 sends it and reports button presses back. All phone control flows
`phone → S3 → C6`; the C6 never sees BLE.

See `ROBOT_FIRMWARE_PLAN.md` for the architecture in detail and
`BRINGUP.md` for the end-to-end smoke-test checklist.

---

## Build modes

The repo ships two PlatformIO firmwares from one root `platformio.ini`.

| Env | Board | Source dir | Build flags | Partition | Purpose |
|-----|-------|-----------|-------------|-----------|---------|
| `petbot_s3` | `esp32-s3-devkitc-1` (or your S3-CAM variant) | `firmware/s3_cam_brain/petbot_s3.ino` | *(none)* | `huge_app.csv` | Brain — BLE + WiFi + vision + motors + audio + big face |
| `petbot_s3_wifi` | same | same | `-DPETBOT_ENABLE_WIFI=1` | `huge_app.csv` | Brain with captive-portal web UI |
| `petbot_s3_stream` | same | same | `-DPETBOT_ENABLE_WIFI=1 -DPETBOT_ENABLE_STREAM=1` | `huge_app.csv` | Brain with web UI + MJPEG stream |
| `petbot_c6` | `esp32-c6-devkitc-1` (Waveshare ESP32-C6-LCD-1.47) | `firmware/c6_display_client/petbot_c6.ino` | *(none)* | default | Thin client — ST7789 + input only |

Default transport is UART (`-DPB_TRANSPORT_UART=1`, on by default).
USB-CDC (`-DPB_TRANSPORT_USBCDC=1`) is stubbed — see Task 8 in the
working task list and the TODO block at the top of
`firmware/s3_cam_brain/petbot_s3.ino` and `firmware/c6_display_client/petbot_c6.ino`.

Build commands:

```bash
pio run -e petbot_s3            # S3 brain, BLE-only
pio run -e petbot_s3_wifi -t upload    # S3 brain, BLE + WiFi web UI
pio run -e petbot_c6 -t upload  # C6 thin client
pio device monitor              # serial logs
```

Flash the C6 first, then the S3 — the S3 will start pushing frames as
soon as it sees `PB_HELLO` from the C6 on boot.

---

## Wiring (UART transport, default)

Three wires between the boards:

| S3-CAM | C6-LCD-1.47 |
|--------|-------------|
| `GPIO17 (TX)` | UART RX (any free GPIO clear of the reserved display pins 6, 7, 14, 15, 21, 22) |
| `GPIO18 (RX)` | UART TX (same caveat) |
| `GND` | `GND` |

UART runs at **921600 8N1**. Common ground is non-negotiable.

For USB-CDC transport (later milestone) the S3 hosts a USB CDC port and
the C6's default `Serial` becomes the link — see `BRINGUP.md` and the
TODO comment in `firmware/s3_cam_brain/petbot_s3.ino`.

---

## Phone control (BLE / WiFi)

### BLE — desktop / Android Chrome or Edge

1. Flash `petbot_s3`. It advertises as **PetBot** over BLE NUS.
2. Open `web/robot_webapp.html` in Chrome → **Connect via Bluetooth** → pick **PetBot**.

> Web Bluetooth is **not** supported on iOS / iPadOS / Safari / Firefox. Use the WiFi build for Apple devices.

### WiFi captive portal — any browser

1. Flash `petbot_s3_wifi`. On first boot it raises an AP `PETBOT_SETUP` (password `petbot123`).
2. Join the AP from your phone. Captive portal opens at `192.168.4.1`; if it doesn't, navigate manually.
3. Pick your home WiFi, enter the password, save. The device reconnects to home WiFi.
4. From any device on the same network: `http://petbot.local`.

Hold **GPIO 0 low for 3 s during boot** to wipe saved credentials and re-enter the captive portal.

### Camera stream

Build with `petbot_s3_stream`. Once the device is on home WiFi, the
MJPEG stream is at `http://petbot.local/stream`.

### BLE / web command reference

All commands flow `phone → S3 → C6`. The S3 dispatches them; commands
that affect the C6 surface are translated into protocol frames before
they leave the S3.

| Command | Action |
|---------|--------|
| `MOVE:fwd` / `back` / `left` / `right` / `stop` | Drive |
| `FACE:HAPPY` / `IDLE` / `SEARCH` / `CURIOUS` / `SLEEP` | Big-face mode + matching C6 status update |
| `SAY:<text>` | TTS via I2S amp (when wired) |
| `SOUND:BOOT` / `HAPPY` / `ALERT` | Built-in sound |
| `SCREEN:<text>` | Debug: push one line of `PB_DRAW_TEXT` to the C6 |
| `MODE:manual` / `auto` | Switch between manual control and the S3 state machine |
| `STATUS` | Returns feature-flag report and link health |

---

## Reset WiFi credentials

Hold **GPIO 0** low for **3 seconds** during boot to erase saved
credentials and re-enter setup mode. The serial monitor will confirm:
`[WiFi] Credentials erased — starting setup portal`.

---

## Hardware enables (S3 brain)

Set the matching `#define` to `1` and fill the body in the relevant
module to wire up real hardware. Until then the bot can BLE / web /
stream but cannot drive, speak, or render a face:

- `MOTORS_ENABLED` — motor driver (TB6612 / DRV8833 / L298N)
- `FACE_TFT_ENABLED` — big face TFT on FSPI / SPI3_HOST
- `MIC_ENABLED` — I2S microphone (INMP441 …)
- `SPEAKER_ENABLED` — I2S amplifier (MAX98357A …)

The C6 thin client has no such flags — it always renders whatever the
S3 sends and always polls the BOOT button (and any extra button GPIOs
the firmware is configured for).

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| C6 screen stays black on boot | Check the reserved display pins (`6, 7, 14, 15, 21, 22`) aren't reused; verify `setRotation(1)`; backlight on `GPIO22` |
| C6 screen says "PetBot — waiting" forever | S3 isn't sending; check UART wiring (S3 GPIO17 → C6 RX, S3 GPIO18 → C6 TX, GND), confirm both at 921600 8N1, and check the S3 serial log for `PB_HELLO` reception |
| Buttons on the C6 don't move the menu | The C6 sends `PB_BTN_EVENT`; check the S3 serial log for that frame, and that the menu controller is mapping the button id |
| `PETBOT_SETUP` AP doesn't appear | Compiled with `-DPETBOT_ENABLE_WIFI=1`? Partition `huge_app.csv`? Check `[WiFi]` lines in serial |
| `petbot.local` doesn't resolve | Same network as the bot? Try the IP printed in the serial log |
| BLE picker doesn't show PetBot | Check `[BLE] Advertising` in serial; stay within ~5 m; only Chrome / Edge support Web Bluetooth |
| Compile error about flash size on S3 | Use the `huge_app.csv` partition; `petbot_s3*` envs already do |
