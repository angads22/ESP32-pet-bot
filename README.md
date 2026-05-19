# Marvin — ESP32 robot dog

Single-file Arduino sketches for a Freenove ESP32 robot-dog body, with a
Waveshare ESP32-C6-LCD-1.47 as the face display.

```
petbot/             ← body firmware (Freenove ESP32-WROVER CAM)
  petbot.ino          WiFi AP + web UI + camera + 12-servo gait + touch
petbot_c6/          ← head display firmware (Waveshare ESP32-C6-LCD-1.47)
  petbot_c6.ino       Animated kaomoji face, listens on Serial for FACE:NAME
petbot_calibrate/   ← standalone calibration tool
  petbot_calibrate.ino   12 sliders + wiggle + wave demo, no other features
```

## Quick start

### 1. Body — `petbot/petbot.ino`

Board: **Freenove ESP32-WROVER CAM** (classic ESP32 + OV2640/OV3660 camera).

**Wiring**

| Body | → |
|---|---|
| PCA9685 SDA | ESP32 GPIO 13 |
| PCA9685 SCL | ESP32 GPIO 14 |
| PCA9685 VCC | 3.3 V |
| PCA9685 V+  | 5–6 V *regulated* supply (not raw battery) |
| Touch sensor (TTP223) OUT | GPIO 15 |
| All GNDs tied together | — |

Servos plug into PCA9685 channels:
- **FL** hip=3,  thigh=1,  calf=2
- **FR** hip=15, thigh=14, calf=13
- **BL** hip=7,  thigh=6,  calf=5
- **BR** hip=8,  thigh=9,  calf=10

**Arduino IDE Tools settings**

- Board: **AI Thinker ESP32-CAM**
- Flash Mode: QIO
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
- PSRAM: **Enabled** (not "OPI PSRAM" — that's for ESP32-S3 boards)
- Upload Speed: 921600

**Libraries** (install via Tools → Manage Libraries…)

- Adafruit PWM Servo Driver Library

**Flash and connect**

1. Upload `petbot.ino`. If upload fails: hold BOOT, tap RST, release BOOT, click Upload again.
2. Phone WiFi → join **`PetBot_xxxx`** (password `petbot123`)
3. Browser → `http://192.168.4.1`

**Tabs**

| Tab | What |
|---|---|
| Move | Live camera, joystick (walk magnitude < 0.7, run > 0.7), Stand / Stop |
| Face | 18 kaomoji emotions sent to the C6 head |
| Calibrate | 12 sliders grouped by leg, release / save / reload |
| Settings | Switch between AP mode and joining your home WiFi |

### 2. Head display — `petbot_c6/petbot_c6.ino`

Board: **Waveshare ESP32-C6-LCD-1.47**.

**Arduino IDE Tools settings**

- Board: **ESP32C6 Dev Module**
- USB CDC On Boot: Enabled
- Partition Scheme: Default 4MB
- Upload via the C6's USB-C port

**Libraries**

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library

Flash, open Serial Monitor at 115200, type `FACE:HAPPY` + Enter → screen
changes. Commands: `FACE:IDLE / HAPPY / SAD / CRY / ANGRY / LOVE /
SLEEP / SEARCH / CURIOUS / WALK / RUN / TABLE_FLIP / SURPRISED / EXCITED
/ COOL / EMBARRASSED / DIZZY / WINK / BLINK`, plus `PING`.

### 3. Wiring the bot → head over UART (later)

Both sketches have a commented `Serial1.begin(...)` / `Serial2` line. When
you want the bot to drive the C6 directly (instead of pasting `FACE:`
commands manually), wire:

- Body GPIO 4 (TX) → C6 GPIO 16 (Serial1 RX)
- Body GND → C6 GND

…and uncomment the matching lines in both sketches. The body already
prints `FACE:` lines to Serial on every state change.

### 4. Calibration helper — `petbot_calibrate/petbot_calibrate.ino`

If you ever need to find the home pose again (after re-assembling, a
servo swap, etc.):

1. Flash `petbot_calibrate.ino` (same board / Tools settings as `petbot.ino`).
2. Join WiFi `PetBot_Cal` (`petbot123`) → `http://192.168.4.1`.
3. Use **Wiggle** if you forget which channel is which.
4. Slide each joint to neutral, tap **Wave demo** to verify clean lift on
   each leg, tap **Show home values**.
5. Copy the resulting `HOME_US[]` block into `petbot.ino` near the top.

### Reset WiFi back to AP

If you switched to "Home WiFi" and can't reach Marvin: power off, hold
**BOOT**, power back on, keep holding 3 s. WiFi config is wiped → next
boot is AP mode.

## Status

- Body: WiFi AP + web UI ✓, calibration ✓, joystick gait engine ✓,
  touch sensor ✓, camera (needs `esp32` board package 2.0.14+ for
  OV3660 PID recognition) — falls back gracefully if camera init fails.
- Head: 18 kaomoji faces with idle glance + blink + yawn animations,
  bot ↔ head UART link awaiting physical wiring.

## Known issues

- **OV3660 camera "not supported"**: update Arduino IDE's "esp32 by
  Espressif Systems" to **3.x** via Boards Manager. Earlier versions
  only included the OV2640 chip ID in the bundled camera driver.
- **Joystick walking direction is wrong**: flip the relevant entries
  in `LEG_SIGN[4]` at the top of `petbot.ino`. The hip / thigh / calf
  signs are calibrated guesses based on the home-pose mirror pattern;
  they're easy to flip per leg if the dog walks sideways or backwards.
