# Sound & Alerts — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** A 5-cue synthesized sound font (new / inbound / military / emergency-klaxon / ME menace-horn), split military out of the emergency sound, a per-theme ME emergency override, and a red-alert screen flash.

**Architecture:** Extend `audio.{h,cpp}` (enum + `play_cue` rewrite + a detuned-tone helper); change `checkAudioEvents()` triggers in `main.cpp`; add `radar::flashAlert()`. All sounds are synthesized tone sequences — no audio samples.

**Tech Stack:** C++/Arduino/LVGL, ES8311 over I2S. `audio.cpp` is device-only (not native env). **No audio in the SDL sim**, so per-task verification is *compilation*; functional acceptance is on-device (Task 4). Existing 500 ms PSRAM scratch buffer (`S_BUF_LEN`) per note is reused.

**Ground rules:** branch `feature/sound-pack`; commit per task; don't push (flash/redeploy at end).

---

## Task 1: Sound font — enum + play_cue rewrite + detuned helper

**Files:** `src/audio.h`, `src/audio.cpp`

- [ ] **Step 1 — `audio.h`:** replace the enum:
```cpp
enum AudioCue {
    AUDIO_NEW           = 0,   // new aircraft: soft ping
    AUDIO_EMERGENCY     = 1,   // 7500/7600/7700: klaxon
    AUDIO_MILITARY      = 2,   // military contact: low descending
    AUDIO_INBOUND       = 3,   // closest/inbound: sonar ping
    AUDIO_ME_EMERGENCY  = 4,   // Mass Effect theme emergency: menace horn
};
```
(Keep the file's comments style.)

- [ ] **Step 2 — `audio.cpp`:** the current `play_cue(int cue)` uses `cue == AUDIO_ALERT` and the magic `cue == 2` for self-test — `2` now collides with `AUDIO_MILITARY`. Fix + rewrite:
  - Add a self-test sentinel: `#define CUE_SELFTEST 100` (or `static const int`). Update `audio_selftest()` (grep for where it calls into play/sets `s_cue`) to use `CUE_SELFTEST`.
  - Add a detuned-tone generator next to `gen_beep`:
```cpp
// two detuned sines summed → a beating "drone" (menace). Same anti-click fade as gen_beep.
static size_t gen_beep2(int16_t *buf, size_t cap, float f1, float f2, int ms, float amp) {
    size_t n = (size_t)((int64_t)SR * ms / 1000); if (n*2 > cap) n = cap/2;
    const size_t fade = SR / 200;
    size_t i = 0;
    for (; i < n; ++i) {
        float env = 1.0f;
        if (i < fade) env = (float)i / fade;
        else if (i > n - fade) env = (float)(n - i) / fade;
        float s = 0.5f * (sinf(2*(float)M_PI*f1*i/SR) + sinf(2*(float)M_PI*f2*i/SR));
        int16_t v = (int16_t)(amp * env * s);
        buf[i*2] = v; buf[i*2+1] = v;
    }
    return i * 2;
}
```
  - Add a small **note-player helper** that plays a sequence of tones through I2S (mirrors the existing AUDIO_ALERT loop: `gen_beep` → `i2s_write` → `delay(gap)`):
```cpp
struct Note { float freq; int ms; int gapMs; };
static void play_notes(int16_t *buf, float amp, const Note *notes, int count) {
    size_t bw;
    for (int k = 0; k < count; ++k) {
        size_t ns = gen_beep(buf, S_BUF_LEN, notes[k].freq, notes[k].ms, amp);
        i2s_write(I2S_PORT, buf, ns * 2, &bw, portMAX_DELAY);
        if (notes[k].gapMs) delay(notes[k].gapMs);
    }
}
```
  - Rewrite `play_cue` as a switch. Keep the PA-on / `delay(8)` / … / `delay(90)` / PA-off envelope around the whole thing (as today). Per-cue (tune values on-device later; these are starting points):
    - `AUDIO_NEW`: one note `{880,150,0}` at reduced amp (~0.7×).
    - `AUDIO_INBOUND`: one note `{560,170,0}`.
    - `AUDIO_MILITARY`: `{440,90,20},{330,130,0}`.
    - `AUDIO_EMERGENCY`: `{760,150,25},{560,150,25},{760,150,25},{560,150,0}`.
    - `AUDIO_ME_EMERGENCY`: two `gen_beep2` swells, e.g. `f1=185,f2=196,ms=560` then a 60 ms gap then the same again (tune pitch up if inaudible on the speaker).
    - `CUE_SELFTEST`: the existing ~2 s 1000 Hz tone.
  - Keep the mute/vol guard (`if (s_muted && cue != CUE_SELFTEST)`) and `s_vol` amp calc.

- [ ] **Step 3 — Verify:** `pio run -e esp32-s3-amoled-175` → SUCCESS. (native env doesn't build audio.cpp.) Code-trace: each cue maps to its sequence; self-test no longer collides with a real cue; the old single `AUDIO_ALERT` call site is updated to `AUDIO_EMERGENCY` (grep — if `main.cpp` still references `AUDIO_ALERT` it won't compile, which is fine, Task 2 fixes triggers; but the ENUM value rename means `AUDIO_ALERT` no longer exists → Task 1 must at least not break other TUs. If `main.cpp` references `AUDIO_ALERT`, either keep a `#define AUDIO_ALERT AUDIO_EMERGENCY` shim temporarily OR note that Task 2 immediately follows. Prefer: leave a one-line back-compat `enum` alias is not possible; instead Task 1 should `grep AUDIO_ALERT src/*.cpp` and update the 2 call sites in main.cpp to AUDIO_EMERGENCY as part of Step 2 so the build stays green.)

- [ ] **Step 4 — Commit:** `git add src/audio.h src/audio.cpp src/main.cpp && git commit -m "feat(sound): 5-cue sound font (ping/sonar/mil/klaxon/menace) + detuned tone gen"`

---

## Task 2: Trigger logic in checkAudioEvents

**Files:** `src/main.cpp` (`checkAudioEvents`, ~lines 207-239)

- [ ] **Step 1 —** Read the current loop. It computes `emergency = acIsEmergency(ac.squawk) || ac.military`, has a `seenProx` proximity path firing `AUDIO_ALERT`, and a new-contact path firing `AUDIO_NEW`. Restructure per-aircraft to pick ONE cue by priority (most urgent wins), gated by `g_alertMode` (0=off, 1=emergencies, 2=new+all):
```cpp
    const bool isEmerg = acIsEmergency(ac.squawk);
    const bool isMil   = ac.military;
    const bool isNewProx = (!first && !seenProx.count(hex));   // entered inner proximity
    if (isEmerg) {
        if (g_alertMode >= 1) {
            audio_play(radar::theme() == THEME_MASSEFFECT ? AUDIO_ME_EMERGENCY : AUDIO_EMERGENCY);
            radar::flashAlert();                                // red screen flash, all themes
        }
    } else if (g_alertMode >= 2) {
        if      (isMil)                                   audio_play(AUDIO_MILITARY);
        else if (isNewProx)                              audio_play(AUDIO_INBOUND);
        else if (/* brand-new in range, existing rate-limited path */)  audio_play(AUDIO_NEW);
    }
```
Preserve the existing `seen`/`seenProx` set bookkeeping and the new-contact rate-limit (`millis()-lastNew>3000`). Ensure only ONE cue plays per aircraft per poll. `radar::theme()` and `THEME_MASSEFFECT` come from `radar_view.h` (already included). `radar::flashAlert()` is declared in Task 3 — include order: since Task 3 adds the declaration, do Task 3 first OR add the call and let Task 3 land the decl before building; to keep each task's build green, **Task 2 depends on Task 3's declaration** — so implement Task 3 before Task 2, OR fold the `flashAlert` decl into radar_view.h as the very first step here. Simplest: this plan runs Task 3 BEFORE Task 2. (Reordered below is fine; keep the numbering but build Task 3 first if needed.)

- [ ] **Step 2 — Verify:** `pio run -e esp32-s3-amoled-175` → SUCCESS. Code-trace the priority (emergency→mil→inbound→new), the ME theme override, alertMode gating, and that no aircraft double-pings.

- [ ] **Step 3 — Commit:** `git add src/main.cpp && git commit -m "feat(sound): split military/inbound cues, ME emergency override, red-flash on emergency"`

---

## Task 3: Red-alert screen flash (do this before Task 2's build)

**Files:** `src/radar_view.h`, `src/radar_view.cpp`

- [ ] **Step 1 — `radar_view.h`:** declare `void flashAlert();` next to `flashRefresh()`.

- [ ] **Step 2 — `radar_view.cpp`:** mirror the `flashRefresh` pattern (hidden overlay + one-shot hide timer), but a semi-transparent RED full-screen rect over the scope:
  - file-static `static lv_obj_t *s_alertFlash = nullptr;`
  - in `init()` (near the mascot/refresh label): create a full-screen `lv_obj` covering `SCREEN_W×SCREEN_H`, `bg_color = red` (e.g. `lv_color_hex(0xFF2020)`), `bg_opa ≈ 70` (translucent), no scroll/click, hidden by default, on top.
  - `flashAlert()`: show it + a one-shot `lv_timer` (~900 ms) that hides it (same as `refresh_hide_cb`).

- [ ] **Step 3 — Verify:** both `pio run -e native` and `-e esp32-s3-amoled-175` → SUCCESS (radar_view.cpp is in the native build). Code-trace: `flashAlert()` shows the red overlay ~900 ms then hides; created before init()'s final setTheme so it's ready.

- [ ] **Step 4 — Commit:** `git add src/radar_view.h src/radar_view.cpp && git commit -m "feat(sound): radar::flashAlert() red-alert screen flash"`

---

## Task 4: Bump version, flash, on-device listening/tuning, redeploy

- [ ] **Step 1** — `FW_VERSION` → `1.7.0`; commit.
- [ ] **Step 2** — merge `feature/sound-pack` → main, push; flash `PLATFORMIO_UPLOAD_PORT="/dev/cu.usbmodem111201" pio run -e esp32-s3-amoled-175 -t upload` (warn Selma; C3 plane radar also on the Mac — target the S3 port); confirm boot via serial.
- [ ] **Step 3 — On-device listening (with Selma):** trigger/observe each cue (new/inbound/military/emergency + ME-theme emergency + red flash). Tune each cue's pitch/tempo/amp in `play_cue` — especially the menace-horn for the small speaker (pitch up until audible + ominous). Confirm cues are distinguishable, ME override fires only on Mass Effect, red flash pairs with emergencies, volume/mute/alert-mode still work. `audio_selftest()` still fine.
- [ ] **Step 4** — `gh workflow run webflasher.yml --repo selmapi/capsule-radar --ref main` to redeploy at v1.7.0.

---

## Self-review checklist
- Self-test sentinel (100) no longer collides with `AUDIO_MILITARY` (2). ✓
- `AUDIO_ALERT` rename: all call sites updated → build stays green. ✓
- One cue per aircraft per poll, priority emergency>mil>inbound>new, alertMode-gated. ✓
- ME emergency override keyed on `radar::theme()==THEME_MASSEFFECT`. ✓
- flashAlert declared before its use (Task 3 before Task 2's build). ✓
- All sounds synthesized (gen_beep/gen_beep2) — no audio samples/files. ✓
- Verification is on-device (no sim audio); Opus review on play_cue rewrite + trigger priority.
