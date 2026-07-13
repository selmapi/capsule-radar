# Sci-fi Theme Pack (Saber / LCARS / Browncoat) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three descriptor-table themes — Saber (Star Wars), LCARS (Star Trek), Browncoat (Firefly) — taking the count 15 → 18, with two small per-theme structural draws (LCARS pill-chrome, Browncoat targeting reticle).

**Architecture:** All three are rows in `radar::kThemes[]` driven by the existing engine. Saber is a pure retint. LCARS adds a create-once/show-hide pill-chrome badge (identical lifecycle to the ClaudeIC mascot). Browncoat adds a theme-gated targeting-reticle draw inside `grid_draw_cb`'s rings path (identical pattern to the Mission Control starfield). No new engine capabilities, no new config settings.

**Tech Stack:** C++17, PlatformIO (`native` SDL sim + `esp32-s3-amoled-175` firmware), LVGL v8.

---

### Task ST-1: Theme ids + three table rows

**Files:**
- Modify: `src/radar_view.h:16-20` (RadarTheme enum)
- Modify: `src/theme_table.cpp:34-35` (append rows before the closing `};`)

- [ ] **Step 1: Add the three enum ids and bump the count**

In `src/radar_view.h`, change the enum tail from:

```cpp
    THEME_BORDERLANDS=10, THEME_ALIENS=11, THEME_MASSEFFECT=12, THEME_TOPGUN=13, THEME_FIREFOX=14,
    THEME_COUNT=15
```

to:

```cpp
    THEME_BORDERLANDS=10, THEME_ALIENS=11, THEME_MASSEFFECT=12, THEME_TOPGUN=13, THEME_FIREFOX=14,
    THEME_SABER=15, THEME_LCARS=16, THEME_BROWNCOAT=17,
    THEME_COUNT=18
```

- [ ] **Step 2: Append the three rows**

In `src/theme_table.cpp`, immediately after the Firefox row (line 34, `{"Firefox", ...},`) and before the closing `};`, add:

```cpp
  // 15 Saber (Star Wars — near-black, steel chrome, white core, cool-blue mono blips)
  {"Saber", 0x9FB0C4,0x2A6AFF,0xFFFFFF,0x7A8290, 0x05060A,0x2E3A48, ScopeStyle::kRings, BlipMode::kMono,0x4A7AFF, Decoration::kSweep,true, BlipShape::kAuto, false, 0},
  // 16 LCARS (Star Trek — apricot ring, mauve/periwinkle accents; pill-chrome badge added in setTheme)
  {"LCARS", 0xFF9966,0xCC99CC,0xFFCC99,0x9999FF, 0x000000,0x5A4A66, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true, BlipShape::kAuto, false, 0},
  // 17 Browncoat (Firefly — rust rings, gold accents, cream ink; targeting-reticle drawn in grid_draw_cb)
  {"Browncoat", 0xC87A3A,0xD4A050,0xE8D0A8,0x9A7A50, 0x120C06,0x5A4326, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true, BlipShape::kAuto, false, 0},
```

- [ ] **Step 3: Compile the sim to prove the table + static_assert are consistent**

Run: `pio run -e native`
Expected: build SUCCESS. If the `static_assert` at `theme_table.cpp:37` fires ("row count must equal THEME_COUNT"), the enum count and row count disagree — recount.

- [ ] **Step 4: Commit**

```bash
git add src/radar_view.h src/theme_table.cpp
git commit -m "feat(themes): add Saber/LCARS/Browncoat rows (15->18)"
```

---

### Task ST-2: Browncoat targeting reticle

**Files:**
- Modify: `src/radar_view.cpp` — end of `grid_draw_cb` rings path (insert before the closing `}` at line 326)

Reticle = a center square outline + four corner L-brackets just inside the outer ring, in the rust/gold palette. Drawn only when the active theme is Browncoat. `s_theme` holds the current theme id (see its use at line 865); `s_cRing`/`s_cLead` are the active chrome/accent colors; `rim_point(deg, radius)` returns a point on a circle of that radius around center (used throughout the vector path).

- [ ] **Step 1: Add the reticle draw at the end of the rings path**

In `src/radar_view.cpp`, the standard rings path ends with the crosshair lines and the function's closing `}` at line 326. Immediately **before** that closing `}` (after the `lv_draw_line(d, &ll, &v1, &v2);` at line 325), insert:

```cpp
    // Browncoat targeting reticle: center box + four corner lock-marks (rust/gold)
    if (s_theme == THEME_BROWNCOAT) {
        // center box outline
        lv_draw_rect_dsc_t bx; lv_draw_rect_dsc_init(&bx);
        bx.bg_opa = LV_OPA_TRANSP;
        bx.border_color = s_cLead; bx.border_width = 2; bx.border_opa = 220;
        lv_area_t box = { (lv_coord_t)(s_cx - 16), (lv_coord_t)(s_cy - 16),
                          (lv_coord_t)(s_cx + 16), (lv_coord_t)(s_cy + 16) };
        lv_draw_rect(d, &bx, &box);

        // four L-brackets just inside the outer ring (NE/SE/SW/NW), rust
        lv_draw_line_dsc_t br; lv_draw_line_dsc_init(&br);
        br.color = s_cRing; br.width = 2; br.opa = 200;
        const lv_coord_t rad = RADAR_R_OUTER_PX - 8;
        const lv_coord_t arm = 16;
        for (int q = 0; q < 4; ++q) {
            int sx = (q == 0 || q == 1) ? 1 : -1;   // +x for NE/SE
            int sy = (q == 0 || q == 3) ? -1 : 1;    // -y for NE/NW
            lv_coord_t cxp = (lv_coord_t)(s_cx + sx * (rad * 0.62f));
            lv_coord_t cyp = (lv_coord_t)(s_cy + sy * (rad * 0.62f));
            lv_point_t h1 = { cxp, cyp }, h2 = { (lv_coord_t)(cxp - sx * arm), cyp };
            lv_point_t v1 = { cxp, cyp }, v2 = { cxp, (lv_coord_t)(cyp - sy * arm) };
            lv_draw_line(d, &br, &h1, &h2);
            lv_draw_line(d, &br, &v1, &v2);
        }
    }
```

- [ ] **Step 2: Compile the sim**

Run: `pio run -e native`
Expected: build SUCCESS.

- [ ] **Step 3: Visual check in the SDL sim**

Run the native sim binary, cycle to the **Browncoat** theme, and confirm: rust rings + gold center box + four corner brackets render; the reticle is centered and symmetric; then switch to any other theme and confirm the reticle is **gone** (proves the `s_theme` gate). Confirm blips still draw over the box (box is an outline, no fill).

- [ ] **Step 4: Commit**

```bash
git add src/radar_view.cpp
git commit -m "feat(themes): Browncoat targeting-reticle draw (theme-gated)"
```

---

### Task ST-3: LCARS pill-chrome badge

**Files:**
- Modify: `src/radar_view.cpp:83` (add a static handle next to `s_mascot`)
- Modify: `src/radar_view.cpp:865` (show/hide in setTheme)
- Modify: `src/radar_view.cpp:1056` (create-once, right after the mascot block)

Lifecycle mirrors the ClaudeIC mascot exactly: built once during object creation, shown/hidden per theme. No per-switch teardown, so no leak surface.

- [ ] **Step 1: Add the static handle**

In `src/radar_view.cpp`, next to line 83 (`static lv_obj_t *s_mascot = nullptr;`), add:

```cpp
static lv_obj_t  *s_lcars     = nullptr;
```

- [ ] **Step 2: Show/hide it in setTheme**

In `src/radar_view.cpp`, immediately after line 865 (`if (s_mascot) show(s_mascot, s_theme == THEME_CLAUDEIC);`), add:

```cpp
    if (s_lcars)  show(s_lcars,  s_theme == THEME_LCARS);
```

- [ ] **Step 3: Build the badge (create-once)**

In `src/radar_view.cpp`, immediately after the mascot block's closing `}` at line 1056 (before `s_refreshLbl = lv_label_create(parent);`), insert:

```cpp
    // LCARS-only pill-chrome badge (a few rounded blocks, lower-left; shown/hidden in setTheme)
    {
        const lv_color_t apri = lv_color_hex(radar::kThemes[THEME_LCARS].ring);   // #FF9966
        const lv_color_t mauv = lv_color_hex(radar::kThemes[THEME_LCARS].lead);   // #CC99CC
        const lv_color_t peri = lv_color_hex(radar::kThemes[THEME_LCARS].soft);   // #9999FF
        s_lcars = lv_obj_create(parent);
        lv_obj_remove_style_all(s_lcars);
        lv_obj_set_size(s_lcars, 30, 46);
        lv_obj_clear_flag(s_lcars, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(s_lcars, LV_ALIGN_CENTER, -150, 150);   // ~7-8 o'clock, inside the round safe area
        lv_obj_set_style_bg_opa(s_lcars, LV_OPA_TRANSP, 0);

        struct { lv_coord_t w, h, y; lv_color_t c; } pills[3] = {
            { 30, 14, 0,  apri },
            { 22, 12, 17, mauv },
            { 26, 11, 32, peri },
        };
        for (auto &p : pills) {
            lv_obj_t *pill = lv_obj_create(s_lcars);
            lv_obj_remove_style_all(pill);
            lv_obj_set_size(pill, p.w, p.h);
            lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 0, p.y);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pill, p.c, 0);
            lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        }
    }
```

- [ ] **Step 4: Compile the sim**

Run: `pio run -e native`
Expected: build SUCCESS.

- [ ] **Step 5: Visual check in the SDL sim**

Cycle to **LCARS**: confirm the three rounded pills (apricot/mauve/periwinkle) appear lower-left, inside the round bezel, not overlapping the scope center. Switch to another theme → pills hidden. Switch back → pills return. Confirm the ClaudeIC mascot still only shows on ClaudeIC (no regression to the show/hide block).

- [ ] **Step 6: Commit**

```bash
git add src/radar_view.cpp
git commit -m "feat(themes): LCARS pill-chrome badge (create-once, show/hide)"
```

---

### Task ST-4: Version bump, firmware build, flash, redeploy flasher

**Files:**
- Modify: `src/config.h:4` (`FW_VERSION`)

- [ ] **Step 1: Bump the version**

In `src/config.h` line 4, change `#define FW_VERSION "1.7.2"` to `#define FW_VERSION "1.8.0"`.

- [ ] **Step 2: Firmware compile**

Run: `pio run -e esp32-s3-amoled-175`
Expected: build SUCCESS.

- [ ] **Step 3: Flash the connected device**

Run: `pio run -e esp32-s3-amoled-175 -t upload`
Expected: upload completes over USB-C.

- [ ] **Step 4: On-device acceptance**

On the round AMOLED, cycle to Saber, LCARS, Browncoat. Confirm: palettes read right at real DPI; Saber blips are cool-blue and an emergency shows the red halo; LCARS pills sit cleanly lower-left; Browncoat box + brackets are centered/symmetric; theme-switch cleanly hides each theme's structural bits. (User does this — no audio/vision telemetry on-device.)

- [ ] **Step 5: Commit + redeploy the web flasher**

```bash
git add src/config.h
git commit -m "release: v1.8.0 sci-fi theme pack (Saber/LCARS/Browncoat)"
git push
gh workflow run webflasher.yml
```

Expected: workflow dispatched; once green, the web flasher lists 18 themes and serves the v1.8.0 firmware.

---

## Self-Review

- **Spec coverage:** Saber row (ST-1) ✓; LCARS row + pills (ST-1, ST-3) ✓; Browncoat row + reticle (ST-1, ST-2) ✓; version bump + flash + flasher redeploy (ST-4) ✓; count 15→18 + static_assert (ST-1) ✓; no new config/blipFx/shape (rows all `kAuto`/`0`) ✓.
- **Type consistency:** `s_theme`, `s_cRing`, `s_cLead`, `s_cx/s_cy`, `rim_point`, `show()`, `radar::kThemes[]`, `RADAR_R_OUTER_PX` all match existing usages verified in `radar_view.cpp`. New handle `s_lcars` defined in ST-3 Step 1 before its uses in Steps 2–3. Enum ids `THEME_SABER/LCARS/BROWNCOAT` defined in ST-1 before use in ST-2/ST-3.
- **Placeholder scan:** none — every code step shows full code.
- **Deviation from spec wording:** spec said the LCARS badge is "created/torn-down like the mascot"; the mascot is actually create-once + show/hide (no teardown), which is what the plan does — strictly safer re: leaks, same visual result.
