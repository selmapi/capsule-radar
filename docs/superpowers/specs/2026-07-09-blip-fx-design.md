# Capsule Radar — Blip Enhancements (per-theme)

**Date:** 2026-07-09
**Repo:** `selmapi/capsule-radar`, branch `feature/blip-fx`
**Builds on:** the descriptor engine + `BlipShape` from theme-pack-2.

## Goal

Five blip enhancements, assigned per theme (Selma's "spread across themes" direction):

| Enhancement | Themes | Control |
|---|---|---|
| **Fade-in** (new contact eases in over ~1s) | All | always-on |
| **Pulse** (subtle ring on new; red ring on emergency squawk 7500/7600/7700) | All | always-on |
| **ATC leader line** (thin connector blip→label) | Military, CIC, ClaudeIC | per-theme |
| **Heading-vector line** (forward line, length = groundspeed) | Military, CIC, ClaudeIC | per-theme |
| **Altitude-scaled size** (glyph size from altitude) | Mission Control | per-theme |

The three tactical themes (Military/CIC/ClaudeIC) become a console: **dot + forward speed-vector + back-line to label**. Mission Control gets altitude depth. Every theme gets fade-in + emergency pulse.

## Architecture

### Per-theme `blipFx` bitfield on `ThemeDesc`
```cpp
enum BlipFx : uint8_t { FX_LEADER = 1<<0, FX_VECTOR = 1<<1, FX_ALTSIZE = 1<<2 };
```
Add `uint8_t blipFx;` to `ThemeDesc` (after `outline`). Values:
- Military(3), CIC(8), ClaudeIC(9): `FX_LEADER | FX_VECTOR`
- Mission Control(7): `FX_ALTSIZE`
- all others: `0`

Fade-in + pulse are NOT flags — they're global (all themes) in `ac_draw_cb`. No new web toggles (it's all per-theme + always-on, as requested).

### First-seen temporal state (for fade-in + new-pulse)
Add `uint32_t firstSeenMs;` to `AcDraw`. In `update()`, alongside the existing `prevPos` map (hex→pos, line ~994), maintain a persistent `static std::map<std::string,uint32_t> s_firstSeen`:
- For each incoming aircraft, `key = ac.hex`. If `key` not in `s_firstSeen`, insert `s_firstSeen[key] = lv_tick_get()`. Set `d.firstSeenMs = s_firstSeen[key]`.
- After building the new set, prune `s_firstSeen` entries whose hex is no longer present (mirror the trail-map cleanup). So an aircraft that leaves and returns fades in again.
`age = lv_tick_get() - firstSeenMs` drives fade-in and the one-shot new-pulse.

## Rendering (all in `ac_draw_cb`, `src/radar_view.cpp`)

- **Fade-in (global):** `opa = (age >= FADEIN_MS) ? 255 : (255*age/FADEIN_MS)` (FADEIN_MS≈900). Apply to the blip shape fill and its floating labels. Thread `opa` into `blip_fill_dsc` (add an opacity arg) and into the label descriptors; the kite/vector/orb paths also honor it. Fully faded-in aircraft (the common case) get 255 → no change from today.
- **Pulse (global):** if `age < PULSE_MS` (≈900), draw one expanding ring: radius `4 + (age/PULSE_MS)*18`, opacity fading `~200→0`, color `ac.color`. If `ac.emergency`, draw a **repeating** red expanding ring keyed off `s_frameCtr` (color `COL_EMERG`) — this augments, doesn't replace, the existing static emergency halo. Both are cheap arcs.
- **Heading-vector (FX_VECTOR):** from `ac.pos`, draw a line along `ac.track` of length `clamp(ac.gsKt * VEC_SCALE, VEC_MIN, VEC_MAX)` (e.g. scale so ~500kt ≈ 26px, cap 30px), width 2, color `ac.color`. Points forward — opposite the (backward) trail, so no clash. Skipped for `onGround`/`gsKt<~40`.
- **ATC leader line (FX_LEADER):** a thin (width 1) dim line from `ac.pos` to the anchor of the floating call/alt label (the existing `a1` label box top-left, ~`ac.pos + (12,-14)`), color `s_cSoft` at reduced opacity. Only where labels are drawn (non-orb).
- **Altitude-scaled size (FX_ALTSIZE):** a scale factor `s = lerp(1.35 at 0ft → 0.75 at 40000ft)` (clamped) multiplying the glyph's local shape coordinates (GX/GY for the kite, and the shape helpers' local points). Applied only to the marker geometry, not the labels. For `kAuto`/kite and the shape helpers, pass the scale through.

Order in the per-aircraft draw: trail → pulse ring(s) (behind) → heading-vector → glyph (with fade opacity + altsize) → emergency halo → selection ring → leader-line + labels.

## Surfaces

| File | Change |
|------|--------|
| `src/theme_table.h` | `BlipFx` enum; `uint8_t blipFx` on `ThemeDesc` |
| `src/theme_table.cpp` | `blipFx` value on all 15 rows (4 non-zero) |
| `src/radar_view.cpp` | `firstSeenMs` on `AcDraw`; `s_firstSeen` map in `update()`; fade/pulse/vector/leader/altsize in `ac_draw_cb`; opacity + scale threaded through the blip-shape helpers |
| (constants) | `FADEIN_MS`, `PULSE_MS`, `VEC_SCALE/MIN/MAX`, altsize lerp bounds — named `constexpr` at top of the draw section for tuning |

`static_assert(sizeof/THEME_COUNT)` already guards the table; the new field just extends each row.

## Verification

- `pio run -e native` + `-e esp32-s3-amoled-175` compile per task.
- These are pure rendering and DO render in the SDL sim (fed by `sim_main.cpp`'s synthetic aircraft), but the sim window isn't observable from the build host — so functional acceptance is **on-device** (Selma): confirm fade-in on new contacts, the emergency pulse, the tactical console look (vector + leader) on Military/CIC/ClaudeIC, and altitude-size on Mission Control.
- Independent Opus review on the temporal state (first-seen tracking, fade/pulse) + the opacity/scale threading (regression risk to the existing glyph paths).
- Then: bump `FW_VERSION` → 1.6.0, flash, redeploy the flasher.

## Out of scope / deferred

No web toggles (per-theme by design). A global "animations off" switch only if Selma finds fade/pulse distracting. Sound & alerts pack and Idle & polish pack remain queued. The trails-scatter bug is being fixed in a separate session.
