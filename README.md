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

## Freenove ESP32 Dog — GPIO reference

### Which GPIO leads to the servo rug (PCA9685)?

The "rug" is the PCA9685 16-channel PWM servo driver board that all
12 leg servos plug into.  Two I²C lines connect the ESP32-CAM to it:

| ESP32-CAM GPIO | PCA9685 pin | Note |
|---|---|---|
| `GPIO 13` | `SDA` | I²C data — this wire **leads to the rug** |
| `GPIO 14` | `SCL` | I²C clock — this wire **leads to the rug** |
| `3.3 V` | `VCC` | PCA9685 logic power |
| `GND` | `GND` | common ground (also tie servo-battery GND here) |
| external 5–6 V | `V+` | servo motor power — do **not** use raw battery voltage |

### Free GPIOs for expansion

After allocating I²C for the PCA9685, the following GPIOs are
unassigned on the Freenove dog board and available for sensors, UART
to a display board, LEDs, etc.:

| GPIO | Direction | Notes |
|---|---|---|
| `4` | output / input | **recommended TX pin to C6 screen** |
| `15` | output / input | recommended RX pin from C6 screen |
| `21` | output / input | |
| `22` | output / input | |
| `23` | output / input | |
| `32` | output / input | |
| `33` | output / input | |
| `34` | **input only** | no internal pull-up; cannot drive output |
| `35` | **input only** | no internal pull-up |
| `36 (VP)` | **input only** | ADC / battery sense on some Freenove boards |
| `39 (VN)` | **input only** | ADC |

> **Boot-sensitive:** GPIO 0 and GPIO 2 affect the boot mode.  Use them
> with care and never pull them low at power-on.

---

## Wiring (UART transport, default)

Three wires between the boards:

| S3-CAM | C6-LCD-1.47 |
|--------|-------------|
| `GPIO17 (TX)` | UART RX (any free GPIO clear of the reserved display pins 6, 7, 14, 15, 21, 22) |
| `GPIO18 (RX)` | UART TX (same caveat) |
| `GND` | `GND` |

UART runs at **921600 8N1**. Common ground is non-negotiable.

### Connections: Freenove ESP32-CAM board → C6-LCD-1.47

Use free GPIO 4 (TX) and 15 (RX) on the dog board to drive the C6
display.  The C6 sketch already listens on `Serial1 RX=GPIO16, TX=GPIO17`.

| Freenove ESP32-CAM | C6-LCD-1.47 | Purpose |
|---|---|---|
| `GPIO 4` (free TX) | `GPIO 16` (Serial1 RX) | S3 → C6 commands (required) |
| `GPIO 15` (free RX) | `GPIO 17` (Serial1 TX) | C6 → S3 button events (optional) |
| `GND` | `GND` | common ground (required) |

> The C6's **display pins** `6, 7, 14, 15, 21, 22` are wired on-board to
> the ST7789 — do **not** connect anything to those pins.

### C6 GPIO availability (practical)

- **Reserved by onboard display/backlight**: `6, 7, 14, 15, 21, 22`
- **Used by C6 UART screen link in current sketch**: `16, 17`
- Treat remaining exposed GPIO as candidate free pins after checking your
  exact carrier-board schematic.

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
