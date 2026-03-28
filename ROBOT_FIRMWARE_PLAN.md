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

## 8) Current repo implementation (March 28, 2026)

- `firmware/esp32_screen_only.ino`
  - Targeted for the ESP32-C6 display board.
  - Handles only ST7789 face rendering + UART command parsing.
  - Replies with `READY`, `PONG`, and `ACK` for communication health checks.

- `firmware/esp32_cam_brain.ino`
  - Targeted for ESP32-CAM.
  - Owns behavior state machine and movement decisions.
  - Pushes `FACE,<mode>` + `BLINK` to the screen board.
  - Hosts temporary Wi-Fi API endpoints:
    - `GET /api/status`
    - `GET /api/cmd?mode=auto|manual`
    - `GET /api/cmd?move=forward|left|right|stop`
    - `GET /api/cmd?face=<IDLE|CURIOUS|SEARCH|HAPPY|SLEEP>`
    - `GET /api/cmd?blink=1`

- `web/temp_robot_control.html`
  - Temporary browser UI that can control the ESP32-CAM API over Wi-Fi.
  - Default AP target in firmware is `http://192.168.4.1`.
