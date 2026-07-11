# Capsule Radar — Theme Pack 2 (5 pop-culture themes + blip shapes + distance-box fix)

**Date:** 2026-07-09
**Repo:** `selmapi/capsule-radar`, branch `feature/theme-pack-2`
**Builds on:** the descriptor-table theme engine from theme-pack 1 (10 themes → this adds 5, total 15).

## Goal

Add 5 themes inspired by Borderlands 4, Aliens, Mass Effect, Top Gun, and Firefox — and, in the process, introduce a per-theme **blip shape** so markers aren't locked to the rotated-quad "kite." Also fix a theme-consistency bug: the bottom distance/zoom box stays green across theme changes.

## The 5 themes (indices 10–14, `THEME_COUNT` → 15)

| # | Theme | Scope | Blip shape | Blips | Sweep | Notes |
|---|-------|-------|-----------|-------|-------|-------|
| 10 | **Borderlands** | Rings | **Diamond + outline** | mono crimson `#E5342A` | off | Cel-shaded HUD: steel-blue field, orange accents, black-outlined diamonds |
| 11 | **Aliens** | Rings | auto (kite) | mono lime `#A8E03C` | **on** | Motion-tracker green on black, teal accents, the sweep is the point |
| 12 | **Mass Effect** | Rings | auto (kite) | altitude ramp | on | Holo-blue chrome, deep-space field, Renegade-red emergencies |
| 13 | **Top Gun** | Rings | **Silhouette** | mono muted-red `#B2413A` | on | Steel blue-grays, overcast; a fleet of little jets |
| 14 | **Firefox** | **Vector** | **Chevron** | mono red `#FF2A1A` | off | Atari vector-arcade: orange grid + bearing ring, red chevrons on black |

### Exact palettes (logical RGB → `lv_color_hex`)

- **Borderlands** — ring `#AEC9DB`, lead `#FF9A2E`, ink `#EAF2F8`, soft `#AEC9DB`, bg `#16212B`, layer `#4E7A9E`, mono `#E5342A`.
- **Aliens** — ring `#8FCB3A`, lead `#A8E03C`, ink `#C8F06A`, soft `#2E7D7D`, bg `#030A05`, layer `#2E7D7D`, mono `#A8E03C`.
- **Mass Effect** — ring `#3AA0FF`, lead `#7CC6FF`, ink `#DDEEFF`, soft `#6A9AD0`, bg `#060B18`, layer `#3A6A9E`, blips = altitude ramp.
- **Top Gun** — ring `#7F99A8`, lead `#AEC4D0`, ink `#DCE8ED`, soft `#9FB8C4`, bg `#1B2830`, layer `#5E7684`, mono `#B2413A`.
- **Firefox** — ring `#FF8A2E`, lead `#FFB454`, ink `#FFB454`, soft `#FF8A2E`, bg `#000000`, layer `#6A5A4A`, mono `#FF2A1A`.

## Engine changes (all in-family, additive)

### 1. `BlipShape` field on `ThemeDesc`
```cpp
enum class BlipShape : uint8_t { kAuto, kChevron, kSilhouette, kDiamond };
// kAuto = the scope's natural marker: Rings→kite quad, Vector→bracket, Grid(orb)→ball.
```
Add `BlipShape shape;` and `bool outline;` to `ThemeDesc`. In `ac_draw_cb`, resolve `kAuto` to the current scope default, then dispatch:
- **kChevron** — 3-point arrowhead, rotated by `track` (reuses the kite's cos/sin transform).
- **kSilhouette** — ~10-point aircraft polygon (fuselage + swept wings + tail), rotated by `track`.
- **kDiamond** — axis-aligned 4-point diamond (no rotation), classic data-block look.
- default/**kBracket**/kite — existing behavior.

Only Top Gun (silhouette), Firefox (chevron), Borderlands (diamond) set a non-auto shape; every existing theme is `kAuto`, so their rendering is unchanged.

### 2. `outline` (cel-shade)
When `s_desc->outline` is true, the filled blip polygon gets a black border pass (`border_color=black, border_width≈2, border_opa=COVER` on the shape's `lv_draw_rect_dsc`). Only Borderlands uses it. Applies to whatever shape the theme draws (diamond, here).

### 3. Vector coastline color from the descriptor
The vector scope currently hard-codes a blue coastline (`0x4E86C6`), which clashes with Firefox's red/black/orange. Change the vector branch to use `lv_color_hex(s_desc->layer)`, and set **CIC and ClaudeIC `layer` = `0x4E86C6`** (they're `kVector`, so their `layer` field is otherwise unused — this preserves their blue coastline exactly). Firefox's `layer` (`#6A5A4A`) then gives it a dim neutral map instead of stray blue.

### 4. Distance-box theme-color fix
The bottom zoom/range button (`ui.cpp` `s_zoomBtn` border + `s_zoomLbl` text) hard-codes `UI_GREEN` and is never re-tinted on theme change → it stays green.
- Add `lv_color_t radar::chromeColor()` returning the active `s_cRing`.
- Add `void ui_apply_theme_accent(lv_color_t c)` in `ui.cpp` that re-tints the zoom button border + label to `c`.
- Call it from the theme-changed callback in `main.cpp` (fires for both long-press and web changes) **and** once at startup after the initial theme is applied.

## Surfaces touched

| File | Change |
|------|--------|
| `src/theme_table.h` | `BlipShape` enum; `shape`/`outline` fields on `ThemeDesc` |
| `src/theme_table.cpp` | 2 new fields on all 10 existing rows (`kAuto`/`false`); 5 new rows; CIC/ClaudeIC `layer`→`0x4E86C6` |
| `src/radar_view.h` | enum: +5 values, `THEME_COUNT`→15; `chromeColor()` decl |
| `src/radar_view.cpp` | `BlipShape` dispatch + `draw_chevron`/`draw_silhouette`/`draw_diamond` + outline in `ac_draw_cb`; vector coastline from `s_desc->layer`; `chromeColor()` |
| `src/ui.cpp` / `src/ui.h` | `ui_apply_theme_accent()` |
| `src/main.cpp` | call `ui_apply_theme_accent(radar::chromeColor())` on theme change + at boot |

**Web picker needs no change** — after theme-pack 1's cleanup it already loops `i < THEME_COUNT` over `kThemes[i].name`, so all 15 appear automatically.

## Verification

- SDL native sim compile (`pio run -e native`) + firmware compile (`pio run -e esp32-s3-amoled-175`) per task; independent Opus review on the blip-shape rendering task.
- Device flash over USB-C for visual acceptance (Selma).
- Redeploy the web flasher: `gh workflow run webflasher.yml --repo selmapi/capsule-radar --ref main` (push doesn't auto-trigger on this fork). Bump `FW_VERSION` in `config.h` so the flasher shows a fresh version.

## Deferred (from the hardware exploration — separate future rounds)

Shake-to-refresh / tilt-to-zoom (IMU), distinct per-event audio, auto-rotate from gravity, emergency red-alert mode, smooth brightness fade; more blip modifiers (altitude-scaled size, fade-in on new contact, heading-vector line, ATC leader line). Logged in the session's exploration report; not in this build.
