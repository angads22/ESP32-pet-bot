# PetBot — kinematics

This is the math behind the dog's motion. Two systems:

1. **Legs** — 3-DOF inverse kinematics, one IK solve per leg per tick.
2. **Head** — pan/tilt; no IK needed, just angle setpoints, plus an optional
   visual-servoing P-controller for camera-driven tracking.

The leg IK below is what `dog_ik.cpp` will implement in Phase B (port from
Freenove's `cooToA()`). The head section is what `dog_head.cpp` will do in
Phase L.

---

## 1. Leg geometry & coordinate frame

Each leg has 3 servos: **HIP** (yaw), **THIGH** (shoulder pitch),
**CALF** (knee pitch). Three rigid links:

```
                    BODY
                     │
                     │   ▲ +z (up)
                     │   │
            ●────────●   │   ●─── shoulder bracket (bolted to body)
            │        │   │   │
            │   L1   │   │   │   L1 = root offset (body-mount → hip yaw axis)
            │        │   │   │        Freenove: 23 mm
            │        ●───┼───●   ● HIP servo (yaws the leg horizontally)
            │            │       │
            │            │       │   L2 = thigh
            │            │       │        Freenove: 55 mm
            │            │       ●─── THIGH servo (shoulder pitch)
            │            │       │
            │            │       │
            │            │       │   L3 = calf (shin)
            │            │       │        Freenove: 59 mm
            │            │       ●─── CALF servo (knee pitch)
            │            │       │
            │            ▼ −z    ●─── foot tip
            │
            └─── horizontal plane (x-y)
```

Per-leg local frame (origin at the hip yaw axis, body-relative):

- `+x` points outward from the body sideways
- `+y` points forward along the body
- `+z` points up

The IK target is the **foot-tip position `(x, y, z)` in this local frame**.

> Freenove's neutral stand: each foot at roughly `(10, 99, ±10)` mm in their
> reference, depending on leg. Keep their constants verbatim — they were
> tuned for this body.

## 2. Closed-form 3-DOF IK

The classic mammal-leg IK in three steps:

### Step 1 — HIP yaw

The HIP servo rotates the entire leg about the vertical (z) axis. Look at
the leg from above:

```
                    +y (forward)
                     ▲
                     │
         leg_plane ──┼── θ_hip
                     │
        ─────────────●──────────────► +x (outboard)
                   hip yaw axis
```

After the HIP rotates by `θ_hip`, the THIGH and CALF live in a tilted
*vertical* plane. We choose `θ_hip` so the foot lies in that plane.

```
  θ_hip = atan2(x, y)        (radians)
```

Notes:
- `atan2` handles all quadrants automatically.
- The L1 offset (body to hip-yaw axis) is **already** baked into the local
  frame, so no L1 term appears in this equation. L1 shows up in the Freenove
  code because they sometimes work in body coordinates instead of leg-local
  coordinates. Pick one convention and stick to it.

### Step 2 — Project into the leg's vertical plane

Now collapse the (x, y) point into a single radial distance `r` along the
yawed leg-plane:

```
  r = sqrt(x² + y²)         (after subtracting any L1 term in body frame)
```

The 2-link THIGH–CALF problem is now planar: hit the point `(r, z)` with
two links of lengths `L2` and `L3` connected at the knee.

### Step 3 — 2-link planar IK (law of cosines)

Distance from hip to foot in the leg plane:

```
  D = sqrt(r² + z²)
```

**Reachability check** — the only useful sanity check in IK:

```
  if D > L2 + L3:   foot is too far away — clamp or refuse
  if D < |L2 − L3|: foot is too close — clamp or refuse
```

Knee angle (interior angle between THIGH and CALF at the knee), via law of
cosines on the triangle with sides `L2, L3, D`:

```
  cos(α_knee_interior) = (L2² + L3² − D²) / (2·L2·L3)
  α_knee_interior      = acos( clamp(...) )
```

Convert to a servo angle. A "knee bent = 0°" convention means:

```
  θ_calf = π − α_knee_interior         (servo angle, knee pitch)
```

Thigh angle (shoulder pitch). Two pieces:

1. The angle from horizontal up to the line `hip → foot`:
   ```
   φ = atan2(z, r)        (negative when foot is below hip — usually true)
   ```
2. The angle from that line to the THIGH itself (law of cosines on triangle
   `L2, D, L3`):
   ```
   ψ = acos( clamp( (L2² + D² − L3²) / (2·L2·D) ) )
   ```

Then:

```
  θ_thigh = φ + ψ        (or φ − ψ, depending on knee-up vs knee-down convention)
```

Pick the convention that matches the physical knee direction on your dog.
Freenove uses knee-up, so `φ + ψ` for the front-right and front-left, with a
sign flip on the rear pair. If the knee folds backwards in simulation, flip
the sign.

### Pseudocode

```cpp
struct LegAngles { float hip, thigh, calf; };

bool leg_ik(float x, float y, float z, LegAngles& out) {
    constexpr float L2 = 55.0f, L3 = 59.0f;

    float hip = atan2f(x, y);
    float r   = sqrtf(x*x + y*y);
    float D   = sqrtf(r*r + z*z);

    if (D > L2 + L3 - 0.5f) return false;              // unreachable, clamp
    if (D < fabsf(L2 - L3) + 0.5f) return false;

    float cos_knee = (L2*L2 + L3*L3 - D*D) / (2.0f * L2 * L3);
    cos_knee = fmaxf(-1.0f, fminf(1.0f, cos_knee));
    float knee_interior = acosf(cos_knee);

    float phi = atan2f(z, r);                          // usually negative (z < 0)
    float cos_psi = (L2*L2 + D*D - L3*L3) / (2.0f * L2 * D);
    cos_psi = fmaxf(-1.0f, fminf(1.0f, cos_psi));
    float psi = acosf(cos_psi);

    out.hip   = hip;
    out.thigh = phi + psi;
    out.calf  = (float)M_PI - knee_interior;
    return true;
}
```

## 3. Right-leg inversion + calibration injection

Two more transforms before the angle reaches a servo:

### Right-leg inversion

The right legs are mirror-imaged. Freenove handles this by inverting *just
the angle going to the servo*:

```cpp
float servo_angle = (is_right_leg) ? (M_PI - ik_angle) : ik_angle;
```

Specifically the channels listed as needing inversion in their firmware:
**9, 10, 14**. (The hip on the rear-right doesn't get inverted — depends on
the motor mounting orientation. Verify per-servo with the calibration sweep.)

### Per-servo calibration trim

Mechanical assembly is never perfect. Each servo has a small angular offset
from "ideal":

```cpp
float trimmed = servo_angle + offset[leg][joint];      // radians
```

Offsets live in NVS under key `KEY_SERVO_OFFSET` (Freenove uses the same key
— keep their convention). Offsets are set during the per-servo sweep
calibration in `CALIBRATION.md`.

### Final mapping to PCA9685 pulse width

PCA9685 wants a microsecond pulse:

```cpp
constexpr int  US_MIN = 500, US_MAX = 2500;     // 0° → 500 µs, 180° → 2500 µs
constexpr int  CHANNEL_COUNT = 16;
constexpr float TWO_PI = 6.283185307f;

int us = US_MIN + (int)((trimmed * (180.0f / (float)M_PI) / 180.0f)
                        * (US_MAX - US_MIN));
us = clamp(us, US_MIN, US_MAX);
pca.setPWM(channel, 0, us_to_ticks(us));
```

(`us_to_ticks` = `us * 4096 / 20000` for 50 Hz.)

## 4. Workspace & step constraints

Don't trust the IK to keep you safe. Bound the foot trajectory:

- **Max step height**: Freenove uses `15 mm`. Going higher means the gait
  table generates points the leg can't reach (or just looks unstable).
- **Active radius**: ~104 mm. Keep `D` well under `L2 + L3 = 114 mm`.
- **Step length**: 0–20 mm per cycle.
- **Speed**: 1–8 mm / 10 ms tick. Above 8 mm/tick the servos can't keep up
  and they audibly chatter.

If the IK returns "unreachable", the gait task should hold the previous
angle (don't crash, don't overshoot — just freeze that leg until the
trajectory comes back into the workspace).

## 5. Gait — putting IK on a clock

The gait engine is *not* IK. Its job is to generate a sequence of
foot-tip points `(x, y, z)` for each leg over time, then call `leg_ik()` per
leg per tick. Freenove's `move_any(alpha, stepLength, gamma, speed)` does
exactly that: it computes per-leg trajectories given the omnidirectional
walk parameters.

A single 50 Hz tick:

```
for each leg in {FL, FR, BL, BR}:
    (x, y, z) = trajectory(leg, phase, alpha, stepLength, gamma)
    leg_ik(x, y, z, &angles)
    apply right-leg inversion + calibration trim
    pca.setPWM(leg.hip_ch,   ...,   us(angles.hip))
    pca.setPWM(leg.thigh_ch, ...,   us(angles.thigh))
    pca.setPWM(leg.calf_ch,  ...,   us(angles.calf))
```

Phase B ports `move_any` verbatim and replaces the blocking
`delay(TICK_MS)` with this 50 Hz FreeRTOS tick.

---

## 6. Head — no IK, just angles

Two servos on PCA9685 channels 11 (pan) and 12 (tilt). No kinematic chain
to solve — each axis is independent.

```cpp
void head_set_pan(float deg);    // clamp to HEAD_PAN_MIN..HEAD_PAN_MAX
void head_set_tilt(float deg);   // clamp to HEAD_TILT_MIN..HEAD_TILT_MAX
```

Important: the **OV2640 ribbon cable** twists with pan rotation. Hard limit
to ±90° in firmware AND constrain mechanically with end-stops. Past ±90°
the ribbon kinks and signal integrity drops; past ±180° it tears.

### Visual servoing — pointing the head at a detection

When the recog server returns a face / object bounding box, project its
center back to angular error and feed a P-controller:

```
camera frame:
   (0,0)──────────────► +u (px)
       │
       │      ●  ← detection center (cu, cv)
       │
       ▼ +v (px)
   (W, H)
```

Image center: `(W/2, H/2)`. Pixel error from center:

```
  e_u = cu - W/2          (+ means subject is to the right of center)
  e_v = cv - H/2          (+ means subject is below center)
```

Convert pixels to degrees using the **camera FOV**. The OV2640 with the
default Freenove lens is roughly:

```
  HFOV ≈ 60°,  VFOV ≈ 45°
  deg_per_px_u = HFOV / W
  deg_per_px_v = VFOV / H
```

Angular error:

```
  Δpan_deg  = e_u * deg_per_px_u
  Δtilt_deg = e_v * deg_per_px_v
```

P-controller with rate-limit (don't slew the head at full servo speed —
makes the camera unusable):

```cpp
constexpr float Kp           = 0.4f;     // 0.0..1.0; tune
constexpr float MAX_DEG_TICK = 2.0f;     // max move per 50 ms tick

void head_track_target(float cu, float cv, float W, float H) {
    float e_u = cu - W * 0.5f;
    float e_v = cv - H * 0.5f;
    float dpan  = clampf(Kp * e_u * (HFOV / W), -MAX_DEG_TICK, MAX_DEG_TICK);
    float dtilt = clampf(Kp * e_v * (VFOV / H), -MAX_DEG_TICK, MAX_DEG_TICK);
    head_set_pan(  current_pan  - dpan);     // sign chosen so positive e_u pans LEFT to recenter
    head_set_tilt(current_tilt + dtilt);     // image y points down, head tilt up positive
}
```

Sign of `dpan` / `dtilt` depends on which way the head is mounted. Walk the
calibration once (move target right → head should pan right), flip signs
until correct.

### Tracking modes

Three modes the firmware supports:

1. **`HEAD_MANUAL`** — pan/tilt set by app sliders or PS5 right stick.
2. **`HEAD_TRACK`** — visual-servoing on the latest recognition event from
   the home-PC recog server. Decay to neutral after 2 s of no detection.
3. **`HEAD_LOOK_AROUND`** — slow sinusoidal pan ± look-up tilt during
   `IDLE_AUTO`, breaks the "frozen statue" look.

The mode is just a state in `app_state`; the head task obeys whichever is
active.

---

## 7. What ports verbatim from Freenove vs. needs rewrite

| Concept | Source | Status |
|---|---|---|
| Body geometry (`L1=23, L2=55, L3=59` mm) | Freenove `RobotDefinitions.h` | Port verbatim |
| `cooToA()` 3-DOF IK | Freenove `Motion.cpp` | Port verbatim, swap `delay(TICK_MS)` for FreeRTOS tick |
| `move_any(alpha, stepLength, gamma, speed)` trajectory generator | Freenove `Motion.cpp` | Port verbatim |
| Right-leg inversion (`180° − a` on ch 9, 10, 14) | Freenove | Port verbatim |
| Per-servo NVS calibration table (`KEY_SERVO_OFFSET`) | Freenove | Port verbatim — even the NVS key, so their calibration tool stays compatible |
| Dance routines (`danceSayHello` etc.) | Freenove `DanceMovements.cpp` | Defer — pose tables port cleanly but not needed for v1 |
| Head pan/tilt | n/a (not in Freenove) | New — math is trivial (§6 above) |
| Visual servoing P-controller | n/a | New — §6 |

That's the math. Everything else is just keeping the IK fed with sane
trajectories at 50 Hz.
