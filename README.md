# PetBot — ESP32-CAM Firmware

BLE-controlled robot with WiFi-hosted web UI and optional MJPEG camera stream.

---

## Build modes

| Mode | Compile flags | Partition scheme | Works on |
|------|--------------|-----------------|----------|
| **BLE only** (default) | *(none)* | Default | Desktop Chrome/Edge, Android Chrome |
| **WiFi + Web UI** | `-DPETBOT_ENABLE_WIFI=1` | **Huge APP (3MB No OTA)** | Any device with a browser (iPad, iPhone, etc.) |
| **WiFi + Web UI + Camera** | `-DPETBOT_ENABLE_WIFI=1 -DPETBOT_ENABLE_STREAM=1` | **Huge APP (3MB No OTA)** | Same as above |

> Web Bluetooth is not supported on iOS/iPadOS. Use the WiFi build for Apple devices.

---

## Option A — iPad / iPhone / any browser (WiFi build)

### Step 1 — Flash with WiFi enabled

1. Open `firmware/esp32_cam_brain.ino` in Arduino IDE 2.x.
2. Install board support: **Tools → Board → Boards Manager** → search `esp32` by Espressif, install ≥ 2.0.
3. Select **Tools → Board → AI Thinker ESP32-CAM**.
4. Set partition: **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)**.
5. Add build flag: **Sketch → Export Compiled Binary** — or in `platformio.ini` add `build_flags = -DPETBOT_ENABLE_WIFI=1`.
   - In Arduino IDE 2.x: create a `sketch.yaml` alongside the `.ino` with `build_flags: [-DPETBOT_ENABLE_WIFI=1]`
6. Connect ESP32-CAM via USB-UART adapter (IO0 → GND for flash mode), click **Upload**.
7. Remove IO0 jumper and press reset.

### Step 2 — Get WiFi credentials from BLE

The bot always advertises BLE. When you connect, it immediately sends the WiFi info.

1. Open **nRF Connect** (free on App Store / Google Play).
2. Scan and connect to **PetBot**.
3. Subscribe to the TX characteristic (`6E400003…`).
4. You will receive: `WIFI:PETBOT_CAM:petbot123:http://192.168.4.1`

### Step 3 — Join the hotspot and open the UI

1. Go to **Settings → Wi-Fi** on your iPad and join **PETBOT_CAM** (password: `petbot123`).
2. Open **Safari** and navigate to **http://192.168.4.1**.
3. The PetBot control UI loads — tap the directional buttons to drive.

---

## Option B — Desktop / Android (BLE build)

> Requires Chrome 56+ or Edge 79+. Safari and Firefox do **not** support Web Bluetooth.

### Step 1 — Flash (default build, no extra flags)

1–4 same as above but **keep the default partition scheme** and no extra build flags.

### Step 2 — Verify BLE advertising

Open Serial Monitor at **115200 baud**. You should see:

```
=== PetBot booting ===
[BLE] Advertising as "PetBot"
=== PetBot ready ===
```

### Step 3 — Open and connect

1. Serve the web app locally: `python3 -m http.server 8080` from the repo root.
2. Open **http://localhost:8080/web/robot_webapp.html** in Chrome or Edge.
3. Click **Connect** — select PetBot from the Bluetooth picker.

---

## Commands

| Command | Action |
|---------|--------|
| `MOVE:fwd` | Drive forward |
| `MOVE:back` | Drive backward |
| `MOVE:left` | Turn left |
| `MOVE:right` | Turn right |
| `MOVE:stop` | Stop |
| `SAY:<text>` | Speak text (requires speaker hardware) |
| `SOUND:<name>` | Play named sound |
| `STATUS` | Returns feature flags |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| nRF Connect doesn't show PetBot | Check Serial Monitor for `[BLE] Advertising`; stay within 5 m |
| BLE connect shows no message | Subscribe to TX characteristic (`6E400003…`) before connecting |
| http://192.168.4.1 doesn't load | Confirm iPad is on PETBOT_CAM WiFi, not your home network |
| Web UI loads but buttons do nothing | Check Serial Monitor for `[CMD]` lines |
| Compile error about flash size | Switch partition to **Huge APP (3MB No OTA)** in Tools menu |
| Upload fails | Confirm IO0 tied to GND before power-on; remove jumper after upload |

---

## Hardware stubs

Set the matching `#define` to `1` in the firmware and fill the `TODO` bodies:

- `MOTORS_ENABLED` — motor driver (TB6612, DRV8833, L298N …)
- `SCREEN_ENABLED` — SPI/I2C display
- `MIC_ENABLED` — I2S microphone (INMP441 …)
- `SPEAKER_ENABLED` — I2S amplifier (MAX98357A …)
