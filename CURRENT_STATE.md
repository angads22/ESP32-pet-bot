# PetBot — current repo state (Task 0 audit, pre-refactor)

This file is a factual snapshot of what the repo looks like **today**, before
any of the transport-refactor work in the task list is started. It exists so
that the user can confirm the audit is accurate before Task 1 begins.

Repo URL: `https://github.com/angads22/ESP32-pet-bot`
Branch the audit was performed on: `claude/petbot-transport-refactor-I67r9`

---

## 1. File-by-file inventory

The entire repo is six entries:

```
ROBOT_FIRMWARE_PLAN.md
README.md
platformio.ini
huge_app.csv
.gitkeep
firmware/
  esp32_cam_brain.ino
```

There is no `src/` directory. There are no `.cpp` or `.h` files. There is no
`web/` directory (despite README and the plan referring to one). There is
exactly one source file: `firmware/esp32_cam_brain.ino`.

### `firmware/esp32_cam_brain.ino` — what it actually does

- **Target board:** AI-Thinker ESP32-CAM (OV2640). The header comment, the
  `CAM_*` GPIO pin defines, and the partition table all confirm this.
- **Build flags:**
  - default → BLE-only build (fits 1.3 MB default partition).
  - `-DPETBOT_ENABLE_WIFI=1` → adds WiFiManager captive portal, mDNS, embedded
    web UI. Requires `huge_app.csv` partition.
  - `-DPETBOT_ENABLE_STREAM=1` → additionally initialises the OV2640 and
    serves an MJPEG stream at `/stream`. Implies WiFi.
- **BLE:** advertises as `"PetBot"` over the Nordic UART Service. Uses
  NimBLE if available (detected via `__has_include(<NimBLEDevice.h>)`),
  otherwise falls back to the classic `BLEDevice` stack.
- **WiFi (when enabled):** WiFiManager creates an AP `PETBOT_SETUP`
  (password `petbot123`) on first boot, captive portal at `192.168.4.1`,
  3-minute portal timeout. Once provisioned, it joins the user's home
  network and exposes `http://petbot.local` via mDNS. Holding GPIO0 low
  for 3 s at boot wipes saved credentials.
- **Web UI (when WiFi enabled):** a single embedded HTML page (in the
  `WEBAPP_HTML` string literal) with a 3×3 D-pad and a text-to-speak input.
  Buttons hit `GET /cmd?c=<urlencoded-command>`.
- **Camera stream (when both flags enabled):** `setupCamera()` configures
  the OV2640 at QVGA / JPEG / quality 10 / 2 framebuffers, and
  `handleStream()` serves a `multipart/x-mixed-replace` stream.
- **Hardware module stubs:** four `#define X_ENABLED 0` blocks for
  SCREEN, MOTORS, MIC, SPEAKER. Every motor/audio/screen function in
  the file is an empty no-op (`/* TODO */`) by default. Example pins
  are commented out — they are not active. As shipped today, the bot
  cannot drive, speak, listen, or display anything.
- **Command dispatcher (`handleCommand`):** receives strings from BLE
  RX or from `/cmd?c=`. Recognised verbs:
  - `MOVE:fwd|back|left|right|stop` → calls the corresponding (stubbed)
    motor function, replies `OK:MOVE:<dir>`.
  - `SAY:<text>` → `speakText()` (stub prints to serial), replies `OK:SAY`.
  - `SOUND:<name>` → `playSound()` (stub prints to serial), replies `OK:SOUND`.
  - `SCREEN:<text>` → `screenShow()` (stub), replies `OK:SCREEN`.
  - `STATUS` → returns a comma-separated feature-flag report.
  - anything else → `ERR:unknown:<cmd>`.
- **Setup order:** `setupCamera() → setupWifi() → setupBLE()` then the
  enabled-feature stubs. `loop()` only does an optional `micListen()` call
  and a 10 ms delay; all BLE / HTTP work is callback-driven.

### `platformio.ini`

Three environments, all targeting `board = esp32cam`:

- `petbot_ble` — default partition, no flags.
- `petbot_wifi` — `-DPETBOT_ENABLE_WIFI=1`, partition `huge_app.csv`.
- `petbot_stream` — adds `-DPETBOT_ENABLE_STREAM=1`, same partition.

There is **no** environment for an ESP32-C6 anywhere.

### `huge_app.csv`

A 4 MB-flash partition table: 20 KB NVS, 3 MB factory app, 960 KB SPIFFS.
Sized for the AI-Thinker ESP32-CAM. Currently only consumed by the two
WiFi-enabled CAM build envs.

### `README.md`

User-facing docs for the firmware. Talks exclusively about a single
ESP32-CAM device. Build-mode table lists the three CAM envs. Covers
WiFi captive-portal flow, BLE pairing in Chrome/Edge, and a command
table. Mentions stub `#defines` for screen/motors/mic/speaker but does
not describe a second board, a UART link, or any C6 firmware.

The README's BLE command table lists: `MOVE:*`, `SAY:`, `SOUND:`, `STATUS`.
It does **not** mention `SCREEN:`, even though the firmware accepts it.

### `ROBOT_FIRMWARE_PLAN.md`

A long architecture / planning document. Describes a **two-board** robot
where the **ESP32-C6 is the brain** (state machine, motors, audio, ST7789
face) and the ESP32-CAM is a thin **vision coprocessor** sending only
`SEEN,x,y,size` and `LOST` over UART. Includes a pin map for both boards,
a state-machine spec (`IDLE / HAPPY / SEARCH / CURIOUS / DRIVE / SLEEP`),
a Codex prompt for generating the firmware, a face-drawing tutorial, and
a BLE/web-app section. Repeatedly refers to `firmware/esp32_screen_only.ino`
as if it existed.

### `.gitkeep`

Empty zero-byte file at the repo root, left over from when `firmware/`
was empty.

---

## 2. Board targeting per file

| File | Board it targets in code/config | Notes |
|------|---------------------------------|-------|
| `firmware/esp32_cam_brain.ino` | ESP32-CAM (AI-Thinker, OV2640) | The only real source file |
| `platformio.ini` | ESP32-CAM (`board = esp32cam`) | No C6 env exists |
| `huge_app.csv` | ESP32-CAM 4 MB flash | Only used by CAM WiFi envs |
| `README.md` | ESP32-CAM (single-board view) | No mention of a second MCU |
| `ROBOT_FIRMWARE_PLAN.md` | Claims dual ESP32-C6 + ESP32-CAM | References a non-existent file |

`firmware/esp32_screen_only.ino` is referenced from PLAN §8 (pin reference),
§9 (face-drawing instructions), and §10 (microphone wiring) but **does not
exist in the repository**. There is no C6 firmware in any branch state I
can see in this checkout.

---

## 3. Inter-board protocol messages — what is actually wired up

Today there is **no inter-board protocol implemented in code**, because
there is only one firmware. The full set of "wire" messages currently in
source is the BLE / HTTP command dispatcher in `esp32_cam_brain.ino`:

**Inbound (phone → CAM, over BLE NUS write or HTTP `/cmd?c=`):**

- `MOVE:fwd`
- `MOVE:back`
- `MOVE:left`
- `MOVE:right`
- `MOVE:stop`
- `SAY:<text>`
- `SOUND:<name>`
- `SCREEN:<text>`
- `STATUS`

**Outbound (CAM → phone, over BLE NUS notify):**

- `READY:PetBot` (on connect)
- `OK:MOVE:<dir>`
- `OK:SAY`
- `OK:SOUND`
- `OK:SCREEN`
- `STATUS:ok,motors=<0|1>,screen=<0|1>,mic=<0|1>,speaker=<0|1>[,web=http://petbot.local]`
- `ERR:unknown:<cmd>`

**Inter-board messages described in `ROBOT_FIRMWARE_PLAN.md` but not in any
code:**

- `SEEN,<x>,<y>,<size>` (CAM → C6, hypothesised in §1 / §5)
- `LOST` (CAM → C6, hypothesised in §1 / §5)
- `FACE,<MODE>` (CAM → C6, mentioned in §9's face-drawing instructions —
  contradicts §1 which says CAM only emits `SEEN`/`LOST`)

The PLAN's BLE command table (§10) additionally claims the bot accepts
`FACE:HAPPY|IDLE|SEARCH|CURIOUS|SLEEP` and `MODE:manual|auto` over BLE.
**Neither verb is recognised** by the dispatcher in
`esp32_cam_brain.ino`; both fall through to `ERR:unknown:<cmd>`.

---

## 4. README vs ROBOT_FIRMWARE_PLAN.md — every disagreement

1. **Which board is the brain.**
   - README: implicitly the ESP32-CAM. It's the only board mentioned, and
     it owns BLE, WiFi, the web UI, and the command dispatcher.
   - PLAN §1 ("Core architecture"): the ESP32-C6 is the main controller.
     The CAM is downgraded to "a dedicated vision coprocessor" that emits
     only `SEEN` / `LOST`.
   - The actual code matches the README, not the plan.

2. **Existence of a second firmware file.**
   - README: never mentioned. The PlatformIO envs build only one binary.
   - PLAN: §8 and §9 reference `firmware/esp32_screen_only.ino` as if it
     were committed. It is not present anywhere in the tree.

3. **Where motors / audio / display live.**
   - README + code: motor / speaker / mic / screen stubs all live inside
     the CAM firmware; the example motor pins (`M_AIN1 12` etc.) match
     ESP32-CAM GPIOs.
   - PLAN §1 / §2 / §8: motors (TB6612FNG), audio (MAX98357A I2S), and
     the ST7789 face all live on the **ESP32-C6**, with the CAM doing
     vision only. PLAN §8's CAM pin table also lists motor pins on the
     CAM (GPIO12-15), so even the plan double-assigns motors to both
     boards.

4. **Which board hosts BLE and the web UI.**
   - README + code: the ESP32-CAM hosts BLE NUS and the HTTP web UI.
   - PLAN: never mentions BLE or the web UI in its architecture sections;
     §10 talks about pairing to "PetBot" without saying which MCU is
     advertising. The plan implicitly assumes the C6 is the only thing
     a phone talks to, which contradicts the actual code.

5. **WiFi SSID / IP for the camera stream.**
   - README + code: there is one captive-portal SSID `PETBOT_SETUP`
     (`petbot123`). Once the user provisions home WiFi, the device joins
     that network and the stream is reachable through `petbot.local`,
     not a fixed IP.
   - PLAN §10: claims a separate AP `PETBOT_CTRL` (password `petbot123`)
     at `192.168.4.1/stream`. **No such AP is created** by the firmware;
     `PETBOT_CTRL` does not appear in source.

6. **BLE command surface.**
   - README §Commands: `MOVE:*`, `SAY:`, `SOUND:`, `STATUS`.
   - Code dispatcher: `MOVE:*`, `SAY:`, `SOUND:`, `SCREEN:`, `STATUS`.
   - PLAN §10: `MOVE:*`, `FACE:*`, `SAY:`, `SOUND:`, `MODE:`, `STATUS`.
   - All three lists are different. `SCREEN:` is only in code; `FACE:`
     and `MODE:` are only in the plan; the README is missing `SCREEN:`.

7. **Display details.**
   - README: no mention of a TFT at all.
   - Code: `SCREEN_ENABLED 0`, no library or pin map chosen.
   - PLAN: ST7789 172×320 on the C6, with a fully-fleshed pin map
     (MOSI=6, SCLK=7, CS=14, DC=15, RST=21, BL=22) and a face-drawing
     tutorial keyed to `setRotation(1)` landscape.

8. **GPIO0 behaviour at boot.**
   - README + code: holding GPIO0 low for 3 s at boot wipes saved WiFi
     credentials.
   - PLAN: GPIO0 on the CAM is the OV2640 XCLK pin (§8 pin table). The
     plan's own table makes GPIO0 unsafe to repurpose, but the code
     reads it as an INPUT_PULLUP for the credential-reset feature. This
     works in the current code only because the camera is not initialised
     in the BLE-only build, but it would conflict with `PETBOT_ENABLE_STREAM`
     where the CAM driver claims XCLK on the same pin. (Worth flagging
     for later, not part of this refactor's scope.)

---

## 5. Other observations worth surfacing before Task 1

- All hardware peripherals (motors, screen, mic, speaker) are **stubbed
  out**. The bot today can advertise BLE, host a web page, and stream
  video, but it cannot move, render a face, or play sound.
- `platformio.ini` does not pin any library versions. NimBLE vs classic
  BLE is decided at compile time by header presence.
- The `huge_app.csv` partition table is sized for the CAM's 4 MB flash;
  it will need to be revisited (or duplicated) for a C6 build env.
- The `.gitkeep` at repo root has no purpose now that `firmware/` exists
  with content. It can be removed at any time.
- Git history shows the project went BLE-only → BLE+WiFi-AP → WiFi captive
  portal over the last several PRs; the architecture has been actively
  consolidating onto the CAM, which is the opposite direction from the
  PLAN's "C6 is the brain" thesis.

---

## 6. What this implies for the planned refactor

The task list says the canonical architecture going forward is **CAM = brain,
C6 = thin display client**. That matches the actual code direction (CAM has
absorbed all control logic) and **inverts** the role assignment in
`ROBOT_FIRMWARE_PLAN.md`. Resolving that inversion in docs is Task 1.

Concretely, before any refactor begins, the user should confirm:

1. The audit above is accurate.
2. They are OK with the PLAN being rewritten to make the CAM the brain,
   discarding the "C6 is the brain / CAM is a vision coprocessor" framing.
3. They are OK with the missing `esp32_screen_only.ino` references in the
   PLAN being deleted (rather than us synthesising a file to match them).
4. They are OK with `.gitkeep` being removed during the restructure.

Pausing here per the Task 0 instruction. No files outside this one have been
modified.
