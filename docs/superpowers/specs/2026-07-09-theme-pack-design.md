# Capsule Radar — Theme Pack (Vice / Midnight / Silent Running / Mission Control / CIC)

**Date:** 2026-07-09
**Repo:** `selmapi/capsule-radar` (fork of `socquique/capsule-radar`, MIT), branch `feature/theme-pack`
**Hardware:** Waveshare ESP32-S3-Touch-AMOLED-1.75 (round 466×466 CO5300 AMOLED, capacitive touch, GPS, speaker, IMU, RTC, battery), connected to the Mac over USB-C (`/dev/cu.usbmodem*`).

## Goal

Port the five distinctive themes from our plane-radar fork (`selmapi/ESP32-Plane-Radar`) into capsule-radar, plus one new original (**ClaudeIC**), growing its theme count from 4 to 10. Bring over the *look and feel* faithfully; do **not** duplicate the app — capsule-radar's features (touch, flow tracks, coastline, airports, web settings) stay as they are and the new themes simply skin them.

Capsule-radar was the original inspiration for the plane-radar themes, so this is partly bringing them home. Two of its four existing themes (Phosphor green, Amber CRT) already cover the palettes we'd otherwise duplicate, so those are intentionally **not** re-ported.

## The themes we're adding

| # | Theme | Character | Scope style | Blips | Decoration |
|---|-------|-----------|-------------|-------|------------|
| 4 | **Vice** | Miami neon — pink rings, violet crosshairs, cyan/pink accents on deep violet | Rings (stock) | Altitude ramp (pink→amber→cyan) | Sweep |
| 5 | **Midnight** | Navy field, stock-radar readouts (green grid, amber/cyan tags, magenta track) | Rings (stock) | Altitude ramp (red→amber→cyan) | Sweep |
| 6 | **Silent Running** | Submarine night-vision red, near-black background, monochrome | Rings (stock) | **Mono** red | Sweep (slow) |
| 7 | **Mission Control** | NASA navy + gold, cream readouts, **starfield** behind the scope | Rings (stock) | Altitude ramp (red→white→gold) | **Starfield** |
| 8 | **CIC** | Combat-information-center vector scope — bearing-degree ring, tick ring, square grid, `[ ]` brackets on targets, natural-color map, no sweep | **Vector** | **Mono** amber targets | None |
| 9 | **ClaudeIC** | CIC's vector scope in Claude's colors — warm-black field, clay chrome, cream bearing labels, coral brackets | **Vector** | **Mono** coral/cream targets | None |

Existing themes keep their indices (0 Phosphor, 1 Orb, 2 Amber CRT, 3 Military) so saved-theme persistence stays valid. New themes append at 4–9; `THEME_COUNT` 4 → 10. Order among the new six is cosmetic and easy to change.

**ClaudeIC** is a playful easter-egg original (not from the plane radar): the same `ScopeStyle::kVector` geometry as CIC, so it costs **no new drawing code** — it's purely a tenth descriptor row in Claude's palette. Chosen palette (variant A, "dark scope"): bg `#14100E`, clay chrome `#CC785C`, coral accent/brackets `#E8825A`, cream labels `#F0EEE6`; mono coral/cream targets; no sweep.

### Exact palettes (logical RGB → LVGL `lv_color_hex`)

These are lifted verbatim from `selmapi/ESP32-Plane-Radar/src/ui/theme_table_data.cpp`. All values are **logical RGB** and map directly to `lv_color_hex(0xRRGGBB)` — capsule-radar's CO5300/LVGL path uses normal color order, so there is **no R/B swap** here (unlike the plane radar's GC9A01).

> **Implementation landmine — Midnight:** in the plane-radar table Midnight (index 0) stores its colors **pre-swapped** for the GC9A01 panel. Use its *logical* values (the parenthetical "stock" values in that file), listed below — do NOT copy the raw stored bytes, or every Midnight color renders R/B-transposed on capsule-radar. Every other theme's stored values are already logical and copy as-is.

**Vice** — bg `#12041F`, rings `#FF2A9D`, ink/label `#F0E0FF`, center `#FFFFFF`, tag_type `#2AF5FF`, tag_alt `#FFD24A`, track/crosshair `#7A2AFF`; altitude ramp `#FF3A5A` → `#FFD24A` → `#2AF5FF`.

**Midnight** (logical) — bg `#040A1C`, grid `#106420`, label/center `#FFFFFF`, tag_type `#FFC800`, tag_alt `#5AC8FF`, track `#FF00FF`, runway/secondary `#3896AA`; altitude ramp `#FF4A2A` → `#FFD24A` → `#39D0FF`.

**Silent Running** — bg `#0A0002`, rings `#7A1408`, label/center/tags `#FF5A3A`, track (dim) `#5A0F06`, sweep wedge `#FF3A1E`; **mono** (all targets `#FF5A3A`), brightness-style ramp.

**Mission Control** — bg `#081228`, gold rings `#D4A544`, cream ink `#E8E2CE`, tag_type `#D4A544`, tag_alt `#B8C8E8`, track `#D4A544`, secondary `#6A84B0`, star tint `#E8E2CE`; altitude ramp `#C8332A` → `#E8E2CE` → `#D4A544`.

**CIC** — bg `#000000`, green chrome rings `#2AAB5A`, bearing labels `#5AFF8A`, **amber targets** `#FFB428` / `#FFD24A`, dim amber vector `#B47A14`; **mono** amber targets, **no sweep**, `ScopeStyle::Vector`.

**ClaudeIC** (new) — bg `#14100E`, clay chrome rings `#CC785C`, coral accent/brackets `#E8825A`, cream bearing labels `#F0EEE6`, dim cream `#C9C4B8`; **mono** coral/cream targets, **no sweep**, `ScopeStyle::Vector` (shares CIC's draw path). Claude's clay/coral/cream, dark-scope variant.

## Architecture (Option 2 — theme descriptor)

Capsule-radar today defines themes as an `enum RadarTheme` plus a `switch` in `setTheme()` that overrides four chrome colors (`s_cRing/cLead/cInk/cSoft`); its one geometry-changing theme (Orb) is gated by a scattered `orb()` boolean. That pattern doesn't scale to two more geometry-changing themes, so we replace the palette `switch` with a descriptor table — the same shape the plane-radar already uses successfully.

### New file: `src/theme_table.h` (ours, upstream-clean)

```cpp
enum class ScopeStyle : uint8_t { kRings, kGrid, kVector };   // Rings=stock, Grid=Orb, Vector=CIC
enum class BlipMode   : uint8_t { kAltRamp, kMono };          // altitude color-code vs single hue
enum class Decoration : uint8_t { kNone, kSweep, kStarfield };

struct ThemeDesc {
    const char* name;
    uint32_t ring, lead, ink, soft;   // chrome palette (0xRRGGBB)
    uint32_t bg;                      // background (kGrid may add a gradient bottom)
    ScopeStyle scope;
    BlipMode   blips;
    uint32_t   mono;                  // blip color when blips == kMono
    Decoration decor;
    bool       sweep;
};
extern const ThemeDesc kThemes[];     // indices 0..9; existing 4 first, ours appended
extern const int kThemeCount;
```

The ten rows (4 existing, re-expressed as descriptors, + 6 new) live in `src/theme_table.cpp`. Adding a future retint theme is one row; CIC and ClaudeIC share the one `ScopeStyle::kVector` draw path.

### Edits to upstream `src/radar_view.cpp` (minimal, marked seams)

- `setTheme()` reads `kThemes[s_theme]` instead of the hard-coded `switch`; assigns `s_cRing/cLead/cInk/cSoft`, background (solid, or gradient when `scope == kGrid`), and stores the active `ScopeStyle`/`BlipMode`/`Decoration`.
- Replace the scattered `orb()` checks with a single `scopeStyle()` accessor; draw code branches on `ScopeStyle` (`kRings` = today's rings+rose+sweep, `kGrid` = today's Orb path unchanged, `kVector` = new CIC path).
- `alt_color()` gains a mono short-circuit: when `blips == kMono`, return the theme's `mono` color (Silent Running red, CIC amber) instead of the altitude ramp.
- Existing layers (coastline, airports, flow tracks) are tinted from the active descriptor rather than the current fixed `#4E86C6` / `#8A93A6`, so Vice/Silent Running/CIC don't show a stray blue coastline. Natural-color exception for CIC (see below).

Keeping the theme *data* in our own files and the `radar_view.cpp` edits at a few clear seams keeps upstream merges clean (see Upstream sync).

### The two structural themes

**Mission Control (Decoration::kStarfield):** navy/gold palette on `ScopeStyle::kRings` + a new starfield decoration — a field of faint, slowly-twinkling stars drawn behind the scope chrome (analogous to how Orb owns the flow canvas). This is the faithful "full treatment"; it is *not* a console frame (that was an early mockup sketch, discarded).

**CIC (ScopeStyle::kVector):** the vector-scope look — bearing-degree ring (000/090/180/270 + minor ticks), square grid, `[ ]` brackets around each target, amber targets, no rotating sweep. For the natural-color "map" layer, **reuse capsule-radar's existing baked `coastline_data.h` + airports** drawn in natural colors (water blue, coast/roads gray, airports muted), rather than building a new OSM map pipeline — capsule already ships that geometry, so CIC is lighter here than the plane-radar version was.

### ClaudeIC mascot badge (easter-egg garnish)

ClaudeIC (and **only** ClaudeIC) shows the little 8-bit Claude mascot — a clay pixel-creature with two eyes and two legs — as a small fixed overlay tucked inside the scope.

- **Placement matters because the panel is round.** The literal display corner is clipped by the bezel; the badge sits **inside the circular safe area at ~4–5 o'clock** (default), not the pixel corner. Alternatives (bottom-center under 180°, or top-right by 000–090) are one coordinate change if Selma prefers.
- **Implementation:** a tiny hand-encoded bitmap — an `lv_canvas` filled with a small pixel grid, or an `lv_img` from a C-array image descriptor (a new `src/mascot_img.h` / `theme_table.cpp` constant). Body in the theme's clay `#CC785C`, eyes punched to the bg `#14100E`. Created once; shown when `s_theme == THEME_CLAUDEIC`, hidden otherwise (same `show()` pattern as the compass rose). No animation, no per-frame cost.
- Scoped tightly: it's a garnish on one theme, not a general per-theme badge system (YAGNI).

## Surfaces touched

| File | Change |
|------|--------|
| `src/radar_view.h` | `RadarTheme` enum: add 6 values, `THEME_COUNT` → 10 |
| `src/theme_table.{h,cpp}` | **new** — `ThemeDesc` table (the 10 rows) |
| `src/radar_view.cpp` | descriptor-driven `setTheme()`, `ScopeStyle` branch, mono blips, layer tinting, starfield + CIC-vector draw paths (ClaudeIC reuses the vector path), ClaudeIC-only mascot overlay |
| `src/mascot_img.h` | **new** — hand-encoded 8-bit Claude mascot bitmap (ClaudeIC badge) |
| `src/main.cpp` | `tnames[]` (line ~299) + inline web settings picker: add the 6 names so they appear at `capsuleradar.local` and persist |
| `README.md` / `docs/LISTING.md` | list the new themes (optional, low priority) |

Long-press-to-cycle and web-picker persistence already key off the theme index, so both pick up the new themes for free once the enum, table, and `tnames[]` agree.

## Verification

- **Primary loop:** the SDL native simulator — `pio run -e native -t exec` runs the exact LVGL UI on the Mac. Build and screenshot all 10 themes; visually confirm each new theme and confirm the original 4 are pixel-unchanged after the descriptor refactor (regression guard).
- **Device acceptance:** flash to the connected board — `pio run -e esp32-s3-amoled-175 -t upload` (device on `/dev/cu.usbmodem*`), long-press to cycle through all 10, confirm the web picker at `capsuleradar.local`. Warn Selma before flashing if she's mid-use.
- No coordinate/privacy surface: this is pure theming. Nothing location-related is edited or committed (capsule already ships baked `coastline_data.h`).

## Upstream sync (this is Selma's daily-driver fork)

- `upstream` remote → `socquique/capsule-radar`; push to upstream is disabled locally (one-way: we take his improvements, never push to him). Pull with `git fetch upstream && git merge upstream/main`.
- Merge-friendliness is a design constraint: theme *data* lives in our own `theme_table.{h,cpp}`; edits to his `radar_view.cpp` are concentrated at a few marked seams (`setTheme`, the `scopeStyle()` branch, `alt_color` mono short-circuit) rather than sprawled through the file, minimizing conflict surface when his updates land.

## Out of scope / deferred (recorded, not built)

The richer hardware (GPS, speaker, touch, more RAM/flash) opens ideas beyond theming — logged so we don't lose them, but explicitly **not** in this spec:

- GPS-driven live location instead of a configured center (ties into the plane-radar map-service work).
- Audio alerts via the ES8311 speaker (emergency squawk, new-contact chime).
- Tap-to-inspect a target on the touch panel (dossier card / route lookup).

We land the themes first and let these shake out afterward.

## Open items

- Exact starfield density/twinkle rate and CIC bracket sizing are visual-tuning knobs — settle them in the sim against screenshots, not in this spec.
- Whether Vice/Midnight/Mission Control should carry their plane-radar per-theme **altitude ramp stops** exactly, or approximate with capsule's existing ramp for v1. Recommendation: use their exact 3-stop ramps (they're in the table above) since we already have the values.
