# PetBot — ESP32-CAM Firmware

BLE-controlled robot with optional MJPEG camera stream. Control via the included web app over Bluetooth Low Energy.

---

## Connecting to PetBot via Bluetooth

> **Requires:** Chrome 56+ or Edge 79+ on desktop/Android. Safari and Firefox do **not** support Web Bluetooth.

### Step 1 — Flash the firmware

1. Open `firmware/esp32_cam_brain.ino` in Arduino IDE (2.x recommended).
2. Install board support: **Tools → Board → Boards Manager** → search `esp32` by Espressif, install ≥ 2.0.
3. Select **Tools → Board → AI Thinker ESP32-CAM**.
4. Connect your ESP32-CAM via a USB-UART adapter (IO0 → GND for flash mode).
5. Click **Upload**. Remove the IO0 → GND jumper and press reset after upload completes.

> **Optional camera/stream build:** compile with `-DPETBOT_ENABLE_STREAM=1` and use the **Huge APP (3MB No OTA)** partition scheme.

### Step 2 — Verify BLE is advertising

Open the Arduino Serial Monitor at **115200 baud**. You should see:

```
=== PetBot booting ===
[BLE] Advertising as "PetBot"
=== PetBot ready ===
  BLE  : connect to "PetBot"
```

If you see the advertising line, the ESP32 is broadcasting correctly.

### Step 3 — Open the web app

Open `web/robot_webapp.html` directly in Chrome or Edge (no server needed — it runs from the local file).

### Step 4 — Pair with PetBot

1. Click the **Connect** button in the web app.
2. A browser Bluetooth picker appears. Wait a few seconds for **PetBot** to appear in the list.
3. Select **PetBot** and click **Pair**.
4. The status bar turns green and shows **Connected**.

> If "PetBot" does not appear: confirm Serial Monitor shows the advertising line, keep the phone/laptop within ~5 m, and ensure no other device is already connected (BLE NUS allows one client at a time).

### Step 5 — Send commands

Use the on-screen controls or type commands directly:

| Button / Command | Action |
|-----------------|--------|
| Forward / `MOVE:fwd` | Drive forward |
| Back / `MOVE:back` | Drive backward |
| Left / `MOVE:left` | Turn left |
| Right / `MOVE:right` | Turn right |
| Stop / `MOVE:stop` | Stop motors |
| `SAY:<text>` | Speak text (requires speaker hardware) |
| `SOUND:<name>` | Play named sound effect |
| `STATUS` | Returns enabled-feature bitmap |

The bot replies over BLE; responses appear in the app log.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| "PetBot" not visible in picker | Reload page, wait 10 s, check Serial Monitor for `[BLE] Advertising` |
| Picker shows device but pairing fails | Press reset on ESP32-CAM, try again |
| Connected but no response to commands | Check Serial Monitor for `[CMD]` lines; verify motor/speaker stubs are enabled |
| Disconnects immediately | Reduce distance; another client may be connected |
| Upload fails | Confirm IO0 tied to GND before powering on for flash mode |

---

## Hardware stubs

Enable optional hardware by setting the matching `#define` to `1` in the firmware and filling the `TODO` bodies:

- `MOTORS_ENABLED` — motor driver (TB6612, DRV8833, L298N …)
- `SCREEN_ENABLED` — SPI/I2C display
- `MIC_ENABLED` — I2S microphone (INMP441 …)
- `SPEAKER_ENABLED` — I2S amplifier (MAX98357A …)
