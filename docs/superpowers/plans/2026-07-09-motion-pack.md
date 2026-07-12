# Motion Pack — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** IMU gestures — shake-to-refresh, auto-rotate (upright-gated), wake-on-motion — all NVS-toggleable at capsuleradar.local; plus an instant-blip-recolor fix on theme change.

**Architecture:** Extend the accel driver to read X/Y/Z; a new `motion` module samples at ~30 Hz and derives shake/orientation/wake/face-down from one read; `main.cpp` wires the effects + web toggles.

**Tech Stack:** C++/Arduino/LVGL, PlatformIO. `motion.cpp`/`imu_qmi8658.cpp` are **device-only** (use `Wire`/`millis`) → they compile in `-e esp32-s3-amoled-175` only, NOT the native env (do not add them to `[env:native]` build_src_filter). The blip fix is in `radar_view.cpp` (compiles in both). **The IMU features have no SDL-sim equivalent** — per-task verification is *compilation* + code-trace; functional acceptance is the on-device tuning session (Task 5).

**Ground rules:** work in `/Users/selmapittman/Documents/Claude/Projects/capsule-radar`, branch `feature/motion-pack`; commit per task; don't push (flash/redeploy at the end).

---

## Task 1: Instant blip recolor on theme change (the reported lag fix)

**Files:** `src/radar_view.cpp` (`setTheme`)

- [ ] **Step 1** — In `setTheme()`, just before the existing `flow_redraw_all();` call, add:
```cpp
    // Recolor already-drawn blips for the new palette; otherwise each AcDraw keeps its
    // cached color (set at poll time in update()) until the next ADS-B poll (~seconds).
    for (AcDraw &ac : s_acs) ac.color = alt_color(ac.altFt, ac.onGround);
    if (s_acLayer) lv_obj_invalidate(s_acLayer);
```
Confirm `AcDraw` has `float altFt;` and `bool onGround;` fields (it does) and that `alt_color` is declared above `setTheme` (it is). `alt_color` already honors the active theme's mono/ramp via `s_desc`.

- [ ] **Step 2 — Verify:** `pio run -e native` and `pio run -e esp32-s3-amoled-175` → SUCCESS. Code-trace: on any theme switch, every visible blip's color is recomputed and the aircraft layer invalidated → blips flip immediately instead of on the next poll.

- [ ] **Step 3 — Commit:** `git add src/radar_view.cpp && git commit -m "fix(themes): recolor blips instantly on theme change (was lagging until next poll)"`

---

## Task 2: Full accel-vector read on the IMU driver

**Files:** `src/imu_qmi8658.h`, `src/imu_qmi8658.cpp`

- [ ] **Step 1 — `imu_qmi8658.h`:** add `bool imu_read_accel(float *ax, float *ay, float *az);` (returns g units; false on I2C read failure — same defensive contract as `imu_facedown`).

- [ ] **Step 2 — `imu_qmi8658.cpp`:** implement it by reading 6 bytes from `0x35` (AX_L) through `0x3A` (AZ_H) in one burst (auto-increment is already enabled in `imu_begin`), converting each int16 with `/ 16384.0f` (±2 g ⇒ 16384 LSB/g):
```cpp
bool imu_read_accel(float *ax, float *ay, float *az) {
    if (!s_ok) return false;
    uint8_t b[6];
    if (!rd(0x35, b, 6)) return false;   // AX_L..AZ_H, auto-increment
    const int16_t rx = (int16_t)((b[1] << 8) | b[0]);
    const int16_t ry = (int16_t)((b[3] << 8) | b[2]);
    const int16_t rz = (int16_t)((b[5] << 8) | b[4]);
    *ax = rx / 16384.0f; *ay = ry / 16384.0f; *az = rz / 16384.0f;
    return true;
}
```
Leave `imu_facedown()` in place (harmless; the motion module will supersede its use).

- [ ] **Step 3 — Verify:** `pio run -e esp32-s3-amoled-175` → SUCCESS. (native env doesn't build this file.)

- [ ] **Step 4 — Commit:** `git add src/imu_qmi8658.h src/imu_qmi8658.cpp && git commit -m "feat(imu): read full accel vector (X/Y/Z)"`

---

## Task 3: The `motion` module (gesture logic)

**Files:** `src/motion.h`, `src/motion.cpp` (new); `platformio.ini` (ensure NOT added to native filter — it uses Arduino)

- [ ] **Step 1 — `motion.h`:** the public surface (exactly):
```cpp
#pragma once
#include <stdbool.h>
void motion_begin();
void motion_update();                 // call every loop(); self-limits to ~30 Hz
bool motion_facedown();               // orientation-agnostic face-down (replaces imu_facedown use)
bool motion_take_shake();             // edge: true once per shake, then clears
int  motion_orientation();            // 0/90/180/270, or -1 = flat/unknown (hold last)
bool motion_take_wake();              // edge: true once if motion seen since last call
void motion_set_enabled(bool shake, bool autorotate, bool wake);
```

- [ ] **Step 2 — `motion.cpp`:** implement with all thresholds as named constants at the top (for the tuning pass) and a `#ifndef MOTION_DEBUG`/serial-log path. Structure:
  - `motion_begin()` → `imu_begin()`.
  - `motion_update()`: `if (millis() - s_last < 33) return; s_last = millis();` then `imu_read_accel(&ax,&ay,&az)` (return on failure — noisy-bus safe). Update sub-detectors:
    - **face-down**: port the existing orientation-agnostic baseline from `imu_facedown` but drive it off `az` from this read (slow EMA of resting az; face-down when az swings to the far opposite side). Store as `s_facedown`.
    - **shake**: `float m = sqrtf(ax*ax+ay*ay+az*az); float dev = fabsf(m - 1.0f);` maintain a short ring of recent `dev` crossings of `SHAKE_G` (0.6f); if ≥ `SHAKE_MIN_SWINGS` (3) alternating strong swings within `SHAKE_WINDOW_MS` (600) and now > `SHAKE_COOLDOWN_MS` (2000) since the last fire → set a latched `s_shakePending = true`, stamp cooldown.
    - **orientation**: `float ip = sqrtf(ax*ax+ay*ay);` if `ip < TILT_G` (0.4f) → candidate = -1 (flat). else `float deg = atan2f(AY_SIGN*ay, AX_SIGN*az_or_ax...)` — compute the in-plane down-angle and snap to nearest of {0,90,180,270}. **Axis mapping constants** `AX_SIGN`, `AY_SIGN`, and which physical axes are screen-X/Y are named `#define`s to flip during on-device calibration; document that clearly. Commit a new orientation only after the same non-(-1) candidate has held for `ROT_HOLD_MS` (1000); expose via `motion_orientation()` (returns the committed value, or -1 if currently flat/uncommitted).
    - **wake**: keep a slow baseline `s_wakeBase` of `m`; if `fabsf(m - s_wakeBase) > WAKE_G` (0.15f) set latched `s_wakePending = true`; ease `s_wakeBase` toward `m`.
  - `motion_take_shake()`/`motion_take_wake()`: return-and-clear the latched flag.
  - `motion_set_enabled(...)`: store 3 bools; when a feature is disabled its detector still runs but the getters/edges return false/no-op (simplest: gate in the getters).
  Keep the file focused and the detectors independent; comment the axis-mapping calibration prominently.

- [ ] **Step 3 — Verify:** `pio run -e esp32-s3-amoled-175` → SUCCESS. Confirm `platformio.ini` `[env:native]` filter was NOT changed (motion.cpp is Arduino-bound, firmware-only; the `+<*>` firmware env picks it up automatically).

- [ ] **Step 4 — Commit:** `git add src/motion.h src/motion.cpp && git commit -m "feat(motion): IMU gesture module (shake/orientation/wake/face-down at 30Hz)"`

---

## Task 4: Wire motion into main.cpp + web toggles

**Files:** `src/main.cpp`

- [ ] **Step 1 — Boot + loop:** replace `imu_begin()` (~line 831) with `motion_begin()`; add `motion_update();` early in `loop()`. Replace the `imu_facedown()` call (~line 1020) with `motion_facedown()`.

- [ ] **Step 2 — Shake → force poll + confirm:** add `static volatile bool g_forcePoll = false;`. In `loop()`: `if (g_motionShake && motion_take_shake()) { g_forcePoll = true; /* brief visual pulse via the existing center pulse or a short label */ }`. In the core-0 ADS-B task (around line 129), before the interval check: `if (g_forcePoll) { g_forcePoll = false; if (nowMs - lastPoll >= 1500) lastPoll = 0; }` (force a poll but respect ~1.5 s API politeness; `lastPoll = 0` triggers the immediate-poll path already used for range changes).

- [ ] **Step 3 — Auto-rotate:** in `loop()` (gate on `g_motionRotate`): `int o = motion_orientation(); if (o >= 0 && o != display::currentRotation()) display::setRotation(o);` (use the display module's existing rotation setter + whatever "current rotation" accessor exists; if none, track it in a static). Confirm `display::setRotation` remaps touch (it does).

- [ ] **Step 4 — Wake-on-motion:** in the idle-dim block (~line 1024), if `g_motionWake && motion_take_wake()` while currently dimmed → poke activity so it un-dims (call the same path a touch uses, e.g. `lv_disp_trig_activity(NULL)` or reset the inactivity source; match how touch currently keeps it awake).

- [ ] **Step 5 — Web settings + NVS:** add three checkboxes (Auto-rotate / Shake to refresh / Wake on motion) to the inline settings HTML, default checked; read/write NVS keys `mrot`/`mshake`/`mwake` via `Preferences` (mirror the `idledim` setting's load in setup + save in the POST handler); keep `g_motionRotate/g_motionShake/g_motionWake` in sync and call `motion_set_enabled(g_motionShake, g_motionRotate, g_motionWake)` at boot and on save.

- [ ] **Step 6 — Verify:** `pio run -e esp32-s3-amoled-175` → SUCCESS. Code-trace each wire-up (shake→forcePoll→lastPoll=0; orientation→setRotation; wake→activity poke; toggles persist + gate).

- [ ] **Step 7 — Commit:** `git add src/main.cpp && git commit -m "feat(motion): wire shake/auto-rotate/wake into main + web toggles"`

---

## Task 5: Bump version, flash, on-device tuning, redeploy

- [ ] **Step 1** — Bump `FW_VERSION` (`1.4.0` → `1.5.0`); commit.
- [ ] **Step 2** — Merge `feature/motion-pack` → main, push; flash `pio run -e esp32-s3-amoled-175 -t upload` (warn Selma), confirm boot via serial.
- [ ] **Step 3 — On-device tuning (with Selma, `MOTION_DEBUG` on):** shake → confirm it re-polls without false-firing; turn upright → confirm rotation snaps correctly (fix `AX_SIGN`/`AY_SIGN`/axis mapping if it rotates the wrong way); lay flat → confirm it holds (no spurious rotate); idle-dim then nudge → confirm wake. Tune `SHAKE_G`/`TILT_G`/`WAKE_G`/holds. Toggle each off at capsuleradar.local → confirm it stops.
- [ ] **Step 4** — `gh workflow run webflasher.yml --repo selmapi/capsule-radar --ref main` to redeploy the flasher at v1.5.0.

---

## Self-review checklist
- `motion.cpp`/`imu_qmi8658.cpp` are firmware-only (Arduino) — not added to `[env:native]`. ✓
- Shake respects API politeness (2 s cooldown + ≥1.5 s since last poll). ✓
- Auto-rotate holds when flat (`ip < TILT_G` → -1) and debounces (`ROT_HOLD_MS`). ✓
- All three gated by NVS toggles, default on. ✓
- Blip-recolor fix is independent (Task 1), sim-verifiable, addresses the reported lag. ✓
- Axis mapping for auto-rotate is a documented on-device calibration (named sign constants). ✓
