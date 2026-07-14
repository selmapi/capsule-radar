# Config Page Redesign — grouped accordion + chunked backend

**Date:** 2026-07-14
**Status:** Approved (design) — pending spec review
**Version target:** v1.9.0

## Goal

Rework the local config page served at `capsuleradar.local` so it is (1) **organized** into logical collapsible groups instead of one 14-control mega-card, (2) **structurally safe** — no more single fixed `snprintf` buffer that can silently truncate, and (3) **friendlier** — live settings confirm they saved. Global settings only this round; leave a clean seam for future per-theme customization.

## Problems being fixed

1. **Mega "Display" card** — 14 unrelated controls (display + traffic filters + motion) in one bucket.
2. **The truncation landmine** — the whole page is one `snprintf(buf, 24576, …)` into a fixed PSRAM buffer. It already overflowed once (killed the map + all JS). Every new setting creeps toward the cap.
3. **No save feedback** — live settings fire a silent `fetch()`; the user never sees confirmation.

## Layout — accordion, 6 groups

A single scrolling page of collapsible cards (accordion). Tap a header to expand/collapse. First group open by default, rest collapsed. Phosphor-green aesthetic retained (it's the brand). Each group header shows an icon + a short current-value hint.

| Group | Contents | Apply |
|-------|----------|-------|
| ⌖ **Location & Range** | Leaflet map, latitude, longitude, GPS toggle (‑G only), display range | **Save & restart** (geo re-init) |
| ◐ **Appearance** | Theme, brightness, dim-after, show sweep, units, screen rotation | Live |
| ✈ **Traffic & Filters** | Show airports, hide-ground, military-only, min altitude, trails, max aircraft | Live |
| ⤾ **Motion** | Auto-rotate, shake-to-refresh, wake-on-motion | Live |
| ♪ **Sound & Alerts** | Volume, mute, alert mode, proximity alert, test ping | Live |
| ⏱ **Time & System** | Time zone, reset WiFi, firmware update, version | Live / actions |

### Live-behavior changes (so the grouping is honest)
Currently the **Save & restart** form bundles lat/lon/range/**theme**/**tz**. Theme and time zone are moved out to their natural groups and made **live** (both are already live-capable on the device):
- **Theme** → Appearance, applied by a new `GET /theme?v=N&save=1` (calls `radar::setTheme` + the existing `saveTheme` persist). No reboot to change theme from the web anymore.
- **Time zone** → Time & System, applied live via `setenv("TZ",…) + tzset()` (new/extended handler), persisted to NVS.
- **Location & Range** then needs a restart only for lat/lon/range (genuine geo re-init) — the one clearly-labeled "Save & restart" form.

All other settings keep their existing live `fetch()` endpoints unchanged.

## Backend — chunked streaming (the structural fix)

Replace the single `snprintf(buf, BUFSZ, …)` + `g_web.send(buf)` with **chunked transfer**:

```
g_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
g_web.send(200, "text/html", "");        // start streaming
g_web.sendContent(headAndStyle);         // <head> + CSS
g_web.sendContent(cardLocation);         // one card per group
g_web.sendContent(cardAppearance);
…                                        // each card built into a modest String
g_web.sendContent(scriptAndTail);        // <script> + </body></html>
g_web.sendContent("");                   // end
```

- Each chunk is a small, self-contained `String` (a card or the head/script), built with the existing option-list helpers (`ropts`, `topts`, `iopts`, …). None approaches any fixed cap.
- **Removes the failure mode entirely** — there is no single buffer to overflow, so the page can never again silently truncate. Settings can be added indefinitely.
- Keep it readable: a small helper per card (e.g. `sendCard_appearance()`), so `handleRoot()` becomes a short sequence of `sendContent` calls instead of a 100-line format string. This also directly improves the file (the current `handleRoot` is unwieldy).
- PSRAM `ps_malloc` big-buffer goes away; per-card `String`s are short-lived and small.

## Save feedback

Each live-setting JS handler shows a brief **"Saved ✓"** toast after its `fetch()` resolves (a fixed bottom-center pill, ~1.2 s). Test-ping and reset actions show their own message ("♪ test ping", "WiFi reset"). Pure CSS/JS, no dependencies.

## Seam for future per-theme customization (NOT built this round)

Per-theme sound/feature customization (e.g. a distinct alert cue per theme) is a deliberate **future feature** — it needs a new per-theme storage model (NVS keys scoped by theme) and a "which theme am I editing" concept. It is out of scope here. The accordion accommodates it later with **zero rework**: a theme-scoped card (e.g. "Sound for *[active theme]*") slots under Appearance/Sound, while global settings (volume, mute, brightness) stay global. This round just avoids baking in any assumption that blocks it.

## Non-goals (YAGNI)

- No per-theme settings/storage (future feature; seam only).
- No change to which of lat/lon/range triggers a restart (still restart — geo re-init).
- No self-hosting of Leaflet/OSM tiles. The map needs internet regardless (tiles are remote); on a CDN-blocked network the map won't render, but the **lat/lon inputs work independently** and the page degrades gracefully. Documented limitation, not fixed here.
- No visual redesign beyond the regrouping + toast (keep the phosphor brand).

## Testing / acceptance

The web server is Arduino-only (not in the SDL sim), so verification is compile + on-device + curl:
1. **Firmware compile** — `pio run -e esp32-s3-amoled-175` clean. (`native` sim unaffected — it doesn't build main.cpp's web server.)
2. **Page completeness** — `curl http://capsuleradar.local/` (resolve to IP first; mDNS is flaky) returns a full document ending in `</html>`, with the map div, all 6 group cards, and the `<script>` present. Confirms chunked streaming produced a complete page (the old truncation symptom was a page cut mid-`<script>`).
3. **On-device (Selma):** open `capsuleradar.local` — all 6 groups render and collapse/expand; each live setting applies on the device and shows the "Saved ✓" toast; **theme changes live with no reboot**; time zone changes live; Save & restart still works for lat/lon/range; map loads on the home network.
4. **Regression:** every existing endpoint still works (brightness, volume, all toggles, motion, units, alerts, proximity, GPS, WiFi reset, firmware update).

## Review

Sonnet implements; Opus reviews the **chunked-streaming refactor specifically** (every setting still wired, no dropped chunk, correct `setContentLength(CONTENT_LENGTH_UNKNOWN)` + terminating empty `sendContent`, no use-after-scope of a `String` passed to `sendContent`).
