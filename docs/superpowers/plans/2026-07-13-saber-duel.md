# Saber Duel Sweep + Browncoat Box Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans.

**Goal:** Give the Saber theme a second, counter-rotating **red** sweep blade (so both "lightsabers" are always visible, not just on rare emergencies), and double the Browncoat targeting-box size. Ship as v1.8.1.

**Architecture:** Refactor the single-arm sweep draw into a reusable `draw_sweep_arm(deg, dir, trail, lead)` helper. Non-Saber themes call it once (unchanged behavior). Saber calls it twice — a blue blade rotating one way and a red blade the other, at slightly different rates so the crossings drift. A second angle `s_sweepDeg2` advances/invalidates only when the active theme is Saber. `wedge_bbox` gains a direction arg so the counter-rotating blade's dirty region is computed correctly.

**Tech Stack:** C++17, PlatformIO (`native` + `esp32-s3-amoled-175`), LVGL v8.

**Note:** the Browncoat box change (Task DUEL-0) is ALREADY made in the working tree by the orchestrator — verify it's present; do not redo it. Commit it as part of DUEL-1.

---

### Task DUEL-1: Saber second blade + Browncoat box

**Files:** `src/radar_view.cpp`, `src/config.h`

- [ ] **Step 1: Verify the Browncoat box edit is present**

In `src/radar_view.cpp` inside the `if (s_theme == THEME_BROWNCOAT)` block, the center box must read `s_cx - 32 ... s_cx + 32` (doubled from ±16). If it still says ±16, change it to ±32. (Orchestrator already applied this; just confirm.)

- [ ] **Step 2: Add the second-blade rate constant**

In `src/radar_view.cpp`, right after line 56 (`#define SWEEP_TRAIL_OPA   72`), add:

```cpp
#define SABER_BLADE2_RATE 0.78f   // red blade angular speed relative to blue (≠1 so clashes drift)
```

- [ ] **Step 3: Add the second-blade angle state**

In `src/radar_view.cpp`, right after line 97 (`static float s_prevSweepDeg = 0.0f;`), add:

```cpp
static float       s_sweepDeg2 = 180.0f;   // Saber red blade (counter-rotating); starts opposite the blue
static float       s_prevSweepDeg2 = 180.0f;
```

- [ ] **Step 4: Zero/seed the second blade where the first is initialised**

In `src/radar_view.cpp` near lines 1039-1040 (`s_sweepDeg = 0.0f; s_prevSweepDeg = 0.0f;`), add right after:

```cpp
    s_sweepDeg2 = 180.0f;
    s_prevSweepDeg2 = 180.0f;
```

- [ ] **Step 5: Give `wedge_bbox` a direction argument**

In `src/radar_view.cpp`, change the signature and the trail-angle line of `wedge_bbox` (currently starts at line 388). Replace:

```cpp
static void wedge_bbox(float deg, lv_area_t *out) {
    lv_coord_t minx = s_cx, maxx = s_cx, miny = s_cy, maxy = s_cy;
    const int steps = 10;
    for (int i = 0; i <= steps; ++i) {
        const float a = deg - SWEEP_TRAIL_DEG * (float)i / (float)steps;
```

with:

```cpp
static void wedge_bbox(float deg, int dir, lv_area_t *out) {
    lv_coord_t minx = s_cx, maxx = s_cx, miny = s_cy, maxy = s_cy;
    const int steps = 10;
    for (int i = 0; i <= steps; ++i) {
        const float a = deg - (float)dir * SWEEP_TRAIL_DEG * (float)i / (float)steps;
```

Then update the two existing callers (currently lines 480-481) from:

```cpp
    wedge_bbox(s_prevSweepDeg, &a);
    wedge_bbox(s_sweepDeg, &b);
```

to:

```cpp
    wedge_bbox(s_prevSweepDeg, +1, &a);
    wedge_bbox(s_sweepDeg, +1, &b);
```

- [ ] **Step 6: Extract `draw_sweep_arm` and rewrite `sweep_draw_cb`**

In `src/radar_view.cpp`, replace the entire current `sweep_draw_cb` (currently lines 357-386) with a helper + a thin dispatcher:

```cpp
static void draw_sweep_arm(lv_draw_ctx_t *dctx, float deg, int dir,
                           lv_color_t trailCol, lv_color_t leadCol) {
    const lv_point_t center = { s_cx, s_cy };
    const float R = (float)RADAR_R_OUTER_PX;

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = trailCol;
    ld.width = 5;
    ld.round_start = 1;
    ld.round_end = 1;
    for (int i = SWEEP_TRAIL_STEPS; i >= 1; --i) {
        const float frac = 1.0f - (float)i / (float)SWEEP_TRAIL_STEPS;
        const float ang  = deg - (float)dir * (float)i * (SWEEP_TRAIL_DEG / (float)SWEEP_TRAIL_STEPS);
        ld.opa = (lv_opa_t)(frac * frac * (float)SWEEP_TRAIL_OPA);
        if (ld.opa < 2) continue;
        lv_point_t p2 = rim_point(ang, R);
        lv_draw_line(dctx, &ld, &center, &p2);
    }
    lv_draw_line_dsc_t le;
    lv_draw_line_dsc_init(&le);
    le.color = leadCol;
    le.width = 2;
    le.opa = 217;
    le.round_start = 1;
    le.round_end = 1;
    lv_point_t lead = rim_point(deg, R);
    lv_draw_line(dctx, &le, &center, &lead);
}

static void sweep_draw_cb(lv_event_t *e) {
    if (orb() || !s_desc->sweep) return;
    lv_draw_ctx_t *dctx = lv_event_get_draw_ctx(e);

    if (s_theme == THEME_SABER) {
        // Duel: blue blade CW, red blade CCW — both glow in their own colour.
        const lv_color_t blue = lv_color_hex(0x2A6AFF);   // = kThemes[THEME_SABER].lead
        const lv_color_t red  = lv_color_hex(0xFF2A2A);   // saber red (matches emergency halo)
        draw_sweep_arm(dctx, s_sweepDeg,  +1, blue, blue);
        draw_sweep_arm(dctx, s_sweepDeg2, -1, red,  red);
        return;
    }
    draw_sweep_arm(dctx, s_sweepDeg, +1, s_cRing, s_cLead);
}
```

- [ ] **Step 7: Advance + invalidate the second blade in `sweep_timer_cb`**

In `src/radar_view.cpp`, the primary sweep advance/invalidate lives at (currently) lines 475-486. Immediately AFTER line 477 (`if (s_sweepDeg >= 360.0f) s_sweepDeg -= 360.0f;`), add the counter-rotating advance:

```cpp
    const bool saberDuel = (s_theme == THEME_SABER);
    if (saberDuel) {
        s_prevSweepDeg2 = s_sweepDeg2;
        s_sweepDeg2 -= 360.0f * (float)SWEEP_FRAME_MS / (float)SWEEP_PERIOD_MS * SABER_BLADE2_RATE;
        if (s_sweepDeg2 < 0.0f) s_sweepDeg2 += 360.0f;
    }
```

Then immediately AFTER the primary `lv_obj_invalidate_area(s_sweep, &area);` (currently line 486), add the second-blade invalidation:

```cpp
    if (saberDuel) {
        lv_area_t a2, b2, area2;
        wedge_bbox(s_prevSweepDeg2, -1, &a2);
        wedge_bbox(s_sweepDeg2, -1, &b2);
        area2.x1 = LV_MIN(a2.x1, b2.x1);
        area2.y1 = LV_MIN(a2.y1, b2.y1);
        area2.x2 = LV_MAX(a2.x2, b2.x2);
        area2.y2 = LV_MAX(a2.y2, b2.y2);
        lv_obj_invalidate_area(s_sweep, &area2);
    }
```

- [ ] **Step 8: Bump the version**

In `src/config.h`, change `#define FW_VERSION "1.8.0"` to `#define FW_VERSION "1.8.1"`.

- [ ] **Step 9: Compile the sim**

Run: `pio run -e native`
Expected: build SUCCESS. (No `wedge_bbox`/`sweep_draw_cb` signature mismatch — all callers updated.)

- [ ] **Step 10: Commit**

```bash
git add src/radar_view.cpp src/config.h
git commit -m "feat(saber): counter-rotating red duel blade + 2x Browncoat box (v1.8.1)"
```

---

## Self-Review

- **Spec coverage:** second red counter-rotating blade (Steps 2-7) ✓; Saber-only gate (`s_theme == THEME_SABER` in both draw + timer) ✓; different rate so clashes drift (`SABER_BLADE2_RATE 0.78`) ✓; respects sweep toggle (blade-2 advance sits below the `s_sweepEnabled`/`s_sweep` guards at lines 474/478) ✓; blades glow in own colour (trail colour == blade colour) ✓; Browncoat box 2× (Step 1) ✓; version bump (Step 8) ✓.
- **Type consistency:** `draw_sweep_arm(ctx, deg, dir, trailCol, leadCol)` defined before its 3 call sites; `wedge_bbox(deg, dir, out)` new signature with both callers updated + the two new blade-2 callers; `s_sweepDeg2`/`s_prevSweepDeg2` declared (Step 3) and initialised (Step 4) before use.
- **Placeholder scan:** none — full code in every step.
- **Render-path risk:** this touches the sweep draw + invalidation, exactly the class of change the project rule says MUST get an independent Opus review. Review happens after this task, before flashing.
