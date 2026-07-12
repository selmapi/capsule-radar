# Capsule Radar — Motion Pack (IMU gestures) + blip-recolor fix

**Date:** 2026-07-09
**Repo:** `selmapi/capsule-radar`, branch `feature/motion-pack`
**Hardware:** QMI8658 6-axis IMU (accel used; gyro not needed) on the shared I2C bus.

## Goal

Use the accelerometer (today only accel-Z is read, for face-down sleep) to add three motion features, all toggleable at `http://capsuleradar.local/` and persisted in NVS:
1. **Shake-to-refresh** — shake → immediate ADS-B re-poll + brief on-screen confirmation.
2. **Auto-rotate** — turn the gadget → UI snaps to nearest 90°, but only when upright enough to sense orientation.
3. **Wake-on-motion** — while idle-dimmed, any nudge wakes the screen.

Plus a bundled fix: **blips recolor instantly on theme change** (today they lag a few seconds).

## Architecture — one sampled read feeds everything

Today `imu_facedown()` reads accel-Z itself at ~400 ms. We replace that with a small **motion module** that samples the full accel vector at ~30 Hz and derives all motion state (face-down included) from that one read. This unifies the logic and is the "approach A" chosen over the chip's hardware motion engine (simpler, fully tunable).

### `src/imu_qmi8658.{h,cpp}` — expose the full vector
Add `bool imu_read_accel(float *ax, float *ay, float *az);` reading X/Y/Z (regs 0x35–0x3A → g units, ±2 g ⇒ 16384 LSB/g). Keep `imu_begin()`. `imu_facedown()` may stay or be superseded by the motion module (see below); the module will own the sampling.

### `src/motion.{h,cpp}` (new) — the gesture brain
`motion_update()` is called from the main loop, rate-gated to ~30 Hz. It reads the accel vector once and updates internal state. Public surface:
```cpp
void  motion_begin();                 // wraps imu_begin()
void  motion_update();                // call from loop(); ~30 Hz internally (millis-gated)
bool  motion_facedown();              // replaces imu_facedown() semantics (orientation-agnostic baseline preserved)
bool  motion_take_shake();            // edge: true once per detected shake, then clears
int   motion_orientation();           // 0/90/180/270, or -1 when "flat / can't tell" (hold last)
bool  motion_take_wake();             // edge: true once when motion seen since last call (for wake)
void  motion_set_enabled(bool shake, bool autorotate, bool wake);  // from web settings
```
- **Shake**: compute `|a|` (g). A shake = ≥ K strong deviations (`||a|-1| > SHAKE_G`, e.g. 0.6 g) with alternating sign inside a ~600 ms window; then a ~2 s cooldown (also protects the ADS-B API's ~1 req/1–2 s politeness). Emits one edge via `motion_take_shake()`.
- **Auto-rotate**: the in-plane gravity direction is `atan2(ay, ax)` (screen-plane axes). Snap to nearest 90° → candidate orientation. **Upright gate:** only trust it when `sqrt(ax²+ay²) > TILT_G` (e.g. 0.4 g); below that (device flat) return -1 = "hold last". **Hysteresis:** only commit a new orientation after it's held ~1 s. NOTE: the accel-axis → screen-axis mapping (which axis is screen-X, and signs) depends on how the sensor is mounted and is **calibrated on-device** — the module exposes the mapping as named constants to flip during tuning.
- **Wake**: track `|a|` deviation frame-to-frame; `motion_take_wake()` returns true if any deviation exceeded `WAKE_G` (small, e.g. 0.15 g) since the last call.
- All thresholds are named constants at the top of `motion.cpp` for the tuning pass. A `MOTION_DEBUG` compile flag logs `ax/ay/az`, `|a|`, and each detected event to serial.

### `src/main.cpp` — wire it in
- Call `motion_begin()` where `imu_begin()` is today (line ~831); call `motion_update()` in the loop (it self-limits to 30 Hz).
- Replace the direct `imu_facedown()` call (line ~1020) with `motion_facedown()`.
- **Shake** → if `motion_take_shake()` and shake enabled: set a shared `volatile bool g_forcePoll` that the core-0 ADS-B task checks; the task forces an immediate poll by resetting `lastPoll = 0` (same mechanism a range change already uses at line ~122), then clears the flag — but only if ≥ ~1.5 s since the last poll (politeness). Also trigger a brief visual confirmation (a one-shot ring/center pulse via the existing `pulse` object or a short "⟳" label).
- **Auto-rotate** → if enabled and `motion_orientation()` returns a committed 0/90/180/270 different from current: `display::setRotation(...)` (already supports 4-way + touch remap; 90/270 use the existing PSRAM rot buffer).
- **Wake** → if enabled and `motion_take_wake()` while idle-dimmed: poke the activity timer so the idle-dim logic (line ~1024, `display::inactiveMs()`) un-dims — same effect a touch has.

### Web settings (`capsuleradar.local`, inline HTML in `main.cpp`)
Three checkboxes — **Auto-rotate**, **Shake to refresh**, **Wake on motion** — default **on**, persisted in NVS via `Preferences` (keys e.g. `mrot`/`mshake`/`mwake`), applied through `motion_set_enabled(...)` at boot and on save. Same pattern as the existing `idledim`/units/theme settings.

## Bundled fix — instant blip recolor on theme change

Root cause: each `AcDraw.color` is computed once per ADS-B poll (`alt_color(...)`), and `setTheme()` re-tints chrome but never recomputes the cached blip colors, so blips keep their old color until the next poll (~a few seconds). Fix in `setTheme()` (radar_view.cpp), near the existing `flow_redraw_all()`:
```cpp
for (AcDraw &ac : s_acs) ac.color = alt_color(ac.altFt, ac.onGround);
if (s_acLayer) lv_obj_invalidate(s_acLayer);
```
(`altFt`/`onGround` are already stored on each `AcDraw`; `alt_color` honors the new theme's mono/ramp.) Blips now flip instantly with the rest of the scope.

## Verification (honest)

- The SDL native sim has **no accelerometer**, so the gestures can't be exercised in software — the sim/firmware must **compile**, and functional acceptance is **on-device**.
- On-device tuning session (with `MOTION_DEBUG` serial logging): dial `SHAKE_G`/window/cooldown, the auto-rotate axis mapping + `TILT_G` + hold time, and `WAKE_G`. Expect one iteration.
- The blip-recolor fix IS sim-verifiable (compile + it's a deterministic render change) and confirmed on-device by cycling themes and watching blips flip immediately.
- Then: bump `FW_VERSION`, flash, redeploy the web flasher.

## Out of scope / deferred

Tilt-to-zoom (too twitchy — Selma's call). The other feature packs (Sound & alerts, Idle & polish, Blip enhancements) remain queued. Hardware motion-engine offload (approach B) not pursued.
