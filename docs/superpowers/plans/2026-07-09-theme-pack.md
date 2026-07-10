# Capsule Radar Theme Pack — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 6 themes (Vice, Midnight, Silent Running, Mission Control, CIC, ClaudeIC) to capsule-radar, taking it from 4 → 10 selectable themes, via a descriptor-table refactor that keeps upstream merges clean.

**Architecture:** Replace the hard-coded palette `switch` in `setTheme()` with a `ThemeDesc` table in a new, ours-only `src/theme_table.{h,cpp}`. Draw code branches on a per-theme `ScopeStyle` (Rings / Grid / Vector) instead of the scattered `orb()` boolean. Three themes are pure retints; Mission Control adds a starfield decoration; CIC adds a vector-scope draw path (reused by ClaudeIC); ClaudeIC adds a mascot overlay.

**Tech Stack:** C++ / Arduino / LVGL v8, PlatformIO. Verification is the SDL native simulator (`pio run -e native -t exec`) — there is **no unit-test harness** (adding one would fight upstream merges), so each task's "test" is a described, screenshot-confirmed sim result. Device acceptance is a USB-C flash.

**Reference (exact source palettes):** `~/Documents/Claude/Projects/ESP32-Plane-Radar/src/ui/theme_table_data.cpp`. All values below are already converted to logical RGB for LVGL (no R/B swap — capsule's CO5300 path is normal color order).

---

## Ground rules for every task

- **Work in** `~/Documents/Claude/Projects/capsule-radar`, branch `feature/theme-pack` (already checked out).
- **Build the sim:** `pio run -e native` (compile) / `pio run -e native -t exec` (compile + run in an SDL window). The sim feeds synthetic aircraft via `sim_main.cpp`.
- **Cycle themes in the sim:** the sim maps a key to `radar::cycleTheme()` (confirm the key in `sim_main.cpp`; add one if absent — see Task 2). Screenshot each theme.
- **Regression rule:** after the Task 2 refactor, themes 0–3 (Phosphor/Orb/Amber CRT/Military) must look **pixel-identical** to before. Screenshot them before Task 2 and diff after.
- **Commit after every task** with the message shown. Do **not** push (Selma pushes).
- **Do not** touch coordinates, `coastline_data.h`, or anything location-related.

---

## File structure

| File | Responsibility |
|------|----------------|
| `src/theme_table.h` (new) | `ScopeStyle`/`BlipMode`/`Decoration` enums + `ThemeDesc` struct + `kThemes[]`/`kThemeCount` externs |
| `src/theme_table.cpp` (new) | the 10 `ThemeDesc` rows (data only) |
| `src/mascot_img.h` (new) | hand-encoded 8-bit Claude mascot bitmap for the ClaudeIC badge |
| `src/radar_view.h` (mod) | `RadarTheme` enum: add 6 values, `THEME_COUNT` → 10 |
| `src/radar_view.cpp` (mod) | descriptor-driven `setTheme()`; `scopeStyle()` replacing `orb()`; mono blips; layer tinting; starfield + vector draw paths; mascot overlay |
| `src/main.cpp` (mod) | `tnames[]` + inline web settings picker: 6 new names |
| `platformio.ini` (mod) | add `theme_table.cpp` to the `[env:native]` `build_src_filter` |

---

## Task 1: Theme descriptor table (data + wiring, no behavior change yet)

**Files:**
- Create: `src/theme_table.h`
- Create: `src/theme_table.cpp`
- Modify: `platformio.ini` (`[env:native]` `build_src_filter`)

- [ ] **Step 1: Create `src/theme_table.h`**

```cpp
#pragma once
#include <cstdint>

namespace radar {

enum class ScopeStyle : uint8_t { kRings, kGrid, kVector };   // kRings=stock, kGrid=Orb, kVector=CIC/ClaudeIC
enum class BlipMode   : uint8_t { kAltRamp, kMono };          // altitude color-code vs single hue
enum class Decoration : uint8_t { kNone, kSweep, kStarfield };

// One theme = chrome palette + background + scope geometry + blip policy + decoration.
// Colors are 0xRRGGBB logical RGB (fed to lv_color_hex). `layer` tints the
// coastline/airports/flow so they don't clash with the palette; kVector themes
// override this with natural map colors in the vector draw path.
struct ThemeDesc {
    const char* name;
    uint32_t ring;    // rings / crosshair / sweep base   (-> s_cRing)
    uint32_t lead;    // sweep leading edge / accents      (-> s_cLead)
    uint32_t ink;     // labels / center / selection       (-> s_cInk)
    uint32_t soft;    // secondary labels                  (-> s_cSoft)
    uint32_t bg;      // background fill
    uint32_t layer;   // coastline/airport/flow tint
    ScopeStyle scope;
    BlipMode   blips;
    uint32_t   mono;  // blip color when blips == kMono
    Decoration decor;
    bool       sweep;
};

extern const ThemeDesc kThemes[];
extern const int kThemeCount;

}  // namespace radar
```

- [ ] **Step 2: Create `src/theme_table.cpp` with all 10 rows**

Fill `ring/lead/ink/soft` for the existing 4 from the current `setTheme()` switch and the `COL_*` macros in `radar_view.cpp` (Phosphor = `COL_GREEN/COL_LEAD/COL_INK/COL_SOFT`; Amber/Military from the switch). `bg`: `0x000000` for all except Orb (`0x18540F`, its gradient top — the gradient bottom stays special-cased in `setTheme`). `layer`: existing fixed `0x4E86C6` for the stock four. `scope`: `kGrid` for Orb, else `kRings`. `blips`: `kAltRamp` for all four. `decor`: `kSweep` for Phosphor (Amber/Military currently sweep too — keep `kSweep`, `sweep=true`; Orb `kNone`, `sweep=false`).

```cpp
#include "theme_table.h"
namespace radar {
const ThemeDesc kThemes[] = {
  // 0 Phosphor (stock green + sweep)
  {"Phosphor", 0x1DFF86,0x3DFF9A,0xEAFFF3,0x9AFFC8, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 1 Orb (grid scope) — gradient bottom handled in setTheme()
  {"Orb", 0x3F8B30,0x3F8B30,0xEAFFF3,0x9AFFC8, 0x18540F,0x4E86C6, ScopeStyle::kGrid, BlipMode::kAltRamp,0, Decoration::kNone,false},
  // 2 Amber CRT
  {"Amber CRT", 0xFFB23C,0xFFD27A,0xFFE9C2,0xFFC98A, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 3 Military
  {"Military", 0x49C46B,0x76E08C,0xE0FFE6,0x9FD7A8, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 4 Vice (neon pink/violet; ramp handled by kAltRamp for now)
  {"Vice", 0xFF2A9D,0x7A2AFF,0xF0E0FF,0xF0E0FF, 0x12041F,0x7A2AFF, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 5 Midnight (navy field, stock-radar readouts) — LOGICAL values, NOT the plane-radar's pre-swapped bytes
  {"Midnight", 0x106420,0x3896AA,0xFFFFFF,0x5AC8FF, 0x040A1C,0x3896AA, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 6 Silent Running (mono red + slow sweep)
  {"Silent Running", 0x7A1408,0xFF3A1E,0xFF5A3A,0xFF5A3A, 0x0A0002,0x5A0F06, ScopeStyle::kRings, BlipMode::kMono,0xFF5A3A, Decoration::kSweep,true},
  // 7 Mission Control (navy/gold + starfield)
  {"Mission Control", 0xD4A544,0xD4A544,0xE8E2CE,0xB8C8E8, 0x081228,0x6A84B0, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kStarfield,false},
  // 8 CIC (green vector scope, amber mono targets, no sweep)
  {"CIC", 0x2AAB5A,0x5AFF8A,0x5AFF8A,0x2AAB5A, 0x000000,0x2AAB5A, ScopeStyle::kVector, BlipMode::kMono,0xFFB428, Decoration::kNone,false},
  // 9 ClaudeIC (CIC geometry in Claude's palette; mascot added in Task 8)
  {"ClaudeIC", 0xCC785C,0xE8825A,0xF0EEE6,0xC9C4B8, 0x14100E,0xCC785C, ScopeStyle::kVector, BlipMode::kMono,0xE8825A, Decoration::kNone,false},
};
const int kThemeCount = (int)(sizeof(kThemes)/sizeof(kThemes[0]));
}  // namespace radar
```

- [ ] **Step 3: Add the new .cpp to the native build filter**

In `platformio.ini`, `[env:native]` `build_src_filter` (line ~65), append `+<theme_table.cpp>`. The firmware env uses `+<*>` so it's already included there.

- [ ] **Step 4: Verify it compiles**

Run: `pio run -e native`
Expected: SUCCESS (nothing consumes the table yet — this task only proves it builds and links).

- [ ] **Step 5: Commit**

```bash
git add src/theme_table.h src/theme_table.cpp platformio.ini
git commit -m "feat(themes): add ThemeDesc table scaffold (10 rows, no behavior change yet)"
```

---

## Task 2: Drive `setTheme()` from the table (existing 4 themes unchanged)

**Goal:** refactor with zero visible change to themes 0–3. This is the regression-critical task.

**Files:**
- Modify: `src/radar_view.cpp` (`setTheme()` ~544–585; add `#include "theme_table.h"`; keep `THEME_COUNT` from `radar_view.h`)
- Modify: `src/sim_main.cpp` (ensure a key cycles themes — for screenshots)

- [ ] **Step 1: Screenshot the baseline** — run `pio run -e native -t exec`, cycle through themes 0–3, screenshot each. Keep these as the regression reference.

- [ ] **Step 2: Add `#include "theme_table.h"` and a `scopeStyle()` accessor** near the top of `radar_view.cpp`, and a static `const ThemeDesc* s_desc = &kThemes[0];`:

```cpp
#include "theme_table.h"
static inline ScopeStyle scopeStyle() { return s_desc->scope; }
static inline bool orb() { return scopeStyle() == ScopeStyle::kGrid; }  // keep orb() as a thin alias
```
Delete the old `static inline bool orb() { return s_theme == THEME_ORB; }` at line ~128.

- [ ] **Step 3: Rewrite `setTheme()` to read the descriptor.** Replace the palette `switch` (lines ~548–557) and background block with:

```cpp
void setTheme(int t) {
    // Wrap on the enum THEME_COUNT (still 4 here), NOT kThemeCount (10). This keeps
    // Task 2 a pure no-op refactor: only themes 0-3 are reachable until Task 3 bumps
    // THEME_COUNT to 10. Invariant: THEME_COUNT <= kThemeCount (indexing kThemes[] stays safe).
    s_theme = ((t % THEME_COUNT) + THEME_COUNT) % THEME_COUNT;
    s_desc  = &kThemes[s_theme];
    const bool drg = (s_desc->scope == ScopeStyle::kGrid);

    s_cRing = lv_color_hex(s_desc->ring);
    s_cLead = lv_color_hex(s_desc->lead);
    s_cInk  = lv_color_hex(s_desc->ink);
    s_cSoft = lv_color_hex(s_desc->soft);

    if (s_parent) {
        if (drg) {
            lv_obj_set_style_bg_color(s_parent, ORB_BG_TOP, 0);
            lv_obj_set_style_bg_grad_color(s_parent, ORB_BG_BOT, 0);
            lv_obj_set_style_bg_grad_dir(s_parent, LV_GRAD_DIR_VER, 0);
        } else {
            lv_obj_set_style_bg_color(s_parent, lv_color_hex(s_desc->bg), 0);
            lv_obj_set_style_bg_grad_dir(s_parent, LV_GRAD_DIR_NONE, 0);
        }
        lv_obj_set_style_bg_opa(s_parent, LV_OPA_COVER, 0);
    }
    // Keep the existing retint block, but change the rose/centerDot/pulse/rangeLbl
    // show() gating from `!drg` to `scope == kRings` so the VECTOR themes (CIC/ClaudeIC)
    // also hide the N/S/E/W rose + center dot (they draw their own bearing labels).
    // For themes 0-3 this is identical to `!drg` (Orb=kGrid hidden; others=kRings shown).
    const bool ringsChrome = (s_desc->scope == ScopeStyle::kRings);
    for (int i = 0; i < 4; ++i) show(s_rose[i], ringsChrome);
    show(s_rangeLbl, ringsChrome && s_rangeLblVisible);
    show(s_centerDot, ringsChrome);
    show(s_pulse, ringsChrome);
    // ... then the existing text_color retints on s_rose/s_centerDot/s_pulse/s_rangeLbl ...
    setSweepEnabled(s_desc->sweep);   // NEW: sweep visibility now theme-driven
    flow_redraw_all();
    if (s_parent) lv_obj_invalidate(s_parent);
    if (s_themeCb) s_themeCb(s_theme);
}
```
Note: `THEME_COUNT` in `radar_view.h` still exists but `setTheme` now uses `kThemeCount`; keep them equal (Task 3 bumps both).

- [ ] **Step 4: Verify existing themes unchanged** — `pio run -e native -t exec`, cycle 0–3, screenshot, compare to Step 1. Phosphor/Orb/Amber/Military must be pixel-identical.

- [ ] **Step 5: Commit**

```bash
git add src/radar_view.cpp src/sim_main.cpp
git commit -m "refactor(themes): drive setTheme() from ThemeDesc table (no visual change)"
```

---

## Task 3: Enum values + the 3 retint themes (Vice, Midnight, Silent Running)

**Files:**
- Modify: `src/radar_view.h` (enum + `THEME_COUNT`)
- Modify: `src/radar_view.cpp` (`alt_color()` mono short-circuit; layer tinting)

- [ ] **Step 1: Extend the enum** in `radar_view.h` (~line 15):

```cpp
enum RadarTheme {
    THEME_PHOSPHOR = 0, THEME_ORB = 1, THEME_AMBER = 2, THEME_MILITARY = 3,
    THEME_VICE = 4, THEME_MIDNIGHT = 5, THEME_SILENT = 6,
    THEME_MISSION = 7, THEME_CIC = 8, THEME_CLAUDEIC = 9,
    THEME_COUNT = 10
};
```

- [ ] **Step 2: Mono-blip short-circuit in `alt_color()`** (~line 136). Add at the top of the function:

```cpp
static lv_color_t alt_color(float altFt, bool onGround) {
    if (s_desc->blips == BlipMode::kMono) return lv_color_hex(s_desc->mono);
    // ... existing altitude ramp unchanged ...
}
```

- [ ] **Step 3: Tint the coastline from the descriptor.** In `grid_draw_cb`, non-orb branch, replace the fixed `COAST_COLOR` (line ~220) with `lv_color_hex(s_desc->layer)`. **Leave `AIRPORT_COLOR` as-is** (neutral gray reads fine on every theme, and changing it would break the Task-2 regression baseline for themes 0–3). Leave `flow_draw_seg`'s non-orb path (`s_cRing`, line ~168) unchanged. For the stock four, `s_desc->layer == 0x4E86C6 == COAST_COLOR`, so this is a no-op on them; only the new themes shift the coastline color. (CIC/ClaudeIC override the coastline with natural blue in their vector path, Task 5.)

- [ ] **Step 4: Verify the 3 retints** — `pio run -e native -t exec`, cycle to themes 4/5/6. Expect: **Vice** deep-violet field, pink rings, altitude-colored glyphs; **Midnight** navy field, green grid rings, white labels; **Silent Running** near-black-red field, all glyphs uniform red `#FF5A3A`, slow red sweep. Screenshot each.

- [ ] **Step 5: Commit**

```bash
git add src/radar_view.h src/radar_view.cpp
git commit -m "feat(themes): add Vice, Midnight, Silent Running (retints + mono blips)"
```

---

## Task 4: Mission Control + starfield decoration

**Files:**
- Modify: `src/radar_view.cpp` (new `starfield_draw_cb` layer or hook in `grid_draw_cb`; created in `init()`)

- [ ] **Step 1: Add a deterministic starfield draw.** In `grid_draw_cb`, before the rings block (non-vector branch), when `s_desc->decor == Decoration::kStarfield`, draw ~60 faint star dots at fixed pseudo-random positions (deterministic — a fixed seed array or `(i*2654435761u)`-hashed coords so it doesn't shimmer). Star color = `s_cInk` (cream) at low, slowly-pulsing opacity keyed off `s_frameCtr`. Draw as 1–2 px `lv_draw_rect` dots. Keep it behind the rings/coastline.

```cpp
if (s_desc->decor == Decoration::kStarfield) {
    lv_draw_rect_dsc_t st; lv_draw_rect_dsc_init(&st);
    st.bg_color = s_cInk; st.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < 60; ++i) {
        uint32_t h = (uint32_t)(i * 2654435761u);
        lv_coord_t x = (lv_coord_t)(h % SCREEN_W);
        lv_coord_t y = (lv_coord_t)((h >> 12) % SCREEN_H);
        uint8_t tw = (uint8_t)((h >> 5) & 63);
        st.bg_opa = (lv_opa_t)(40 + ((s_frameCtr + tw) % 64));   // gentle twinkle
        lv_area_t r = { x, y, (lv_coord_t)(x+1), (lv_coord_t)(y+1) };
        lv_draw_rect(d, &st, &r);
    }
}
```
Ensure the starfield layer/grid is invalidated periodically for the twinkle (piggyback on `sweep_timer_cb`'s existing invalidation when `decor==kStarfield`; keep it cheap).

- [ ] **Step 2: Verify** — cycle to theme 7. Expect navy field, gold rings, cream labels, faint twinkling stars behind the scope, altitude-colored glyphs (ramp red→white→gold is approximated by the stock ramp for v1 — see Open Items). Screenshot.

- [ ] **Step 3: Commit**

```bash
git add src/radar_view.cpp
git commit -m "feat(themes): add Mission Control with starfield decoration"
```

---

## Task 5: CIC vector-scope draw path

**Files:**
- Modify: `src/radar_view.cpp` (`grid_draw_cb`, `sweep_draw_cb`, `ac_draw_cb` — add `ScopeStyle::kVector` branches)

- [ ] **Step 1: Vector chrome in `grid_draw_cb`.** Add a `if (scopeStyle() == ScopeStyle::kVector) { ... return; }` branch (sibling to the `orb()` branch). Draw, in order: (a) the natural-color map — `coastline_draw(d, lv_color_hex(0x4E86C6), 150, 2)` (water blue) + `airports_draw(d, lv_color_hex(0x8A93A6), 140)`; (b) a **square grid** (reuse the orb grid loop, color `s_cRing` at low opa); (c) a **bearing ring** at `RADAR_R_OUTER_PX` (`s_cRing`) plus an inner tick ring; (d) **minor ticks** every 30° and **degree labels** 000/090/180/270 in `s_cInk` (montserrat_12) via `rim_point()` for placement. No sweep.

- [ ] **Step 2: Suppress sweep for vector.** In `sweep_draw_cb`, change the early return to `if (orb() || scopeStyle() == ScopeStyle::kVector) return;` (belt-and-suspenders; `sweep=false` in the table already hides the layer).

- [ ] **Step 3: Bracket targets in `ac_draw_cb`.** Add a vector branch in the per-aircraft loop: instead of the rotated glyph, draw a `[ ]` bracket pair around `ac.pos` (four short `lv_draw_line`s forming corner ticks, color `alt_color()` which is mono amber) + a small center dot. Keep the floating call/alt labels (they read well on the vector scope). Emergency halo stays.

- [ ] **Step 4: Verify** — cycle to theme 8 (CIC). Expect pure-black field, green bearing ring + degree labels + square grid, natural-color coastline/airports, amber `[ ]` bracketed targets, **no sweep**. Screenshot.

- [ ] **Step 5: Commit**

```bash
git add src/radar_view.cpp
git commit -m "feat(themes): add CIC vector-scope (bearing ring, grid, bracket targets)"
```

---

## Task 6: ClaudeIC (palette only — geometry reused)

**Files:** none beyond Task 1's row (already present). This task is verification + the enum entry (already added in Task 3).

- [ ] **Step 1: Verify** — cycle to theme 9 (ClaudeIC). Expect the exact CIC vector geometry rendered in warm-black `#14100E` + clay `#CC785C` chrome + cream `#F0EEE6` labels + coral `#E8825A` bracket targets. Screenshot. (No mascot yet.)

- [ ] **Step 2: Commit** (if any tweak was needed; otherwise skip)

```bash
git commit --allow-empty -m "test(themes): confirm ClaudeIC renders on the vector path"
```

---

## Task 7: ClaudeIC mascot badge

**Files:**
- Create: `src/mascot_img.h`
- Modify: `src/radar_view.cpp` (create a mascot `lv_canvas` or `lv_img` in `init()`; show only when `s_theme == THEME_CLAUDEIC`)

- [ ] **Step 1: Encode the mascot.** In `mascot_img.h`, define a small pixel grid (e.g. 12×12) as a `static const uint8_t kMascotMask[12][12]` where 1 = body, 0 = transparent, and mark the two eye cells with a sentinel (2) so they punch the bg color. Body = clay `#CC785C`, eyes = bg `#14100E`. (Shape: rounded-square body rows 1–8, two eyes at ~row 4 cols 4 and 7, two legs rows 9–10 at cols 4 and 8.)

- [ ] **Step 2: Draw it once into a canvas in `init()`**, sized ~24×24 (2× the grid, nearest-neighbor), positioned at `LV_ALIGN_CENTER` with an offset that lands it **inside the round safe area at ~4–5 o'clock** (e.g. `dx=+150, dy=+150` from center on the 466 panel — verify it sits inside `RADAR_R_OUTER_PX`). Store the handle in a static `s_mascot`.

- [ ] **Step 3: Toggle visibility in `setTheme()`** — in the show()/retint block: `show(s_mascot, s_theme == THEME_CLAUDEIC);`.

- [ ] **Step 4: Verify** — cycle to ClaudeIC: the mascot appears at lower-right inside the ring; cycle away: it disappears on all other themes. Screenshot ClaudeIC with the badge, and one other theme without it.

- [ ] **Step 5: Commit**

```bash
git add src/mascot_img.h src/radar_view.cpp
git commit -m "feat(themes): ClaudeIC 8-bit mascot badge (ClaudeIC-only overlay)"
```

---

## Task 8: Web picker + HUD names

**Files:**
- Modify: `src/main.cpp` (`tnames[]` ~line 299; inline settings-page theme options)

- [ ] **Step 1: Extend `tnames[]`** to all 10, matching enum order exactly:

```cpp
const char *tnames[] = {"Phosphor","Orb","Amber CRT","Military",
                        "Vice","Midnight","Silent Running",
                        "Mission Control","CIC","ClaudeIC"};
```

- [ ] **Step 2: Widen the web picker loop.** The option-building loop at `main.cpp:301` is hardcoded `for (int i = 0; i < 4; ++i)`. Change the bound to `THEME_COUNT` (now 10) so all ten `tnames[]` entries are emitted:

```cpp
for (int i = 0; i < THEME_COUNT; ++i) {
    char o[80];
    snprintf(o, sizeof(o), "<option value=%d%s>%s</option>", i, i == th ? " selected" : "", tnames[i]);
    topts += o;
}
```
Confirm `THEME_COUNT` is visible in `main.cpp` (it includes `radar_view.h`). Persistence already keys off the index via `setThemeChangedCb`, so no storage change is needed. (`tnames[]` now has 10 entries from Step 1, so the widened loop is in-bounds.)

- [ ] **Step 3: Verify (firmware build only — web needs the device).** `pio run -e esp32-s3-amoled-175` compiles clean. Full web-picker check happens on-device in Task 9.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(themes): list all 10 themes in HUD + web picker"
```

---

## Task 9: Device flash + acceptance

- [ ] **Step 1: Warn Selma before flashing** (working-agreement rule). Confirm the board is idle.
- [ ] **Step 2: Flash** — `pio run -e esp32-s3-amoled-175 -t upload` (device on `/dev/cu.usbmodem111201`).
- [ ] **Step 3: On-device acceptance** — long-press to cycle all 10 themes; confirm each renders; open `capsuleradar.local` and confirm the picker lists and switches all 10 and the choice persists across a reboot.
- [ ] **Step 4:** Report results to Selma with the sim screenshots; leave the branch unpushed unless she says otherwise.

---

## Open items (carry forward, not blockers)

- **Exact altitude ramps** for Vice / Midnight / Mission Control: v1 uses capsule's stock altitude ramp; the plane-radar per-theme 3-stop ramps are available (`theme_table_data.cpp`) if Selma wants exact parity — would extend `ThemeDesc` with 3 ramp stops + a `RampMode`. Flagged, not built.
- **Silent Running sweep speed** (plane-radar ran it slow): the descriptor has no per-theme sweep period; add one only if Selma asks.
- **Mascot placement** default is ~4–5 o'clock; bottom-center / top-right are one-offset changes.
