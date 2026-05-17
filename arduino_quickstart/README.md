# PetBot — Arduino IDE quick start

Single-file sketch, paste-and-flash. Mirrors Phase 1.1 of the
PlatformIO build at the repo root (AP + camera + web app + `/cmd`
echo), with **no servo / BLE / gait logic** — those land in Step 1.2
and live in the PlatformIO build.

Use this if you don't want to install PlatformIO yet and just want
to see the bot boot, broadcast a WiFi AP, and serve the live camera.

## What's here

```
arduino_quickstart/
  README.md            ← you are here
  petbot/
    petbot.ino         ← open this in Arduino IDE
```

## Setup

1. **Install ESP32 board support** in Arduino IDE 2.x:
   - File → Preferences → Additional boards manager URLs, add:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → search "esp32" → install
     **esp32 by Espressif Systems** ≥ 2.0.14.

2. **Board selection** (Tools menu):

   | Setting | Value |
   |---|---|
   | Board | **ESP32S3 Dev Module** (not "AI Thinker ESP32-CAM") |
   | USB CDC On Boot | Enabled |
   | CPU Frequency | 240MHz (WiFi) |
   | Flash Mode | QIO 80MHz |
   | Flash Size | 8MB (64Mb) — or 4MB if your variant is smaller |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
   | PSRAM | **OPI PSRAM** |
   | Upload Speed | 921600 |

3. **Open** `arduino_quickstart/petbot/petbot.ino` (File → Open).

4. **Plug in** the Freenove ESP32-S3 WROOM CAM with a USB-C **data**
   cable (not charge-only). Tools → Port → pick the new
   `cu.usbmodem*` (mac) / `ttyACM*` (linux) / `COMx` (win).

5. **Upload**. If it fails with "Failed to connect": hold BOOT,
   tap RESET, release BOOT, click Upload again.

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

## When to switch to the PlatformIO build

The single-file sketch tops out at this Phase 1.1 scope. Anything
that needs:

- PCA9685 + 12 leg servos + sliders + "Save home" calibration
- IK + gait (`cooToA` / `move_any`)
- BLE NUS for phone-app control without joining WiFi
- The framed UART protocol to the future C6 head display
- Recognition offload, PS5 controller, autonomous explore

…lives in the modular PlatformIO build at the repo root (env
`petbot_s3_ap` mirrors this sketch's behaviour, plus the rest of the
plan grafts on cleanly). Switch when you're ready; the modules are
laid out so adding features doesn't require touching files you've
already gotten working.
