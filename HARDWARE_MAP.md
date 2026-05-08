# PetBot — hardware map (Phase A, blocking)

This is the deliverable for **Phase A** of the own-it-overhaul plan. It
remaps every Freenove dog-kit peripheral onto a free GPIO on the
ESP32-S3-CAM brain, lists every conflict with the existing camera /
face-TFT / C6-UART pin claims, and enumerates the Freenove gait API.

**Stop point:** review this document, fill in the open decisions at the
bottom, then I move to Phase B (servos + IK).

> Carrier caveat — the existing `firmware/s3_cam_brain/src/vision.cpp`
> uses an **AI-Thinker ESP32-CAM** (non-S3) pin map carried over from
> the original firmware. The actual ESP32-**S3**-CAM you're targeting
> will need its own camera pin map. The "S3-CAM tentative" column in
> this document assumes a generic ESP32-S3 carrier with GPIOs 1–21 +
> 35–48 broken out and *no other peripherals on those pins*. Confirm
> the exact carrier (LilyGO T-Camera S3, Freenove ESP32-S3 CAM, ESP32-
> S3-EYE, XIAO-S3-Sense, etc.) before any pin gets soldered.

---

## 1. Existing pin claims (untouched by this audit)

| Subsystem | Pin(s) | Notes |
|-----------|--------|-------|
| OV2640 camera bus | varies by carrier — see `firmware/s3_cam_brain/src/vision.cpp` | Currently AI-Thinker non-S3 values (`PWDN=32, XCLK=0, …`). Will be re-pinned in Phase B once carrier is confirmed. |
| C6 UART link | S3 `GPIO17` (TX1) / `GPIO18` (RX1) at 921 600 8N1 | `firmware/s3_cam_brain/src/transport/transport.cpp` |
| Face TFT (FSPI / SPI3_HOST) | TBD — Phase B chooses 6 free pins (MOSI / SCLK / CS / DC / RST / BL) | `firmware/s3_cam_brain/src/face_render.cpp` is gated on `FACE_TFT_ENABLED` |
| GPIO 0 credential-reset | `GPIO0` read low ≥ 3 s at boot wipes WiFi creds | `ble_web.cpp` — keep as-is unless GPIO0 is XCLK on this carrier (likely is) |
| Reserved on the **C6** (NOT the S3) | `6, 7, 14, 15, 21, 22` | ST7789 — listed for completeness, the S3 doesn't see them |

## 2. Freenove kit hardware → S3-CAM pin remap (TENTATIVE)

Source for the Freenove side: `Freenove/Freenove_ESP32_Dog_Firmware`'s
`RobotDefinitions.h` and `Freenove/Freenove_Robot_Dog_Kit_for_ESP32`'s
schematic PDF.

| Peripheral | Freenove WROVER pin | S3-CAM tentative pin | Notes / verification |
|-----------|--------------------|----------------------|----------------------|
| PCA9685 SDA (I2C0) | `GPIO13` | `GPIO13` | Free on most S3-CAM carriers. On AI-Thinker non-S3 was SD-card data 3 — does not apply to S3 carriers without an SD slot. |
| PCA9685 SCL (I2C0) | `GPIO14` | `GPIO14` | Same notes as SDA. |
| OLED #1 (status, SSD1306) | n/a | shares I2C0 above | Address `0x3C` (default). Same bus as PCA9685. |
| OLED #2 (face, SSD1306) | n/a | shares I2C0 above | Address `0x3D` — set via the address-select solder bridge / jumper on the back of the second board. |
| HC-SR04 TRIG | `GPIO32` | **`GPIO38`** (suggest) | `GPIO32` collides with `CAM_PWDN`. Pick any free output GPIO; 38 is commonly broken out on S3 carriers. |
| HC-SR04 ECHO | `GPIO12` | **`GPIO37`** (suggest) | `GPIO12` may collide with SD bus on some S3 carriers. ECHO is a 5 V signal — **add a 1 kΩ / 2 kΩ divider to bring it to 3.3 V before this GPIO**. |
| Capacitive touch | `GPIO15` | **`GPIO4`** (suggest) | The ESP32-S3 touch peripheral is on T1–T14 = GPIO1–14. Pick any unused touch-capable pin. `GPIO4 = T4` is a common safe choice. |
| Buzzer (passive, LEDC PWM) | `GPIO33` | **`GPIO39`** (suggest) | Any free GPIO that supports `ledc`. |
| WS2812 DIN (4 LEDs) | `GPIO0` | **`GPIO48`** (suggest) | `GPIO0` collides with `CAM_XCLK`. `GPIO48` is the onboard RGB LED on many S3 dev boards — if you want the 4× WS2812 strip *and* the onboard LED, pick a different free pin. WS2812 prefers a 5 V data line; if your strip flickers from 3.3 V logic, add a 74AHCT1G125 buffer. |
| Battery voltage ADC | `GPIO32` (same as TRIG, multiplexed) | **`GPIO1`** (suggest, ADC1_CH0) | Multiplexing TRIG with battery ADC is a Freenove hack — drop it. Use any free ADC1 pin and a 4:1 voltage divider. |
| Servo battery V+ | n/a (just a rail) | — | 6.0–8.3 V pack to PCA9685 V+. **Do NOT connect to the S3 5 V rail.** |

Suggested S3-CAM-side pin defines (to land in `firmware/s3_cam_brain/src/pin_config.h` during Phase B):

```cpp
// Freenove dog body — S3-CAM remap (TENTATIVE — verify per carrier)
#define DOG_I2C_SDA      13   // shared bus: PCA9685, OLED1, OLED2
#define DOG_I2C_SCL      14
#define PCA9685_ADDR     0x40
#define OLED1_ADDR       0x3C   // status
#define OLED2_ADDR       0x3D   // face
#define ULTRA_TRIG       38     // was Freenove GPIO32 — moved (camera PWDN conflict)
#define ULTRA_ECHO       37     // was Freenove GPIO12 — needs 5V→3V3 divider
#define TOUCH_PIN         4     // ESP32-S3 touch T4 — was Freenove GPIO15
#define BUZZER_PIN       39     // LEDC PWM
#define WS2812_DIN       48     // was Freenove GPIO0 — moved (camera XCLK conflict)
#define WS2812_COUNT      4
#define BATT_ADC_PIN      1     // ADC1_CH0, 4:1 divider
```

## 3. Conflict matrix

| Freenove default pin | Conflicts with | Resolution |
|----------------------|----------------|-----------|
| `GPIO0` (WS2812) | `CAM_XCLK` on AI-Thinker-style camera bus, also strapping pin | **Move WS2812 to `GPIO48`** |
| `GPIO32` (HC-SR04 TRIG, battery ADC) | `CAM_PWDN` | **Move TRIG to `GPIO38`, battery ADC to `GPIO1`** |
| `GPIO12` (HC-SR04 ECHO) | SD-card data on some carriers; strapping on classic ESP32 | **Move ECHO to `GPIO37`** + divider |
| `GPIO15` (touch) | strapping pin / SD on some carriers | **Move touch to `GPIO4` (T4)** |
| `GPIO13/14` (I2C) | SD-card data on AI-Thinker non-S3, free on S3 carriers | **Keep at 13/14** — but verify on your specific board |
| `GPIO33` (buzzer) | none on S3 carriers (on classic AI-Thinker ESP32-CAM `GPIO33` isn't broken out) | **Move buzzer to `GPIO39`** for breakout consistency |

## 4. Power & grounding

```
   ┌──────────────────────────── Servo battery (6.0–8.3 V, ≥ 3 A) ─┐
   │                                                               │
   │   pack (+) ──► PCA9685 V+  ──► 12× servo V+                   │
   │   pack (–) ──► PCA9685 GND ──► 12× servo GND ─┬── ALL GND     │
   │                                               │   COMMON      │
   │                                               │               │
   │   USB / buck (5 V) ──► S3-CAM 5 V ─► 3V3 reg ─┤               │
   │                       └──► PCA9685 VCC (logic, 3.3 V)         │
   │                       └──► OLED1 / OLED2 VCC (3.3 V)          │
   │                       └──► HC-SR04 VCC (5 V — divide ECHO!)   │
   │                       └──► WS2812 VCC (5 V)                   │
   │                                                               │
   │   Common GND tie-point: every device's GND meets here.        │
   │   Logic-ground separation between servo battery and S3 logic  │
   │   is OK and recommended IF you tie GNDs at exactly one place. │
   └───────────────────────────────────────────────────────────────┘
```

Hardware notes:

- **HC-SR04 ECHO is 5 V** — directly tying to a 3.3 V GPIO can damage
  the S3 over time. 1 kΩ (top) + 2 kΩ (bottom) divider, or a 74HCT logic
  shifter.
- **WS2812 DIN tolerates 3.3 V on most strips** but the spec wants
  ≥ 0.7 × VCC, so on a 5 V strip 3.3 V is borderline. Add a `74AHCT1G125`
  single-gate buffer if the strip flickers / shows wrong colours.
- **Servo current** — 12 × MG90S-class servos can pull > 3 A at peak.
  Don't run them off USB. Separate battery, common ground.

## 5. Freenove gait API — function inventory (for Phase B port)

From `Freenove_ESP32_Dog_Firmware/main/Motion.cpp` and friends:

| Function | What it does |
|----------|-------------|
| `cooToA(leg, x, y, z, &angles[3])` | Inverse kinematics: foot-tip Cartesian → 3 servo angles. Per-leg. Pure math. |
| `move_any(alpha, stepLength, gamma, speed)` | Omnidirectional walk. `alpha` = bearing 0–360°, `stepLength` 0–20 mm, `gamma` = yaw ±360°, `speed` = 1–8 mm/10 ms. **The only "walk" primitive — no separate trot/walk.** Today blocking with `delay(TICK_MS = 10)` per tick. |
| `twist_any(x, y, z)` | In-place body twist (no translation). |
| `setServoAngle(channel, 0–180°)` (PCA9685) | Maps angle to 500–2500 µs pulse. |
| `setServoOffset[4][3]` | Per-leg / per-joint calibration trim (radians). NVS-backed under key `KEY_SERVO_OFFSET`. |
| `task_MotionService` (FreeRTOS) | Pulls `mqMotion` queue, dispatches `ACTION_MOVE_ANY`, `ACTION_TWIST`, `ACTION_DANCING`. Runs core 0. |
| Dance routines (canned, blocking) | `danceSayHello`, `dancePushUp`, `danceStretchSelf`, `danceTurnAround`, `danceSitDown`, `danceDancing` — all in `DanceMovements.cpp`. |
| Obstacle avoidance reflex | `task_AutoWalking` polls sonar every 200 ms, auto-issues walk parameters when obstacle detected. |
| Servo channel layout | 12 used channels: `0, 1, 2, 5, 6, 7, 8, 9, 10, 13, 14, 15` (skipping 3, 4, 11, 12). Right-leg angles get `180° − a` inversion at channels 9, 10, 14. |
| Body geometry | `L1 = 23 mm` (root), `L2 = 55 mm` (thigh), `L3 = 59 mm` (calf). Body 136.4 × 80 mm. Max step height 15 mm. Active radius ~104 mm. |
| Step rate | `TICK_MS = 10` → 100 Hz inner loop; gait phase advances at `speed / TICK_MS` mm per tick. Speed clamped to 1–8 mm/10 ms. |

**Port plan for Phase B:**
- Keep `cooToA`, body geometry, channel layout, calibration table verbatim.
- Replace blocking `delay(TICK_MS)` loops with a 50 Hz FreeRTOS task ticking off a `gait_cmd_queue` of `GaitCmd { type, dir }`. The `cooToA` math stays pure.
- Drop the dance routines for now — defer to a later milestone.
- Keep `setServoOffset[4][3]` in NVS under the same key for forward-compatibility with their calibration tool (if anyone ever wants to use it).

## 6. Open decisions for the user

Before Phase B starts, please confirm:

1. **Which S3-CAM carrier do you have?** (LilyGO T-Camera S3 / Freenove ESP32-S3 CAM / ESP32-S3-EYE / XIAO-S3-Sense / other.) The exact camera pin map and the "free GPIO" set depend on this. Without it, every pin in §2 stays TENTATIVE.
2. **Is `GPIO48` taken by an onboard RGB LED on your carrier?** If yes, pick a different pin for `WS2812_DIN`.
3. **Do you have a 4-cell or 6-cell servo pack?** Affects the battery cutoff threshold (Freenove uses 5.9 V cutoff for a 6 V nominal pack).
4. **Confirm OLED part numbers.** Plan assumes 0.96" SSD1306 I2C with the address-select bridge accessible. If they're SH1106 or SPI variants, the driver path differs.
5. **Servo model** (MG90S? SG90? something bigger?). Drives the calibration sweep speed and the safe step rate.

Once these are answered I'll lock §2 into `pin_config.h` (Phase B start) and we move into the porting work.
