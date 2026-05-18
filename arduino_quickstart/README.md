# PetBot — Arduino IDE quick start

Single-file sketch, paste-and-flash. Mirrors Phase 1.1 of the
PlatformIO build (AP + camera + web app + `/cmd` echo) with **no
servo / BLE / gait logic** — those land in Step 1.2 and live in the
PlatformIO build.

Use this if you don't want to install PlatformIO yet and just want
to see the bot boot, broadcast a WiFi AP, and serve the live camera.

## What's here

```
arduino_quickstart/
  README.md               ← you are here
  petbot/                 ← body firmware (ESP32-CAM, full app)
    petbot.ino
  petbot_calibrate/       ← stripped-down "find home positions" sketch
    petbot_calibrate.ino
  petbot_c6/              ← head display firmware (ESP32-C6-LCD-1.47)
    petbot_c6.ino
```

**`petbot/petbot.ino`** runs on the body MCU (Freenove ESP32-WROVER CAM
classic, or Freenove ESP32-S3 WROOM CAM with the alt pin block).
Tabbed UI (Move | Calibrate | Settings). WiFi AP, live camera, 12-servo
calibration over PCA9685, NVS-backed home pose, WiFi-mode toggle, FACE
buttons.

**`petbot_calibrate/petbot_calibrate.ino`** is a tiny standalone sketch
for the body that does ONE thing: 12 sliders + release buttons +
"Show home values" → outputs a paste-able C array of pulse-widths.
No camera, no BLE, no tabs. Use this if the main sketch's Calibrate tab
isn't behaving, or if you just want a tight focused calibration session
before flashing the full sketch.

Workflow: flash this → calibrate → tap "Show home values" → copy the
generated `HOME_US[]` block → paste it into `petbot/petbot.ino`
(replace the `HOME_US` array near the top) → re-flash `petbot.ino`.
The dog now boots into your calibrated pose every time.

**`petbot_c6/petbot_c6.ino`** runs on the head display (Waveshare
ESP32-C6-LCD-1.47). Listens on Serial for `FACE:NAME` commands and
renders animated expressions on the onboard ST7789 (172×320). For
testing today: flash via USB-C, open Serial Monitor, type
`FACE:HAPPY` + Enter. Later, wire body TX → C6 GPIO 16 (Serial1 RX)
and uncomment the `Serial1.begin(...)` line in the C6 sketch.

## Setup (classic Freenove ESP32-WROVER CAM)

1. **Install ESP32 board support** in Arduino IDE 2.x:
   - File → Preferences → Additional boards manager URLs, add:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → search "esp32" → install
     **esp32 by Espressif Systems** ≥ 2.0.14.

2. **Board selection** (Tools menu):

   | Setting | Value |
   |---|---|
   | Board | **AI Thinker ESP32-CAM** |
   | Flash Mode | QIO |
   | Flash Frequency | 80MHz |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
   | PSRAM | **Enabled** *(not "OPI PSRAM" — that's S3 only)* |
   | Upload Speed | 921600 |

3. **Open** `arduino_quickstart/petbot/petbot.ino` (File → Open).

4. **Plug in** the Freenove ESP32-WROVER CAM via micro-USB
   (data cable, not charge-only). Tools → Port → pick the new
   `cu.usbserial-*` (mac) / `ttyUSB*` (linux) / `COMx` (win).

5. **Upload**. If it fails with "Failed to connect": hold **BOOT**
   (or short IO0 → GND), tap **RST**, release BOOT, click Upload
   again. After flashing, press **RST** once to start the sketch.

6. **Open Serial Monitor** at **115200 baud**. You should see:

   ```
   === PetBot Phase 1.1 booting ===
   [cam] OV2640 ready
   [wifi] AP: PetBot_xxxx  pw: petbot123  IP: 192.168.4.1
   [http] up
   === PetBot ready ===
   ```

7. **Connect** from your phone / laptop:
   - WiFi settings → join `PetBot_xxxx` (password `petbot123`)
   - Browser → `http://192.168.4.1`

You'll see a live MJPEG feed, a status line that ticks every 2 s,
a hold-to-move D-pad, and face preset buttons. The D-pad just prints
`[cmd] MOVE:fwd` etc. to serial — the servo wiring is Step 1.2.

## Which board do I have?

Look at the metal can on the module:

| Module silk | Board name | USB port | Pin block to use |
|---|---|---|---|
| `ESP32-WROVER-B` / `-IE` | Freenove ESP32-WROVER CAM | micro-USB | **default in the .ino** |
| `ESP32-S3-WROOM-1` | Freenove ESP32-S3 WROOM CAM | USB-C | swap to the commented S3 block at the top of the .ino |

The S3 pin block + the S3 Tools settings are documented in the
header comment of `petbot.ino`. If you flash with the wrong combo
the board will bootloop (camera init hangs forever, watchdog resets
every ~5 seconds).

## When to switch to the PlatformIO build

The single-file sketch tops out at this Phase 1.1 scope. Anything
that needs:

- PCA9685 + 12 leg servos + sliders + "Save home" calibration
- IK + gait (`cooToA` / `move_any`)
- BLE NUS for phone-app control without joining WiFi
- The framed UART protocol to the future C6 head display
- Recognition offload, PS5 controller, autonomous explore

…lives in the modular PlatformIO build at the repo root. Switch
when you're ready; the modules are laid out so adding features
doesn't require touching files you've already gotten working.
