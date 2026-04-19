# ESP32 Pet Bot — Firmware Plan + Codex Prompt

## 1) Build Plan (condensed)

### Goal
Create a Cozmo-style desktop robot using:
- **ESP32-C6 display board** as UI + main control brain
- **ESP32-CAM** as a dedicated vision coprocessor
- Differential drive motors + speaker for expressive behavior

### Core architecture
- **ESP32-C6**
  - ST7789 face rendering (`172x320`)
  - State machine (`IDLE`, `HAPPY`, `SEARCH`, `CURIOUS`, `DRIVE`, `SLEEP`)
  - Motor driver control (TB6612FNG)
  - Audio trigger/output (MAX98357A over I2S)
  - UART parser for camera events
- **ESP32-CAM**
  - Frame capture and simple person/object detection/tracking
  - UART output protocol:
    - `SEEN,x,y,size`
    - `LOST`

### Hardware constraints
Display pins are reserved and must not be reused:
- `MOSI=GPIO6`
- `SCLK=GPIO7`
- `CS=GPIO14`
- `DC=GPIO15`
- `RST=GPIO21`
- `BL=GPIO22`

### Motion decision
Use **2 DC gear motors + 1 caster** (fastest/lowest risk path).

---

## 2) Suggested Pin Map (starting point)

> Note: Keep this as a draft until you verify your exact ESP32-C6 board pin breakout and strapping restrictions.

### ESP32-C6 main board
- **Display (fixed / reserved)**
  - GPIO6  -> ST7789 MOSI
  - GPIO7  -> ST7789 SCLK
  - GPIO14 -> ST7789 CS
  - GPIO15 -> ST7789 DC
  - GPIO21 -> ST7789 RST
  - GPIO22 -> ST7789 BL

- **Motor driver (TB6612FNG)**
  - GPIO0  -> AIN1
  - GPIO1  -> AIN2
  - GPIO2  -> BIN1
  - GPIO3  -> BIN2
  - GPIO4  -> PWMA (PWM)
  - GPIO5  -> PWMB (PWM)
  - GPIO16 -> STBY

- **UART to ESP32-CAM**
  - GPIO17 (TX) -> CAM RX
  - GPIO18 (RX) -> CAM TX

- **I2S audio (MAX98357A)**
  - GPIO19 -> BCLK
  - GPIO20 -> LRC/WS
  - GPIO23 -> DIN

### Power notes
- Battery -> switch -> motor rail + buck converter for logic rail.
- Common ground across ESP32-C6, ESP32-CAM, TB6612FNG, and amp.
- Keep motor current off logic traces where possible.

---

## 3) Firmware module layout

```text
firmware/
  src/
    main.cpp
    app_state.h / app_state.cpp
    display_face.h / display_face.cpp
    motor_driver.h / motor_driver.cpp
    audio_player.h / audio_player.cpp
    vision_uart.h / vision_uart.cpp
    config_pins.h
```

### Module contracts
- `display_face`: eye draw, blink timing, expression presets by state.
- `motor_driver`: `setMotor(left,right)`, `driveForward()`, `turnLeft()`, `turnRight()`, `stop()`.
- `audio_player`: `playBoot()`, `playHappy()`, `playSearch()`.
- `vision_uart`: parse and expose:
  - `targetSeen`
  - `targetX`
  - `targetSize`

---

## 4) State machine behavior (v1)

- `IDLE`
  - Neutral face, occasional blink.
  - If target appears: `CURIOUS`.
- `CURIOUS`
  - Track target with eye direction and steering bias.
  - If centered and large enough: `HAPPY`.
  - If target stable but far: `DRIVE`.
  - If lost timeout: `SEARCH`.
- `DRIVE`
  - Move forward while tracking.
  - Stop/retreat if target too close (or ToF alert).
  - If lost: `SEARCH`.
- `SEARCH`
  - Slow spin scan + searching sound occasionally.
  - If seen: `CURIOUS`.
  - If prolonged no target: `IDLE`.
- `HAPPY`
  - Short celebratory face/sound burst.
  - Return to `IDLE` or `CURIOUS` depending on target.
- `SLEEP`
  - Reduced updates, dim screen, wake on trigger.

---

## 5) UART protocol parser rules

- Message formats:
  - `SEEN,<x>,<y>,<size>`
  - `LOST`
- Parser requirements:
  - Line buffered (`\n` terminated)
  - Ignore malformed packets
  - Clamp values to configured range
  - Timestamp last valid packet to detect stale vision data

---

## 6) Codex-ready prompt (for generating firmware)

Copy/paste this directly into Codex when you want full code generation:

```text
Generate PlatformIO Arduino firmware for an ESP32-C6 pet robot with modular C++ files.

Requirements:
1) Board roles
- ESP32-C6 is the main controller: state machine, display face (ST7789 172x320), motor control (TB6612FNG), audio output (MAX98357A I2S), UART receive from ESP32-CAM.
- ESP32-CAM is external and sends only UART lines: "SEEN,x,y,size" or "LOST".

2) Reserved display pins (must not be reused):
MOSI=GPIO6, SCLK=GPIO7, CS=GPIO14, DC=GPIO15, RST=GPIO21, BL=GPIO22.

3) Create files:
- src/main.cpp
- src/config_pins.h
- src/app_state.h / src/app_state.cpp
- src/display_face.h / src/display_face.cpp
- src/motor_driver.h / src/motor_driver.cpp
- src/audio_player.h / src/audio_player.cpp
- src/vision_uart.h / src/vision_uart.cpp

4) Implement states:
IDLE, HAPPY, SEARCH, CURIOUS, DRIVE, SLEEP.
Behavior:
- If target seen -> CURIOUS/DRIVE tracking behavior.
- If target lost -> SEARCH spin behavior.
- Otherwise IDLE with blinking face.

5) Required APIs:
- motor_driver: setMotor(int left, int right), driveForward(int speed), turnLeft(int speed), turnRight(int speed), stop().
- vision_uart: update(), targetSeen(), targetX(), targetSize(), millisSinceLastSeen().
- display_face: begin(), setExpression(...), update().
- audio_player: begin(), playBoot(), playHappy(), playSearch().

6) Include defensive coding:
- UART parser ignores malformed lines.
- Clamp motor command values.
- Non-blocking timing using millis().

7) Add a short README section in comments at top of main.cpp describing wiring assumptions and where to edit pin mapping.

Return complete code for all files.
```

---

## 7) Phase checklist

### Phase 1 (core bring-up)
- Display face renders and blinks.
- Motors can run forward/turn/stop.
- Audio chirp on boot.

### Phase 2 (vision integration)
- ESP32-CAM UART packets parsed reliably.
- Robot can rotate toward target.

### Phase 3 (personality)
- Expression + sound mapped to state transitions.
- Search behavior feels intentional (timed turn cadence).

### Phase 4 (advanced)
- ToF obstacle handling.
- Optional servo head motion.
- Better tracking smoothing.

---

## 8) Complete Signal Pin Reference

### ESP32-C6 (display board — `esp32_screen_only.ino`)

| Signal | GPIO | Direction | Connected to | Notes |
|--------|------|-----------|-------------|-------|
| TFT MOSI | 6 | OUT | ST7789 SDA | Reserved — do not reuse |
| TFT SCLK | 7 | OUT | ST7789 SCL | Reserved |
| TFT CS | 14 | OUT | ST7789 CS | Reserved |
| TFT DC | 15 | OUT | ST7789 DC | Reserved |
| TFT RST | 21 | OUT | ST7789 RST | Reserved |
| TFT BL | 22 | OUT | ST7789 BLK | High = backlight on |
| Motor AIN1 | 0 | OUT | TB6612 AIN1 | Motor A direction |
| Motor AIN2 | 1 | OUT | TB6612 AIN2 | Motor A direction |
| Motor BIN1 | 2 | OUT | TB6612 BIN1 | Motor B direction |
| Motor BIN2 | 3 | OUT | TB6612 BIN2 | Motor B direction |
| Motor PWMA | 4 | OUT | TB6612 PWMA | Motor A speed (PWM) |
| Motor PWMB | 5 | OUT | TB6612 PWMB | Motor B speed (PWM) |
| Motor STBY | 16 | OUT | TB6612 STBY | High = driver active |
| UART TX | 17 | OUT | CAM GPIO3 | To CAM RX |
| UART RX | 18 | IN | CAM GPIO1 | From CAM TX |
| I2S BCLK | 19 | OUT | MAX98357 BCLK | Bit clock |
| I2S LRC | 20 | OUT | MAX98357 LRC | Word select / frame sync |
| I2S DIN | 23 | OUT | MAX98357 DIN | Audio data |
| Mic SCK* | 8 | OUT | INMP441 SCK | *Not populated yet |
| Mic WS* | 9 | OUT | INMP441 WS | *Enable with MIC_ENABLED=1 |
| Mic SD* | 10 | IN | INMP441 SD | *Not populated yet |

### ESP32-CAM (brain board — `esp32_cam_brain.ino`, AI-Thinker module)

| Signal | GPIO | Direction | Connected to | Notes |
|--------|------|-----------|-------------|-------|
| UART TX | 1 | OUT | C6 GPIO18 | To display RX |
| UART RX | 3 | IN | C6 GPIO17 | From display TX |
| Motor AIN1 | 12 | OUT | TB6612 AIN1 | Motor A direction |
| Motor AIN2 | 13 | OUT | TB6612 AIN2 | Motor A direction |
| Motor BIN1 | 14 | OUT | TB6612 BIN1 | Motor B direction |
| Motor BIN2 | 15 | OUT | TB6612 BIN2 | Motor B direction |
| Motor PWMA | 2 | OUT | TB6612 PWMA | Also used for SD card; avoid SD if using PWM |
| Motor PWMB | 4 | OUT | TB6612 PWMB | Also: onboard flash LED |
| Cam PWDN | 32 | OUT | OV2640 PWDN | Camera power-down |
| Cam XCLK | 0 | OUT | OV2640 XCLK | 20 MHz clock |
| Cam SIOD | 26 | I/O | OV2640 SDA | SCCB data |
| Cam SIOC | 27 | OUT | OV2640 SCL | SCCB clock |
| Cam D7–D0 | 35,34,39,36,21,19,18,5 | IN | OV2640 data | 8-bit parallel data |
| Cam VSYNC | 25 | IN | OV2640 VSYNC | Frame sync |
| Cam HREF | 23 | IN | OV2640 HREF | Line valid |
| Cam PCLK | 22 | IN | OV2640 PCLK | Pixel clock |

---

## 9) Drawing a Custom Face

All face drawing lives in `firmware/esp32_screen_only.ino` inside `renderFace()`.
The screen is **172 px wide × 320 px tall** (portrait native; `setRotation(1)` makes it landscape 320×172).

### Coordinate system after `setRotation(1)`
```
(0,0) ─────────────────────────── (319,0)
  │         SCREEN_W = 172                │   ← these are swapped by rotation
  │         SCREEN_H = 320                │
(0,171) ──────────────────────── (319,171)
```
Centred helpers: `SCREEN_W / 2 = 86`  `SCREEN_H / 2 = 160`

### Eye placement
```cpp
const int leftX  = SCREEN_W / 2 - 40;  // 46
const int rightX = SCREEN_W / 2 + 40;  // 126
const int eyeY   = SCREEN_H / 2 - 20;  // 140
```

### Primitive toolkit
| Function | What it draws |
|----------|--------------|
| `tft.fillRoundRect(x, y, w, h, r, color)` | Rounded rectangle (use for eyes, eyebrows) |
| `tft.fillCircle(cx, cy, r, color)` | Filled circle (pupils, cheeks) |
| `tft.drawCircle(cx, cy, r, color)` | Circle outline (round mouth) |
| `tft.drawLine(x0,y0, x1,y1, color)` | Angled line (smile, frown) |
| `tft.fillTriangle(x0,y0, x1,y1, x2,y2, color)` | Triangle (open mouth, fangs) |
| `tft.drawFastHLine(x, y, len, color)` | Horizontal line (mouth baseline) |
| `tft.fillScreen(color)` | Background colour |

### Step-by-step example: add pupils
```cpp
// After drawing white eye rectangles:
if (!blinkActive && currentMode != FaceMode::SLEEP) {
    tft.fillCircle(leftX,  eyeY, 8, ST77XX_BLACK);  // left pupil
    tft.fillCircle(rightX, eyeY, 8, ST77XX_BLACK);  // right pupil
}
```

### Step-by-step example: curved smile for HAPPY
```cpp
// Replace the HAPPY mouth drawFastHLine calls with:
tft.drawCircle(SCREEN_W / 2, mouthY - 10, 18, ST77XX_WHITE);
// Then black out the top half so only the bottom arc shows:
tft.fillRect(SCREEN_W / 2 - 20, mouthY - 28, 40, 20, bgForMode(currentMode));
```

### Blink rule
When `blinkActive == true`, always reduce `eyeH` to ≤ 4 px (or 3 px for exaggerated blink).
The eye width can stay the same — only the height collapses.

### Per-expression customisation
Use a `switch (currentMode)` block inside `renderFace()`:
```cpp
switch (currentMode) {
  case FaceMode::HAPPY:
    // wider mouth, squinted eyes, rosy cheeks
    tft.fillCircle(leftX - 14, eyeY + 20, 6, 0xF800);   // left blush
    tft.fillCircle(rightX + 14, eyeY + 20, 6, 0xF800);  // right blush
    break;
  case FaceMode::SEARCH:
    // raised single eyebrow
    tft.fillRoundRect(rightX - 18, eyeY - 32, 36, 6, 3, ST77XX_WHITE);
    break;
  default: break;
}
```

### Adding new expressions
1. Add a new value to `enum class FaceMode` in `esp32_screen_only.ino`
2. Handle it in `bgForMode()`, `renderFace()`, and `parseMode()`
3. Send `FACE,NEWMODE` from `esp32_cam_brain.ino` to trigger it

---

## 10) Web App & BLE Setup

### Requirements
- Chrome or Edge (desktop or Android) — Web Bluetooth is **not** supported on Safari or Firefox
- Open `web/robot_webapp.html` as a local file (`File → Open`) or serve it over HTTP

### Pairing steps
1. Flash `esp32_cam_brain.ino` to the ESP32-CAM board
2. Power on the robot — it advertises as **"PetBot"** over BLE
3. Open `robot_webapp.html` in Chrome → click **Connect via Bluetooth**
4. Select **PetBot** from the browser's device picker → click **Pair**
5. All controls (D-pad, face, TTS, sounds) are now live over BLE

### Camera stream (optional)
1. On your phone/laptop, connect to WiFi SSID **PETBOT_CTRL** (password `petbot123`)
2. In the web app, click **Show Camera** — stream loads from `http://192.168.4.1/stream`
3. BLE control continues to work simultaneously

### BLE command reference
```
MOVE:fwd | back | left | right | stop
FACE:HAPPY | IDLE | SEARCH | CURIOUS | SLEEP
SAY:<text up to 64 chars>
SOUND:BOOT | HAPPY | ALERT
MODE:manual | auto
STATUS
```

### Adding a microphone (INMP441)
Wire the INMP441 to the ESP32-C6:
- SCK → GPIO8   WS → GPIO9   SD → GPIO10   VDD → 3.3V   GND → GND   L/R → GND

Then in `esp32_screen_only.ino` change `#define MIC_ENABLED 0` to `#define MIC_ENABLED 1` and reflash.
