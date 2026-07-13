# Sci-fi Theme Pack — Saber / LCARS / Browncoat

**Date:** 2026-07-13
**Status:** Approved (design)
**Version target:** v1.8.0

## Goal

Add three new pop-culture radar themes, taking the theme count **15 → 18**. Each is an *original* radar color scheme evoking a franchise's vibe (same approach as Vice / Aliens / Top Gun / Mass Effect) — no copied logos, art, or assets. All three ride the existing descriptor-table theme engine and auto-appear in the web theme picker and the on-device HUD name list.

Two of the three carry a small per-theme **structural touch** (a decoration) that makes them read as more than a retint:

| Theme | Franchise vibe | Scope | Structural touch |
|-------|----------------|-------|------------------|
| **Saber** | Star Wars (the duel) | `kRings` | none — pure retint |
| **LCARS** | Star Trek (Okudagram console) | `kRings` | **pill-chrome** corner accent |
| **Browncoat** | Firefly (targeting computer, used-future) | `kRings` | **targeting reticle** (center box + corner lock-marks) |

## Palettes

Exact hexes will be tuned in the SDL sim; these are the approved starting points from the companion board.

### Saber (Star Wars)
- `bg` `0x05060A` (near-black)
- `ring` `0x9FB0C4` (steel chrome — rings + crosshair)
- `lead` `0x2A6AFF` (blue accent / sweep leading edge)
- `ink` `0xFFFFFF` (white center core + labels)
- `soft` `0x7A8290` (secondary labels)
- `layer` `0x2E3A48` (coastline/airport/flow — muted steel)
- `blips` `kMono`, `mono` `0x4A7AFF` (cool blue); emergencies use the built-in red halo (reads as the red blade)
- `scope` `kRings`, `sweep` true, `decor` `kSweep`, `shape` `kAuto`, `outline` false, `blipFx` `0`

### LCARS (Star Trek)
- `bg` `0x000000` (LCARS black)
- `ring` `0xFF9966` (apricot — thick signature outer ring)
- `lead` `0xCC99CC` (mauve — secondary ring / accents)
- `ink` `0xFFCC99` (tan/apricot labels + center)
- `soft` `0x9999FF` (periwinkle secondary)
- `layer` `0x5A4A66` (muted mauve coastline)
- `blips` `kAltRamp` (colorful reads as LCARS); `mono` unused
- `scope` `kRings`, `sweep` true, `decor` `kSweep`, `shape` `kAuto`, `outline` false, `blipFx` `0`
- **Structural:** pill-chrome accent — see below.

### Browncoat (Firefly)
- `bg` `0x120C06` (warm black)
- `ring` `0xC87A3A` (rust — rings + crosshair)
- `lead` `0xD4A050` (gold — accents / sweep leading edge)
- `ink` `0xE8D0A8` (cream labels + center)
- `soft` `0x9A7A50` (muted tan secondary)
- `layer` `0x5A4326` (warm-brown coastline)
- `blips` `kAltRamp` (warm ramp); `mono` unused
- `scope` `kRings`, `sweep` true, `decor` `kSweep`, `shape` `kAuto`, `outline` false, `blipFx` `0`
- **Structural:** targeting reticle — see below.

## Structural touches

Both are **theme-gated draws**, not new engine features. They follow the existing precedent: the Mission Control starfield (a `Decoration` drawn in `grid_draw_cb`) and the ClaudeIC mascot badge (a styled `lv_obj` created in `setTheme`).

### LCARS pill-chrome accent
A small cluster of LCARS-style rounded blocks in one corner (e.g. lower-left), created as styled `lv_obj` children in `setTheme` when the active theme is `THEME_LCARS`, torn down on theme change — exactly like the ClaudeIC mascot badge lifecycle. 2–4 rounded rectangles in the apricot/mauve/periwinkle palette. Non-interactive, does not overlap the scope center or blips. Purely decorative chrome.

### Browncoat targeting reticle
A center **targeting box** (a small square outline at screen center) plus four **corner lock-marks** (short L-brackets just inside the outer ring), drawn in `grid_draw_cb` gated on the active theme being `THEME_BROWNCOAT`. Uses the rust/gold palette. This is the "targeting computer" layout the user chose, recolored into Browncoat. Drawn with the same LVGL line/rect draw primitives already used for the grid and CIC brackets. The existing `kRings` crosshair + range rings remain; the reticle sits on top.

## Implementation surface

- **`src/theme_table.{h,cpp}`** — add 3 rows to `kThemes[]`; bump the count. `static_assert(kThemeCount == THEME_COUNT)` must still hold. Add `THEME_SABER`, `THEME_LCARS`, `THEME_BROWNCOAT` to the theme id enum/`THEME_COUNT` (wherever the current 15 are enumerated).
- **`src/radar_view.cpp`** —
  - `grid_draw_cb`: add the Browncoat targeting-reticle draw, gated on theme id.
  - `setTheme`: add the LCARS pill-chrome create/teardown, mirroring the mascot-badge pattern (track the created objects so they're deleted on the next theme switch).
  - No changes to `ac_draw_cb` (Saber is a plain `kMono` retint; LCARS/Browncoat are `kAltRamp`).
- **`src/config.h`** — bump `FW_VERSION` to `1.8.0`.
- Web picker + HUD name list are **table-driven** — no code changes; the 3 names appear automatically.

## Non-goals (YAGNI)

- No new blip shapes (`shape = kAuto` for all three).
- No new `blipFx` (all `0`).
- No new web-config settings (confirmed: themes add none — this is why we did themes before the config-page redesign).
- No full LCARS console chrome or animated pill-strip — just a static corner accent.
- No friend/foe classification for Saber blue/red — "red blade" = the existing emergency halo.
- The fan-made Reaper MP3 remains unused; all sounds stay original synthesis (unchanged this round).

## Testing / acceptance

1. **Sim compile** — `pio run -e native` builds clean; `static_assert` passes.
2. **Sim visual** — launch the SDL sim, cycle to each of the 3 themes, confirm palette, the LCARS pills, and the Browncoat reticle render correctly and tear down cleanly on theme change (no leaked `lv_obj`, no reticle bleeding into other themes).
3. **Firmware compile** — `pio run -e esp32-s3-amoled-175` builds clean.
4. **Device flash** — flash over USB-C; on-device confirm the 3 themes on the round AMOLED, check the reticle/pills at real DPI, verify theme-switch teardown, and glance at emergency-halo red on Saber.
5. **Web flasher** — bump version, `gh workflow run webflasher.yml`, confirm the picker lists 18 themes.

## Review

Sonnet implements; Opus reviews the two structural draws specifically (teardown/leak safety on the LCARS pills, and reticle theme-gating + no overdraw of blips) — the pattern that caught the blip-fx NaN and no-sweep-invalidation bugs.
