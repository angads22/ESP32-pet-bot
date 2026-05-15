# PetBot — hardware map (Phase A, blocking)

Carrier and head-screen decisions are now locked:

- **Brain: Freenove ESP32-S3 WROOM CAM** (ESP32-S3-WROOM-1 + OV2640 + 8 MB PSRAM).
- **Head display: the existing C6-LCD-1.47 mounts physically on the head.** Its onboard ST7789 *is* the dog's face. No second OLED is purchased for face duty; the second OLED can still live on the body as a status surface, or be deferred.
- **Head movement: pan only** (PCA9685 ch 11). Channel 12 stays free.
- **Servos: assumed MG90S 9 g metal-gear** (Freenove kit default — black, single signal wire). Calibration sweep will validate range per channel.

This audit re-pins every Freenove peripheral onto **Freenove S3-WROOM CAM-specific** free GPIOs (the prior tentative map assumed a generic carrier and was wrong about several pins). Phase B does not start until this section is reviewed.

---

## 1. Freenove ESP32-S3 WROOM CAM — pin reality

The OV2640 + PSRAM + USB-OTG on this board claim a *lot* of the low-numbered GPIOs. The *typical* Freenove S3-WROOM CAM pin map (verify against the board's silk + Freenove's published `CameraPins.h`):

| Function | GPIO |
|----------|------|
| Camera D0 | 11 |
| Camera D1 | 9 |
| Camera D2 | 8 |
| Camera D3 | 10 |
| Camera D4 | 12 |
| Camera D5 | 18 |
| Camera D6 | 17 |
| Camera D7 | 16 |
| Camera XCLK | 15 |
| Camera PCLK | 13 |
| Camera VSYNC | 6 |
| Camera HREF | 7 |
| Camera SIOD (SCCB SDA) | 4 |
| Camera SIOC (SCCB SCL) | 5 |
| Camera PWDN | −1 (not connected) |
| Camera RESET | −1 (not connected) |
| Onboard RGB LED (WS2812-style) | 48 |
| USB-OTG D+ / D− | 19 / 20 (used by native USB) |
| SD card slot (if your variant has one) | various — verify per silk |
| BOOT button | 0 |
| Strapping / reset | 3, 45, 46 |

**That leaves as freely available:** `1, 2, 14, 21, 38, 39, 40, 41, 42, 47`. Some carriers also break out `35, 36, 37` — check the silk.

> **Verify against Freenove's `CameraPins.h`** before any pin gets soldered.
> The table above is the documented default for the **ESP32-S3-WROOM CAM
> (revision 2024)**; older revisions of the same product line may differ.

## 2. Critical change to existing wiring

The transport refactor (this PR's shipped code) uses **`Serial1` on
`TX=17, RX=18`** for the C6 link (`firmware/s3_cam_brain/src/transport/transport.cpp`).
**Both of those pins are camera-bus pins (D6 and D5) on the Freenove
S3-WROOM CAM.** That wiring will not work on this board. The C6 UART
must be relocated to a free pair (suggested below).

| Subsystem | Old pin | New pin (tentative) | Reason |
|-----------|---------|--------------------|--------|
| C6 UART TX | `GPIO17` | **`GPIO38`** | `17` = camera D6 |
| C6 UART RX | `GPIO18` | **`GPIO39`** | `18` = camera D5 |

The C6-side UART pins (`Serial1` RX=16, TX=17 in
`firmware/c6_display_client/src/transport/transport.cpp`) stay as-is —
they're on the C6, not the S3, and the C6 doesn't have a camera bus.

## 3. Freenove kit hardware → Freenove S3-WROOM CAM pin remap (TENTATIVE)

| Peripheral | Freenove (WROVER) original | New on S3-WROOM CAM | Notes |
|-----------|----------------------------|---------------------|-------|
| PCA9685 SDA (I2C0) | `GPIO13` | **`GPIO2`** | `13` is camera PCLK on this board. `GPIO2` is free and not strapping-pin-sensitive after boot. |
| PCA9685 SCL (I2C0) | `GPIO14` | **`GPIO42`** | `14` may be free but `42` is safer (clear of any SD card mux). |
| OLED (status, SSD1306) | n/a | I2C0 (above) at `0x3C` | Same bus as PCA9685. Mounted on the body. |
| HC-SR04 TRIG | `GPIO32` | **`GPIO40`** | `32` does not exist on this board. |
| HC-SR04 ECHO | `GPIO12` | **`GPIO41`** | `12` is camera D4 here. ECHO is 5 V — **add a 1 kΩ / 2 kΩ divider** before this GPIO. |
| Capacitive touch | `GPIO15` | **`GPIO3`** | `15` is camera XCLK. `GPIO3 = T3` is touch-capable on the S3 and free. |
| Buzzer (passive, LEDC PWM) | `GPIO33` | **`GPIO47`** | `33` does not exist on this board. |
| WS2812 strip (4 LEDs) | `GPIO0` | **`GPIO21`** | `0` is the BOOT button on this board. `21` is broken out and free. WS2812 prefers 5 V data; add a `74AHCT1G125` buffer if flicker shows up. |
| Battery ADC | `GPIO32` (muxed) | **`GPIO1`** (ADC1_CH0) | Drop Freenove's mux hack. Use a 4:1 divider. |
| Onboard board RGB LED | n/a | `GPIO48` (left alone) | Useful as a "I'm alive" indicator in firmware. Don't reuse for the WS2812 strip. |
| Servo battery V+ | n/a | — | 6.0–8.3 V pack to PCA9685 V+. **Do NOT connect to the S3 5 V rail.** |
| **Head pan servo** | n/a | PCA9685 ch **11** | Pan-only. Soft limit ±90° to protect the C6's UART pigtail through the joint. |
| (Head tilt — dropped) | — | — | User chose pan-only; ch 12 stays free. |
| **C6 head display** | n/a | UART link only — see §2 | The C6 itself is mounted on the head; its onboard ST7789 *is* the face. No extra display parts on the head. |

Updated `pin_config.h` (lands in Phase B):

```cpp
// Freenove dog body on Freenove ESP32-S3 WROOM CAM
// (TENTATIVE — verify against Freenove's CameraPins.h for your board rev)

// Shared I2C0 — PCA9685 + status OLED
#define DOG_I2C_SDA       2
#define DOG_I2C_SCL      42
#define PCA9685_ADDR     0x40
#define OLED_STATUS_ADDR 0x3C   // body status OLED

// Sensors / effects
#define ULTRA_TRIG       40
#define ULTRA_ECHO       41    // 5V → divider → 3V3
#define TOUCH_PIN         3    // T3 on S3
#define BUZZER_PIN       47
#define WS2812_DIN       21
#define WS2812_COUNT      4
#define BATT_ADC_PIN      1    // ADC1_CH0, 4:1 divider

// C6 UART link — MOVED from 17/18 (camera D5/D6 on this board)
#define C6_LINK_TX       38
#define C6_LINK_RX       39
#define C6_LINK_BAUD     921600

// Head — pan only
#define HEAD_PAN_CH      11
#define HEAD_PAN_MIN    -90    // mechanical limit (C6 UART loop through joint)
#define HEAD_PAN_MAX    +90
```

## 4. Conflict matrix (S3-WROOM CAM-specific)

| What was claimed | What it actually is on this board | Resolution |
|------------------|----------------------------------|------------|
| C6 UART TX = `GPIO17` | Camera D6 | **Move to GPIO 38** |
| C6 UART RX = `GPIO18` | Camera D5 | **Move to GPIO 39** |
| Freenove I2C SDA = `GPIO13` | Camera PCLK | **Move to GPIO 2** |
| Freenove I2C SCL = `GPIO14` | OK, but adjacent to camera signals | **Move to GPIO 42** |
| Freenove TRIG = `GPIO32` | Doesn't exist | **Use GPIO 40** |
| Freenove ECHO = `GPIO12` | Camera D4 | **Use GPIO 41** + divider |
| Freenove touch = `GPIO15` | Camera XCLK | **Use GPIO 3 (T3)** |
| Freenove buzzer = `GPIO33` | Doesn't exist | **Use GPIO 47** |
| Freenove WS2812 = `GPIO0` | BOOT button + bootstrap | **Use GPIO 21** |
| Freenove battery ADC = `GPIO32` | Doesn't exist | **Use GPIO 1 (ADC1_CH0)** |
| GPIO 0 credential-reset (existing) | BOOT button — usable | **Keep as-is** for the WiFi-creds-reset feature |

## 5. Power & grounding

```
   ┌──────────────────────────── Servo battery (6.0–8.3 V, ≥ 3 A) ─┐
   │   pack (+) ──► PCA9685 V+  ──► 12× leg servos + 1× head servo │
   │   pack (–) ──► PCA9685 GND ──► all servo GND ─┬── COMMON GND  │
   │                                               │               │
   │   USB / buck (5 V) ──► S3 5 V ─► 3V3 reg ─────┤               │
   │                       └──► PCA9685 VCC (logic, 3.3 V)         │
   │                       └──► Status OLED VCC (3.3 V)            │
   │                       └──► HC-SR04 VCC (5 V — divide ECHO!)   │
   │                       └──► WS2812 VCC (5 V; buffer DIN if     │
   │                            flicker shows up)                  │
   │                       └──► C6 (mounted on head) 5 V or 3V3    │
   │                            per the C6's `Vin` pin             │
   │                                                               │
   │   Common GND tie-point: every device's GND meets here ONCE.   │
   └───────────────────────────────────────────────────────────────┘
```

Head-cable budget (the 4 wires routed through the pan joint to the C6):

```
   body ─── GND ─────────────────────────────► C6 GND
   body ─── 5 V (or 3V3) ─────────────────────► C6 Vin / 3V3
   body ─── S3 GPIO38 (TX) ──────────────────► C6 GPIO16 (RX)
   body ─── S3 GPIO39 (RX) ◄─────────────────  C6 GPIO17 (TX)
```

Use 28 AWG silicone-jacket flex (5 cm slack, helical loop around the pan
axis). With ±90° pan that's < a quarter turn per direction — well within
flex life.

## 6. Freenove gait API — function inventory (for Phase B port)

(Unchanged from prior version — kept here for reference.)

| Function | What it does |
|----------|-------------|
| `cooToA(leg, x, y, z, &angles[3])` | Inverse kinematics: foot-tip Cartesian → 3 servo angles. Pure math. |
| `move_any(alpha, stepLength, gamma, speed)` | Omnidirectional walk. `alpha` 0–360°, `stepLength` 0–20 mm, `gamma` ±360°, `speed` 1–8 mm/10 ms. |
| `twist_any(x, y, z)` | In-place body twist. |
| `setServoAngle(channel, 0–180°)` (PCA9685) | Maps to 500–2500 µs pulse. |
| `setServoOffset[4][3]` | Per-leg / per-joint calibration trim (radians) in NVS under `KEY_SERVO_OFFSET`. |
| Dance routines | `danceSayHello`, `dancePushUp`, `danceStretchSelf`, `danceTurnAround`, `danceSitDown`, `danceDancing`. |
| Servo channel layout | 12 used: `0, 1, 2, 5, 6, 7, 8, 9, 10, 13, 14, 15`. Right-leg inversion on `9, 10, 14`. **Add head pan on ch 11.** |
| Body geometry | `L1 = 23 mm` (root), `L2 = 55 mm` (thigh), `L3 = 59 mm` (calf). |
| Step rate | `TICK_MS = 10`; speed clamped 1–8 mm / tick. |

See `KINEMATICS.md` for the full IK derivation.

## 7. Architectural consequence: C6 on the head

The C6 is now physically on the head and renders the face. Two firmware
implications:

1. **Drop the planned `OLED #2` (face) module.** The C6's ST7789 is the
   face. The single status OLED on the body remains (Phase D shrinks).
2. **Add a `PB_SET_FACE` packet** to the wire protocol. Payload =
   `[face_mode:u8]`. C6 renders the face locally — eyes, mouth, blink
   animation — from a small face-render module on the C6 side. The S3
   just tells the C6 "be HAPPY"; C6 owns the pixels. This collapses
   what was a fan-out (S3 face TFT *and* C6 menu) into a single send.

The `face_render` module on the **S3** becomes a thin wrapper that
`pb_encode`s `PB_SET_FACE` and writes it to `transport()`. The blink-
animation timer moves to the C6 side, where it belongs (the C6's loop
already runs fast enough; the S3 doesn't need to think about it).

## 8. Open decisions remaining

These don't block Phase B — they're "I'd answer them while soldering":

1. **Servo pack** — 4-cell NiMH (4.8 V nominal, low) or 6-cell / 2S LiPo (7.4 V)? Affects the battery-low cutoff and the LDO choice for the S3.
2. **WS2812 strip behaviour at 3.3 V logic** — most strips work, some flicker. Buy a `74AHCT1G125` just in case (\$0.30, single-gate level shifter).
3. **Optional second OLED on the body** — purely a status / debug surface, with the face already covered by the C6. Skip in Phase D unless wanted.

Once those are settled I lock §3 into `pin_config.h` and start Phase B
(servos + IK + gait port).
