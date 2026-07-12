# Capsule Radar — Sound & Alerts

**Date:** 2026-07-09
**Repo:** `selmapi/capsule-radar`, branch `feature/sound-pack`
**Hardware:** ES8311 codec + speaker, 16 kHz/16-bit, synthesized sine tones via `gen_beep`.

## Goal

Give events distinct, recognizable sounds instead of two generic beeps, plus a red-alert screen flash. All sounds are **synthesized** (original tone sequences — no audio samples/files).

### Event → sound map (Selma's picks)
| Event | Sound | Notes |
|-------|-------|-------|
| New contact | **Soft ping** | gentle single tone |
| Inbound / closest-approach | **Sonar ping** | low single tone (hooks the existing proximity trigger) |
| Military contact | **Low descending** | split OUT from emergency (today military shares the emergency sound) |
| Emergency squawk (7500/7600/7700) | **Klaxon** | alternating urgent tones — on all themes… |
| …**except Mass Effect theme** | **Menace horn** | an ORIGINAL deep detuned two-tone (Reaper-*flavored*, not a sample) |
| Any emergency | **+ red-alert screen flash** | brief red scope tint paired with the sound, all themes |

One consistent set across themes; the ME emergency is the only per-theme override.

## Architecture

### `src/audio.{h,cpp}` — a small sound font
Extend `enum AudioCue` (currently `AUDIO_NEW=0, AUDIO_ALERT=1`):
```cpp
enum AudioCue { AUDIO_NEW=0, AUDIO_EMERGENCY=1, AUDIO_MILITARY=2, AUDIO_INBOUND=3, AUDIO_ME_EMERGENCY=4 };
```
(`AUDIO_ALERT` → `AUDIO_EMERGENCY`; update its one call site.) **Self-test:** today `play_cue` uses the magic `cue==2`, which now collides with `AUDIO_MILITARY`. Give self-test its own sentinel (e.g. `AUDIO_SELFTEST=100`) and update `audio_selftest()` — do NOT let it overlap the enum.

Rewrite `play_cue(int cue)` as a `switch`, each cue a short **sequence** of `gen_beep` tones played back-to-back through I2S (PA on before, off after, small inter-note gaps — mirror the existing AUDIO_ALERT double-beep pattern). Add a **note-sequence helper** so each cue is a small table of `{freq, ms, gapMs}`. Sounds (starting values, tuned on-device):
- **NEW:** ~880 Hz, 150 ms, soft.
- **INBOUND:** ~560 Hz, 170 ms.
- **MILITARY:** 440 Hz 90 ms → 330 Hz 130 ms (descending).
- **EMERGENCY (klaxon):** 760/560 Hz alternating, ~4 notes, 150 ms each.
- **ME_EMERGENCY (menace horn):** TWO detuned sines summed for a beating drone — add a `gen_beep2(buf,cap,f1,f2,ms,amp)` that writes `amp*(sin f1 + sin f2)/2` (same fade/anti-click as `gen_beep`); play ~180/190 Hz for ~600 ms, twice. NOTE: a small speaker rolls off deep bass, so the on-device tuning pass will pitch it into the audible low-mids while keeping the beat/menace.

`audio_play(AudioCue)`, volume/mute, and the playback task are unchanged.

### `src/main.cpp` — `checkAudioEvents()` trigger changes
Currently: `emergency = acIsEmergency(squawk) || ac.military`; proximity fires `AUDIO_ALERT`; new fires `AUDIO_NEW`. Change to:
- **Emergency** (squawk 7500/7600/7700) → `AUDIO_ME_EMERGENCY` if `radar::theme() == THEME_MASSEFFECT`, else `AUDIO_EMERGENCY`. **Also** call `radar::flashAlert()` (red screen flash) on emergency. Gated by `g_alertMode >= 1`.
- **Military** (`ac.military`, and NOT emergency) → `AUDIO_MILITARY`. Gated by `g_alertMode >= 2`.
- **Inbound/proximity** (the existing `seenProx` new-in-proximity path) → `AUDIO_INBOUND`. Gated by `g_alertMode >= 2`.
- **New contact** → `AUDIO_NEW` (rate-limited, `g_alertMode >= 2`, unchanged).
Priority when an aircraft matches several: emergency > military > inbound > new (play the most urgent once per aircraft/poll). `radar::theme()` + `THEME_MASSEFFECT` (=12) already exist.

### `src/radar_view.{h,cpp}` — red-alert flash
Add `void flashAlert();` — a brief (~900 ms) full-screen red tint overlay (a hidden `lv_obj` covering the scope, semi-transparent red, shown then hidden via a one-shot `lv_timer`, same pattern as `flashRefresh`). `checkAudioEvents` runs on core 1 (the LVGL loop), so calling it there is safe.

## Verification

- Compile both envs per task.
- **No audio in the SDL sim** (no speaker), so functional acceptance is **on-device** (flash → listen). A tuning pass with Selma: dial each cue's pitch/tempo, especially the menace-horn for the small speaker; confirm military/inbound/emergency are distinguishable, the ME override fires on the Mass Effect theme, and the red flash pairs with emergencies. Existing `audio_selftest()` still works.
- Independent Opus review on the `play_cue` rewrite (buffer sizing per sequence, self-test sentinel collision, PA gating) + the trigger priority logic.
- Then bump `FW_VERSION` → 1.7.0, flash, redeploy the flasher.

## Out of scope / deferred

The literal fan-made Reaper MP3 (would need a PCM-sample-playback subsystem + is kept out of the public repo/flasher on licensing grounds) — available later as a personal-only build if wanted. Per-theme sound character beyond the ME override. Star Wars / Star Trek / Firefly themes (pinned wishlist). Idle & polish pack.
