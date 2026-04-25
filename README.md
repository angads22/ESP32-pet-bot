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

**Arduino IDE 2.x:**

1. Open `firmware/esp32_cam_brain.ino` in Arduino IDE 2.x.
2. Install board support: **Tools → Board → Boards Manager** → search `esp32` by Espressif, install ≥ 2.0.
3. Select **Tools → Board → AI Thinker ESP32-CAM**.
4. Set partition: **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)**.
5. Create a `sketch.yaml` alongside the `.ino` containing:
   ```yaml
   build_flags:
     - -DPETBOT_ENABLE_WIFI=1
   ```
6. Connect ESP32-CAM via USB-UART adapter (IO0 → GND for flash mode), click **Upload**.
7. Remove IO0 jumper and press reset.

**PlatformIO (VS Code / CLI):**

```bash
pio run -e petbot_wifi -t upload
```

The `platformio.ini` and `huge_app.csv` partition table in this repo handle everything automatically.

---

### Step 2 — First-time WiFi setup (captive portal)

On first boot (or after a credential reset) the device creates a hotspot named **PETBOT_SETUP**.

1. On your phone, tablet, or laptop go to **Settings → Wi-Fi** and join **PETBOT_SETUP** (password: `petbot123`).
2. A captive-portal page opens automatically. If it doesn't, open a browser and navigate to **http://192.168.4.1**.
3. Click **Configure WiFi**, select your home network from the list, enter the password, and click **Save**.
4. The device connects to your home network and the portal closes. The hotspot disappears.

> The portal stays open for **3 minutes**. If no credentials are submitted the device restarts and shows the hotspot again.

---

### Step 3 — Control from any device on the same network

Once connected, the device is reachable at:

```
http://petbot.local
```

from any phone, tablet, or computer on the same WiFi network. Open that URL in a browser to see the control UI with a D-pad and speak input.

Credentials are saved to flash — subsequent boots connect automatically without showing the hotspot.

---

### Reset WiFi credentials

Hold **GPIO 0** low for **3 seconds** during boot to erase saved credentials and re-enter setup mode. The serial monitor will confirm: `[WiFi] Credentials erased — starting setup portal`.

---

## Option B — Desktop / Android (BLE build)

> Requires Chrome 56+ or Edge 79+. Safari and Firefox do **not** support Web Bluetooth.

### Step 1 — Flash (default build, no extra flags)

Steps 1–3 of Option A but keep the **default partition scheme** and omit the build flag (or use `pio run -e petbot_ble -t upload`).

### Step 2 — Verify BLE advertising

Open Serial Monitor at **115200 baud**. You should see:

```
=== PetBot booting ===
[BLE] Advertising as "PetBot"
=== PetBot ready ===
```

### Step 3 — Connect via BLE web app

1. Serve the web app locally: `python3 -m http.server 8080` from the repo root.
2. Open **http://localhost:8080/web/robot_webapp.html** in Chrome or Edge.
3. Click **Connect** — select **PetBot** from the Bluetooth picker.

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
| PETBOT_SETUP hotspot doesn't appear | Confirm compiled with `-DPETBOT_ENABLE_WIFI=1` and **Huge APP** partition; check Serial Monitor for `[WiFi]` lines |
| Captive portal page doesn't open automatically | Manually navigate to **http://192.168.4.1** while connected to PETBOT_SETUP |
| Portal says "Failed to connect" after entering credentials | Double-check your home WiFi password; the device retries every restart |
| http://petbot.local doesn't load after setup | Confirm your device is on the same home network; try the IP shown in Serial Monitor |
| Web UI loads but buttons do nothing | Check Serial Monitor for `[CMD]` lines; confirm no firewall blocks port 80 |
| nRF Connect doesn't show PetBot | Check Serial Monitor for `[BLE] Advertising`; stay within 5 m |
| Compile error about flash size | Switch partition to **Huge APP (3MB No OTA)** in Tools menu (Arduino IDE) or use `petbot_wifi` env (PlatformIO) |
| Upload fails | Confirm IO0 tied to GND before power-on; remove jumper after upload |

---

## Hardware stubs

Set the matching `#define` to `1` in the firmware and fill the `TODO` bodies:

- `MOTORS_ENABLED` — motor driver (TB6612, DRV8833, L298N …)
- `SCREEN_ENABLED` — SPI/I2C display
- `MIC_ENABLED` — I2S microphone (INMP441 …)
- `SPEAKER_ENABLED` — I2S amplifier (MAX98357A …)
