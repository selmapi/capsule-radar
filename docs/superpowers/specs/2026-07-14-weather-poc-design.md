# Weather POC — 2-hour precipitation loop + storm alert

**Date:** 2026-07-14
**Status:** Design — pending spec review
**Version target:** v1.11.0

## Goal

Add a **Weather** view (a 4th swipe tile) that shows an **animated 2-hour precipitation loop** centered on the home location, plus a **rain/storm alert** surfaced two ways: a pill on the Weather tile **and** a persistent raincloud icon in the HUD on every screen (with the existing alert sound). A proof-of-concept that the radar can branch into a second use case.

Requested by a coworker ("local weather loop + storm/rain alert"). Scoped to **2 hours** because that's the free RainViewer history window (verified: its public API serves 13 past frames spanning exactly 2 h).

## Reuse vs. new

**Reused:** the `lv_tileview` (Radar/List/Stats → add Weather as tile 3), the HTTPS-fetch pattern (`WiFiClientSecure` + `HTTPClient`, as in `adsb_client`/`photo_client`), the vector coastline drawing + geo→screen projection, the alert-sound system, and the HUD (for the new icon).

**New:**
1. A **PNG decoder** — RainViewer tiles are PNG with alpha; our existing decoder (TJpgDec) is JPEG-only. Add a lightweight PNG decoder (lodepng, or LVGL's `lv_png`). This is the one genuinely new capability.
2. A **weather client** (`weather.*`, device-only) running on core 0: fetches RainViewer frames + tiles and Open-Meteo conditions/forecast, into shared state read by the UI.
3. A **weather view** in `ui.*`: the animated tile + the HUD raincloud icon.

## Data sources (both free, verified)

- **Precipitation tiles — RainViewer.** `GET https://api.rainviewer.com/public/weather-maps.json` → `radar.past[]` = 13 frames (`{time, path}`) + `host`. Tile URL: `{host}{path}/256/{z}/{x}/{y}/{color}/{smooth}_{snow}.png`. Use `color=4` (a standard scheme; tunable), `smooth=1`, `snow=1`. PNG + alpha. Refresh the frame list every ~10 min.
- **Conditions + alert — Open-Meteo.** `GET https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=temperature_2m,weather_code,wind_speed_10m,relative_humidity_2m,precipitation&minutely_15=precipitation,weather_code&forecast_minutely_15=8&timezone=auto`. No API key. Poll every ~5 min.

## The precipitation loop

- **Coverage:** home-**centered** regional view. Compute home's Web-Mercator pixel at zoom **z=6** (~550 km wide — wide enough to see storms approaching well beyond aircraft range), take the 2×2 block of 256-px tiles around it, composite, and crop the home-centered window. Downscale each composited frame to a **384×384 RGB565** buffer.
- **Memory budget:** 13 frames × 384×384×2 ≈ **3.7 MB** cached in PSRAM — within the several-MB free headroom (the LVGL draw buffer is in internal SRAM; only the ~434 KB trail canvas lives in PSRAM). PNG decode scratch is one 256-px tile at a time (~128 KB).
- **Progressive load:** fetch + show the **newest** frame first (instant "now" view), then backfill the older 12 in the background so the tile is useful within a second or two rather than after all 52 tile fetches. Reuse the HTTPS connection (keep-alive) across tiles.
- **Animation:** auto-play the 13 frames at ~3 fps on a repeating loop; a scrubber shows position (−2 h → now); drag-to-scrub is a nice-to-have, not required for the POC.
- **Compositing:** precipitation drawn over the theme-tinted **coastline** (reused vector map) with the **home marker** at center. Precip keeps RainViewer's standard intensity colors (green→red) — conventional and readable; only the chrome/coastline follow the active theme.

## The alert

- **Trigger:** from Open-Meteo — fire when `current.weather_code` OR any of the next-2-h `minutely_15.weather_code` indicates **rain** (51–55, 61–65, 80–82) or **thunderstorm** (95, 96, 99), or `minutely_15.precipitation` crosses a threshold. Thunderstorm = higher severity than rain.
- **Two surfaces (both):**
  1. **Weather-tile pill** — e.g. "⛈ Storm cell · ~30 min out" / "🌧 Rain approaching".
  2. **Persistent HUD raincloud icon** — shown on every screen (near the existing WiFi/GPS/battery cluster), so a storm reaches the user without opening the Weather tile. Amber for rain, red for thunderstorm; hidden when clear.
- **Sound:** on the *onset* of a new rain/storm condition, play an alert cue (reuse the audio system; a distinct weather cue or the existing emergency/inbound tone), gated by the existing mute/alert settings. One-shot on onset, not repeated every poll (same onset-tracking pattern as `checkAudioEvents`).

## Layout (approved mockup)

Round tile: coastline base (theme-tinted) + animated precip + centered home dot. Top: current-frame time + a plain-language summary ("Rain approaching from the west"). Storm pill below that when active. Bottom: a scrubber (−2 h ─●─ now, auto-playing) and a conditions line (temp · condition · wind · humidity).

## Config

Add to the redesigned config page (a **Weather** group, or fold into Traffic/Sound): **Weather tile on/off** and **Storm alert on/off** (+ mute already exists). Off by default is fine for a POC so existing users are unaffected until they enable it. Persist in NVS. The Weather tile is only added to the tileview when enabled.

## Phasing (for the plan)

- **Phase A — data + alert (no PNG decoder yet):** Open-Meteo client, the HUD raincloud icon, the alert logic + sound, and a Weather tile that shows **conditions + summary + the alert pill** (text/vector only). Ships the coworker's actual "warn me about storms" need first, end-to-end testable.
- **Phase B — the loop:** add the PNG decoder + RainViewer tile client + the animated precip loop + scrubber into the Weather tile.

## New dependency note

The PNG decoder (lodepng / `lv_png`) adds flash + a little RAM. Confirm it fits the build (current firmware ≈ 44 % flash — ample). It only needs to decode 256-px tiles into an RGB565/ARGB buffer; no display-integration of `lv_png`'s image-source path is required if we decode manually like `photo_client` does for JPEG.

## Non-goals (YAGNI / POC boundaries)

- **No 4-hour loop** — 2 h is the free data window; 4 h needs paid data.
- **No historical persistence** — frames are re-fetched on demand; nothing stored to flash/SD across reboots.
- **No forecast animation** (RainViewer nowcast) — past-only loop for the POC.
- **No worldwide panning/zoom controls** — fixed home-centered z=6 view (zoom can become a setting later).
- **Fallback if Phase-B fetch/decode proves too heavy on-device:** drop to a **single home-containing tile** (13 fetches, ~1.7 MB, home marked at true position) rather than the 2×2 composite. Documented so the build can make that call with evidence.

## Testing / acceptance

The weather pipeline is device-only (network + PNG decode), so the SDL sim can't exercise it — the sim will render the Weather **tile chrome/layout with mock data** (so we can iterate the layout), and real verification is on-device:
1. **API sanity (off-device):** `curl` the RainViewer weather-maps.json + one tile URL, and the Open-Meteo URL for the home coordinates — confirm shapes before wiring.
2. **Firmware compile** — `pio run -e esp32-s3-amoled-175` clean with the PNG decoder added.
3. **On-device (Selma):** swipe to the Weather tile → conditions + (Phase B) an animated 2-h loop centered on home; force/observe a rain condition → HUD raincloud icon appears on the radar screen + alert sound; confirm memory is healthy (serial `[mem]` free-heap/PSRAM line stays safe) and the radar itself is unaffected.

## Review

Sonnet implements; Opus reviews **memory safety** specifically (the 3.7 MB frame cache + PNG-decode scratch must not starve the TLS handshake / ADS-B path — the same contiguous-RAM concern that shaped `photo_client` and the draw-buffer placement), plus the alert onset-tracking (no repeated sound) and the core-0/UI shared-state locking.
