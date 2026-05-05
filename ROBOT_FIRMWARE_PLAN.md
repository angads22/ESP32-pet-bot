# ESP32 Pet Bot — Firmware Plan

## 1. Architecture (canonical)

PetBot is a two-board Cozmo-style desktop robot. The boards have asymmetric
roles:

- **ESP32-S3-CAM = brain.** Owns app state, BLE NUS, WiFi web UI, vision
  (OV2640), motors, audio, and the **large** face TFT (driven by the S3 over
  its own SPI bus). All command dispatch, all behaviour, all I/O with the
  outside world lives here.
- **ESP32-C6-LCD-1.47 = thin display client.** Renders whatever the S3 tells
  it on its onboard 172×320 ST7789, and reports button presses back. No
  application logic, no BLE, no WiFi. Treat it as a pixel pipe with input.

All commands flow `phone → S3-CAM → C6`. The C6 never sees BLE traffic
directly. The link between the boards is a framed binary protocol over
UART today, with a USB-CDC option planned later (see Task 8 in the working
task list).

## 2. Boards and modules

### ESP32-S3-CAM (brain)

| Subsystem | Notes |
|-----------|-------|
| BLE NUS | Phone control via `PetBot` Nordic UART Service |
| WiFi web UI | Captive-portal provisioning, mDNS at `petbot.local`, embedded HTML d-pad |
| Vision | OV2640 capture, simple person/object detection |
| Motors | 2× DC gear + caster, TB6612FNG driver |
| Audio | MAX98357A I2S amp |
| Face TFT | Big ST7789/ILI9341 on a separate SPI bus from the OV2640 |
| C6 link | UART (default) or USB-CDC (later milestone) |
| App state | `IDLE / HAPPY / SEARCH / CURIOUS / DRIVE / SLEEP` |

### ESP32-C6-LCD-1.47 (thin display client)

| Subsystem | Notes |
|-----------|-------|
| LCD | Onboard 172×320 ST7789 (`setRotation(1)` → 320×172 landscape) |
| Renderer | Executes `DRAW_*` and `SET_MENU` packets directly via Adafruit_GFX-style primitives |
| PNG blob receiver | Streams `BLOB_PNG_*` chunks into SRAM (<40 KB) or SD card (≥40 KB) and decodes with PNGdec v1.0.2 |
| Input | BOOT button + any added buttons, polled at 50 Hz with debouncing |
| S3 link | UART (default) or USB-CDC (later) |

The C6 sends `PB_HELLO` once on boot, then runs a tight loop of
`feed transport bytes → dispatch frame` and `poll inputs → emit
PB_BTN_EVENT`. Its only knowledge of the application is the menu items
the S3 has most recently pushed.

## 3. Reserved C6 display GPIOs (do not reuse)

Waveshare ESP32-C6-LCD-1.47 pin map — these are wired on-board to the
ST7789 and **must not be repurposed** for transport, buttons, or anything
else:

| Function | GPIO |
|----------|------|
| MOSI | 6 |
| SCLK | 7 |
| CS | 14 |
| DC | 15 |
| RST | 21 |
| BL | 22 |

UART for the S3 link must use other pins (suggested: any free GPIO pair
clear of the strapping pins and the SD-card slot pins; finalise once the
specific C6 carrier is on the bench).

## 4. Wire protocol

Source of truth: `shared/protocol/`. Both firmwares include the same
`packets.h` and `frame.{h,cpp}`.

### Frame format

```
[0xAA] [0x55] [type] [seq] [len_hi] [len_lo] [payload...] [crc_hi] [crc_lo]
```

- Magic bytes `0xAA 0x55` for resync.
- `type` is one byte (see packet IDs below).
- `seq` is a sender-incrementing byte; receivers may use it for dedup.
- `len` is a 16-bit big-endian payload length.
- `crc` is CRC16-CCITT computed over `type ... end-of-payload`.

### Packet IDs

S3 → C6:

| ID | Name | Payload |
|----|------|---------|
| 0x01 | `PB_CLEAR` | empty (clear screen to background) |
| 0x02 | `PB_DRAW_TEXT` | `[x:i16][y:i16][color:u16][size:u8][text...]` |
| 0x03 | `PB_DRAW_RECT` | `[x:i16][y:i16][w:u16][h:u16][color:u16][filled:u8]` |
| 0x04 | `PB_DRAW_ICON` | `[x:i16][y:i16][icon_id:u8]` |
| 0x05 | `PB_SET_MENU` | `[selected_idx:u8][title_len:u8][title...][n:u8] {item_len:u8 item...}*n` |
| 0x06 | `PB_BLOB_PNG_BEGIN` | `[total_len:u32][crc32:u32][x:i16][y:i16][w:u16][h:u16]` |
| 0x07 | `PB_BLOB_PNG_CHUNK` | `[chunk_idx:u16][data...]` |
| 0x08 | `PB_BLOB_PNG_END` | empty |
| 0x09 | `PB_BACKLIGHT` | `[level:u8]` (0–255) |

C6 → S3:

| ID | Name | Payload |
|----|------|---------|
| 0x80 | `PB_BTN_EVENT` | `[btn_id:u8][edge:u8]` (0=release, 1=press, 2=long-press) |
| 0x81 | `PB_ACK` | `[ack_seq:u8]` |
| 0x82 | `PB_NAK` | `[ack_seq:u8][reason:u8]` |
| 0x83 | `PB_LOG` | `[level:u8][text...]` |
| 0x84 | `PB_HELLO` | `[fw_version:u16][caps_bitmap:u16]` |

## 5. State machine (S3-side)

| State | Behaviour |
|-------|-----------|
| `IDLE` | Neutral big-face, occasional blink. Target seen → `CURIOUS`. |
| `CURIOUS` | Eye direction tracks target. Centred + large enough → `HAPPY`. Stable + far → `DRIVE`. Lost timeout → `SEARCH`. |
| `DRIVE` | Forward while tracking. Too close / ToF alert → stop or retreat. Lost → `SEARCH`. |
| `SEARCH` | Slow spin scan + occasional searching sound. Seen → `CURIOUS`. Long no-target → `IDLE`. |
| `HAPPY` | Short celebratory face/sound. Returns to `IDLE` or `CURIOUS`. |
| `SLEEP` | Reduced updates, big-face dim, wake on trigger. |

The same state-mutation API (`setExpression`, `setMode`, etc.) is called
by **both** the BLE/web UI handler **and** the menu controller that
processes incoming `PB_BTN_EVENT` packets from the C6. There is one
source of truth.

## 6. Drawing the big face (S3-side)

The big face TFT is driven by the S3 directly. Use a separate SPI bus
from the OV2640 camera bus (suggestion: route the face TFT to `FSPI` /
`SPI3_HOST`, leaving the camera's parallel data + dedicated SCCB intact).
Library choice: `Adafruit_GFX` + `Adafruit_ST7789` (or `TFT_eSPI`
configured via `User_Setup.h`).

Useful primitives carried over from earlier prototypes:

| Function | Use |
|----------|-----|
| `tft.fillRoundRect(x, y, w, h, r, color)` | Eyes, eyebrows |
| `tft.fillCircle(cx, cy, r, color)` | Pupils, cheeks |
| `tft.drawCircle(cx, cy, r, color)` | Mouth outlines |
| `tft.drawLine(x0,y0,x1,y1,color)` | Smile / frown segments |
| `tft.fillTriangle(...)` | Open mouth, fangs |
| `tft.fillScreen(color)` | Background |

Blink rule: when `blinkActive`, collapse `eyeH` to ≤ 4 px while keeping
the eye width unchanged. Per-expression deltas live in a single
`switch (currentMode)` block inside `face_render::render()` so adding a
mode = adding one case.

The C6's small ST7789 is **not** the big face. The C6 mirrors menus,
status text, and (via `PB_BLOB_PNG_*`) optional PNG decals — the brain
TFT and the C6 LCD are independent surfaces.

## 7. Suggested S3-CAM pin map (verify on the specific board)

```
Camera (OV2640, AI-Thinker-style routing — confirm against your S3-CAM variant):
  PWDN, RESET, XCLK, SIOD, SIOC, D0..D7, VSYNC, HREF, PCLK

Face TFT (separate SPI bus — FSPI/SPI3_HOST):
  MOSI, SCLK, CS, DC, RST, BL  (pick free GPIOs that don't collide with the camera)

C6 link (UART, default transport):
  TX = GPIO17, RX = GPIO18, GND common
  Baud 921600, 8N1

Motors (TB6612FNG):
  AIN1, AIN2, BIN1, BIN2, PWMA, PWMB, STBY  (assign once camera + face TFT pins are locked)

Audio (MAX98357A):
  BCLK, LRC, DIN
```

The S3-CAM has more free GPIOs than the original AI-Thinker ESP32-CAM,
but the camera parallel bus and PSRAM still claim a lot of them. Lock
this map down once the specific S3-CAM carrier board is selected.

## 8. Web app & BLE setup

Phone control is unchanged from the single-board era — the BLE NUS
service and the captive-portal web UI both live on the S3 brain. The
C6 does not advertise BLE.

### Pairing

1. Flash `petbot_s3` to the S3-CAM. It advertises `PetBot` over BLE NUS.
2. Open `web/robot_webapp.html` in Chrome / Edge → **Connect via Bluetooth**.
3. Pick **PetBot** in the picker → all BLE controls are live.

### WiFi web UI

Compile with `-DPETBOT_ENABLE_WIFI=1`, `huge_app.csv` partition. First
boot creates the captive-portal AP `PETBOT_SETUP` (`petbot123`) at
`192.168.4.1`. After provisioning, the device joins the home network
and is reachable at `http://petbot.local`. The MJPEG stream (when
`-DPETBOT_ENABLE_STREAM=1`) lives on the same web server at `/stream`.

### BLE command surface (phone → S3)

```
MOVE:fwd | back | left | right | stop
FACE:HAPPY | IDLE | SEARCH | CURIOUS | SLEEP
SAY:<text up to 64 chars>
SOUND:BOOT | HAPPY | ALERT
SCREEN:<text>     # debug — pushes a one-line PB_DRAW_TEXT to the C6
MODE:manual | auto
STATUS
```

Every verb above resolves to a single `app_state::*` call on the S3.
The same call is made when the C6's menu controller delivers a
`PB_BTN_EVENT` that selects the equivalent menu item, so menu and BLE
share one code path.

## 9. Phase checklist

| Phase | Goal |
|-------|------|
| 1 — Bring-up | UART link works, root menu renders on C6, BOOT button navigates, BLE still drives motors and big face |
| 2 — Vision | OV2640 detection feeds `CURIOUS / DRIVE / SEARCH` transitions |
| 3 — Personality | Per-state face + sound combos; intentional-feeling search cadence |
| 4 — Advanced | ToF obstacle handling, optional servo head, tracking smoothing, USB-CDC transport |

## 10. Repository layout (after refactor)

```
shared/
  protocol/
    frame.{h,cpp}    # framing + CRC16
    packets.h        # type IDs + payload offsets
    tests/           # host-side g++ unit tests
firmware/
  s3_cam_brain/
    src/
      main.cpp
      app_state.{h,cpp}
      face_render.{h,cpp}
      menu_controller.{h,cpp}
      motor_driver.{h,cpp}
      audio_player.{h,cpp}
      vision.{h,cpp}
      ble_web.{h,cpp}
      transport/{transport.h, transport_uart.{h,cpp}, transport_usbcdc.{h,cpp}}
  c6_display_client/
    src/
      main.cpp
      lcd.{h,cpp}
      renderer.{h,cpp}
      png_blob.{h,cpp}
      input.{h,cpp}
      transport/{transport.h, transport_uart.{h,cpp}, transport_usbcdc.{h,cpp}}
platformio.ini       # two envs: petbot_s3, petbot_c6
huge_app.csv         # partition table for the S3 build
README.md
ROBOT_FIRMWARE_PLAN.md
BRINGUP.md
CURRENT_STATE.md
```
