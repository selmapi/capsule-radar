# Weather POC — Phase A (data + alert + HUD + conditions tile) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Add a Weather tile (4th swipe view) showing current conditions + a plain-language summary, a rain/storm **alert** surfaced as a Weather-tile pill **and** a persistent HUD raincloud icon on every screen (+ alert sound), driven by Open-Meteo. No image loop yet (that's Phase B) — this ships the coworker's "warn me about storms" need end-to-end.

**Architecture:** A portable weather-state header/accessor (`weather.*`, compiled in both firmware and the SDL sim) holds the latest conditions + alert level under a mutex. A device-only client (`weather_client.*`, excluded from the native build) fetches Open-Meteo on core 0. `ui.*` renders the Weather tile + a global raincloud icon on `lv_layer_top()`. `main.cpp` polls the client, reads the state each loop to update the UI, fires the alert sound on onset, and adds a config toggle. Mirrors the existing `photo.h` (portable state) + `photo_client.cpp` (device fetch) split.

**Tech Stack:** C++17, Arduino (`WiFiClientSecure`+`HTTPClient`, `ArduinoJson`), LVGL v8, PlatformIO. Data: Open-Meteo (free, no key). Verify by firmware compile + native compile + `curl` + on-device.

**Data source (verified):** `GET https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,relative_humidity_2m,precipitation&minutely_15=precipitation,weather_code&forecast_minutely_15=8&timezone=auto`. WMO codes: rain = 51–55/61–65/80–82, thunderstorm = 95/96/99.

---

### Task WX-A1: Portable weather state (`weather.h` / `weather.cpp`)

**Files:**
- Create: `src/weather.h`, `src/weather.cpp`
- Modify: `platformio.ini:65` (add `+<weather.cpp>` to the native filter)

- [ ] **Step 1: `src/weather.h`**

```cpp
#pragma once
#include <cstdint>

namespace weather {

// alert levels (higher = more severe)
enum Alert : uint8_t { WX_CLEAR = 0, WX_RAIN = 1, WX_STORM = 2 };

struct State {
    bool     valid     = false;   // a successful fetch has happened
    float    tempC     = 0.0f;    // current temperature (°C)
    int      code      = 0;       // current WMO weather_code
    float    windKmh   = 0.0f;    // current wind speed (km/h; UI converts per units)
    int      windDir   = 0;       // current wind direction (deg, meteorological)
    int      humidity  = 0;       // %
    float    precipMm  = 0.0f;    // current precipitation (mm)
    uint8_t  alert     = WX_CLEAR;
    int      etaMin    = -1;      // minutes until incoming precip (-1 = none / already active)
    char     summary[48] = {0};   // e.g. "Thunderstorm nearby", "Rain likely in ~30 min"
    uint32_t updatedMs = 0;       // lv_tick of last successful update
};

const State& state();             // read the latest (copy is cheap; returns a ref to shared)
void         set(const State& s); // replace (thread-safe on device)

}  // namespace weather
```

- [ ] **Step 2: `src/weather.cpp`**

```cpp
#include "weather.h"
#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t s_mtx = nullptr;
#endif

namespace weather {

static State s_state;

const State& state() { return s_state; }

void set(const State& s) {
#if defined(ARDUINO)
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_state = s;
    if (s_mtx) xSemaphoreGive(s_mtx);
#else
    s_state = s;   // sim: single-threaded
#endif
}

}  // namespace weather
```

- [ ] **Step 3: Add `weather.cpp` to the native build filter**

In `platformio.ini` line 65, append `+<weather.cpp>` so the sim compiles the portable state (the UI reads it):

```
build_src_filter = -<*> +<radar_view.cpp> +<ui.cpp> +<route.cpp> +<photo.cpp> +<coastline.cpp> +<airports.cpp> +<sim_main.cpp> +<theme_table.cpp> +<weather.cpp>
```

- [ ] **Step 4: Compile both envs**

Run: `pio run -e esp32-s3-amoled-175 && pio run -e native`
Expected: both SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/weather.h src/weather.cpp platformio.ini
git commit -m "feat(weather): portable weather state + accessors"
```

---

### Task WX-A2: Open-Meteo client (`weather_client.h` / `weather_client.cpp`)

Device-only (network). Excluded from native automatically (not in the native filter). Models `adsb_client`'s fetch pattern.

**Files:** Create `src/weather_client.h`, `src/weather_client.cpp`

- [ ] **Step 1: `src/weather_client.h`**

```cpp
#pragma once
// Device-only: fetch Open-Meteo conditions/forecast into weather::state().
void weather_client_poll(double lat, double lon);   // blocking HTTPS fetch; call from core-0 task
```

- [ ] **Step 2: `src/weather_client.cpp`**

```cpp
#include "weather_client.h"
#include "weather.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <math.h>

static bool isRain(int c)  { return (c>=51&&c<=55)||(c>=61&&c<=65)||(c>=80&&c<=82); }
static bool isStorm(int c) { return c==95||c==96||c==99; }

void weather_client_poll(double lat, double lon) {
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,"
        "relative_humidity_2m,precipitation"
        "&minutely_15=precipitation,weather_code&forecast_minutely_15=8&timezone=auto",
        lat, lon);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) { Serial.println("[wx] begin failed"); return; }
    http.setConnectTimeout(6000);
    const int code = http.GET();
    if (code != 200) { Serial.printf("[wx] HTTP %d\n", code); http.end(); return; }

    // ~2 KB response; filter to the fields we use to keep the doc small.
    StaticJsonDocument<256> filter;
    filter["current"] = true;
    JsonObject fm = filter.createNestedObject("minutely_15");
    fm["weather_code"] = true; fm["precipitation"] = true;
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) { Serial.printf("[wx] json %s\n", err.c_str()); return; }

    weather::State s;
    JsonObject cur = doc["current"];
    s.valid    = true;
    s.tempC    = cur["temperature_2m"]        | 0.0f;
    s.code     = cur["weather_code"]          | 0;
    s.windKmh  = cur["wind_speed_10m"]        | 0.0f;
    s.windDir  = cur["wind_direction_10m"]    | 0;
    s.humidity = cur["relative_humidity_2m"]  | 0;
    s.precipMm = cur["precipitation"]         | 0.0f;

    // alert: current condition, else earliest rain/storm in the next 8x15min (2h)
    int    lvl = weather::WX_CLEAR, eta = -1;
    if (isStorm(s.code))      lvl = weather::WX_STORM;
    else if (isRain(s.code))  lvl = weather::WX_RAIN;
    JsonArray codes = doc["minutely_15"]["weather_code"];
    JsonArray prcp  = doc["minutely_15"]["precipitation"];
    for (int i = 0; i < (int)codes.size(); ++i) {
        int c = codes[i] | 0; float p = (i < (int)prcp.size()) ? (prcp[i] | 0.0f) : 0.0f;
        int L = isStorm(c) ? weather::WX_STORM : ((isRain(c) || p >= 0.3f) ? weather::WX_RAIN : weather::WX_CLEAR);
        if (L > lvl || (L != weather::WX_CLEAR && lvl == weather::WX_CLEAR)) {
            if (L > lvl) lvl = L;
            if (eta < 0 && L != weather::WX_CLEAR) eta = i * 15;   // minutes out
        }
    }
    s.alert  = (uint8_t)lvl;
    s.etaMin = (isRain(s.code) || isStorm(s.code)) ? -1 : eta;   // -1 = already active

    // human summary
    if      (s.alert == weather::WX_STORM && s.etaMin < 0) snprintf(s.summary, sizeof(s.summary), "Thunderstorm now");
    else if (s.alert == weather::WX_STORM)                 snprintf(s.summary, sizeof(s.summary), "Thunderstorm in ~%d min", s.etaMin);
    else if (s.alert == weather::WX_RAIN  && s.etaMin < 0) snprintf(s.summary, sizeof(s.summary), "Raining now");
    else if (s.alert == weather::WX_RAIN)                  snprintf(s.summary, sizeof(s.summary), "Rain likely in ~%d min", s.etaMin);
    else                                                   snprintf(s.summary, sizeof(s.summary), "Clear");

    s.updatedMs = lv_tick_get();
    weather::set(s);
    Serial.printf("[wx] %.0fC code=%d alert=%d eta=%d\n", s.tempC, s.code, s.alert, s.etaMin);
}
```

- [ ] **Step 3: Compile firmware**

Run: `pio run -e esp32-s3-amoled-175`
Expected: SUCCESS.

- [ ] **Step 4: Sanity-check the API off-device (optional but recommended)**

Run: `curl -s "https://api.open-meteo.com/v1/forecast?latitude=39.86&longitude=-104.67&current=temperature_2m,weather_code,wind_speed_10m,precipitation&minutely_15=precipitation,weather_code&forecast_minutely_15=8&timezone=auto" | head -c 400`
Expected: JSON containing `"current":{...}` and `"minutely_15":{...}`.

- [ ] **Step 5: Commit**

```bash
git add src/weather_client.h src/weather_client.cpp
git commit -m "feat(weather): Open-Meteo client (conditions + 2h rain/storm forecast)"
```

---

### Task WX-A3: HUD raincloud icon (global, all screens)

The existing HUD (`s_hudGps` etc.) lives on `s_tileRadar` — radar-only. The weather icon must show on **every** tile, so it goes on `lv_layer_top()`.

**Files:** Modify `src/ui.h`, `src/ui.cpp`

- [ ] **Step 1: Declare the setter in `src/ui.h`**

After the `ui_set_gps` declaration (line 12), add:

```cpp
void ui_set_weather(int alert, const char *summary);  // HUD raincloud (0=hide,1=rain amber,2=storm red) + Weather-tile refresh
```

- [ ] **Step 2: Add the icon handle + create it on the top layer**

In `src/ui.cpp`, near the other HUD statics (`s_hudGps` is at line 31), add:

```cpp
static lv_obj_t *s_hudWx = nullptr;   // global raincloud icon (lv_layer_top)
```

In `ui_create()` (after the tileview + HUD are built, near the end before `ui_show_view(0)` or wherever the HUD labels are created), create it on the top layer so it floats over all tiles:

```cpp
    s_hudWx = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_hudWx, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_hudWx, LV_SYMBOL_WARNING);   // stand-in glyph; amber/red tint conveys severity
    lv_obj_align(s_hudWx, LV_ALIGN_TOP_RIGHT, -16, 14);
    lv_obj_add_flag(s_hudWx, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hudWx, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
```

- [ ] **Step 3: Implement `ui_set_weather` (icon show/hide/tint)**

Add near `ui_set_gps`'s definition (line ~290):

```cpp
void ui_set_weather(int alert, const char *summary) {
    if (s_hudWx) {
        if (alert <= 0) {
            lv_obj_add_flag(s_hudWx, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_hudWx, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(s_hudWx,
                lv_color_hex(alert >= 2 ? 0xFF4030 : 0xFFB23C), 0);   // storm red / rain amber
        }
    }
    ui_weather_tile_refresh(summary);   // defined in WX-A4
}
```

- [ ] **Step 4: Compile (will fail until WX-A4 adds `ui_weather_tile_refresh`)**

Skip compiling standalone; commit together with WX-A4. (If you want an interim compile, temporarily stub `static void ui_weather_tile_refresh(const char*){}` — but WX-A4 replaces it.)

---

### Task WX-A4: Weather tile (4th view) — conditions + summary + alert pill

**Files:** Modify `src/ui.cpp` (tileview + a new tile + refresh), `src/ui.cpp` `ui_show_view`

- [ ] **Step 1: Add the tile handle + widgets statics**

Near `s_tileRadar/...` (line 21) add:

```cpp
static lv_obj_t *s_tileWx = nullptr;
static lv_obj_t *s_wxTitle = nullptr, *s_wxTemp = nullptr, *s_wxCond = nullptr, *s_wxPill = nullptr;
```

- [ ] **Step 2: Add the 4th tile and its widgets in `ui_create()`**

After `s_tileStats = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_LEFT);` (line 542), change Stats to allow swiping further right and add the Weather tile:

Replace line 542 with:
```cpp
    s_tileStats = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);   // was LV_DIR_LEFT — allow swipe to Weather
    s_tileWx    = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_LEFT);
    // Weather tile widgets (Phase A: text; Phase B adds the precip loop canvas)
    s_wxTitle = lv_label_create(s_tileWx);
    lv_obj_set_style_text_color(s_wxTitle, lv_color_hex(0x9affc8), 0);
    lv_obj_set_style_text_font(s_wxTitle, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_wxTitle, "WEATHER");
    lv_obj_align(s_wxTitle, LV_ALIGN_TOP_MID, 0, 60);

    s_wxTemp = lv_label_create(s_tileWx);
    lv_obj_set_style_text_font(s_wxTemp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_wxTemp, lv_color_hex(0xeafff3), 0);
    lv_label_set_text(s_wxTemp, "--\xC2\xB0");
    lv_obj_align(s_wxTemp, LV_ALIGN_CENTER, 0, -30);

    s_wxCond = lv_label_create(s_tileWx);
    lv_obj_set_style_text_color(s_wxCond, lv_color_hex(0x9affc8), 0);
    lv_obj_set_style_text_align(s_wxCond, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_wxCond, "No data yet");
    lv_obj_align(s_wxCond, LV_ALIGN_CENTER, 0, 12);

    s_wxPill = lv_label_create(s_tileWx);
    lv_obj_set_style_bg_color(s_wxPill, lv_color_hex(0x2a0808), 0);
    lv_obj_set_style_bg_opa(s_wxPill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wxPill, 8, 0);
    lv_obj_set_style_radius(s_wxPill, 16, 0);
    lv_obj_set_style_text_color(s_wxPill, lv_color_hex(0xff8a6a), 0);
    lv_obj_align(s_wxPill, LV_ALIGN_CENTER, 0, 70);
    lv_obj_add_flag(s_wxPill, LV_OBJ_FLAG_HIDDEN);
```

- [ ] **Step 3: Add `ui_weather_tile_refresh` + a data hook**

Add this function (called by `ui_set_weather`). It reads `weather::state()` for numbers and takes the summary string for the pill. Include `weather.h` at the top of ui.cpp (`#include "weather.h"`):

```cpp
static void ui_weather_tile_refresh(const char *summary) {
    const weather::State &w = weather::state();
    if (s_wxTemp && w.valid) { char b[16]; snprintf(b, sizeof(b), "%d\xC2\xB0", (int)lroundf(w.tempC)); lv_label_set_text(s_wxTemp, b); }
    if (s_wxCond) {
        char b[80];
        if (w.valid) snprintf(b, sizeof(b), "%s\nwind %d km/h \xC2\xB7 %d%%", summary ? summary : "", (int)lroundf(w.windKmh), w.humidity);
        else         snprintf(b, sizeof(b), "Waiting for data\xE2\x80\xA6");
        lv_label_set_text(s_wxCond, b);
    }
    if (s_wxPill) {
        if (w.alert >= 1 && summary) {
            char b[64]; snprintf(b, sizeof(b), "%s %s", w.alert >= 2 ? "\xE2\x9B\x88" : "\xF0\x9F\x8C\xA7", summary);
            lv_label_set_text(s_wxPill, b);
            lv_obj_set_style_bg_color(s_wxPill, lv_color_hex(w.alert >= 2 ? 0x2a0808 : 0x2a1e08), 0);
            lv_obj_set_style_text_color(s_wxPill, lv_color_hex(w.alert >= 2 ? 0xff6a4a : 0xffc86a), 0);
            lv_obj_clear_flag(s_wxPill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wxPill, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
```

Add `#include <math.h>` to ui.cpp if not present (for `lroundf`).

- [ ] **Step 4: Extend `ui_show_view` to allow view 3**

Change line ~462:
```cpp
void ui_show_view(int idx) {
    if (s_tv && idx >= 0 && idx <= 3) lv_obj_set_tile_id(s_tv, (uint32_t)idx, 0, LV_ANIM_OFF);
}
```

- [ ] **Step 5: Compile both envs**

Run: `pio run -e esp32-s3-amoled-175 && pio run -e native`
Expected: both SUCCESS. (The sim compiles the tile + reads `weather::state()` — it'll show "Waiting for data" since the sim never fetches, which is fine.)

- [ ] **Step 6: Commit**

```bash
git add src/ui.h src/ui.cpp
git commit -m "feat(weather): Weather tile + global HUD raincloud icon"
```

---

### Task WX-A5: Wire into `main.cpp` — poll, alert sound, config toggle

**Files:** Modify `src/main.cpp`

- [ ] **Step 1: Includes + settings + NVS load**

At the top includes, add `#include "weather.h"` and `#include "weather_client.h"`. Add globals near the other `g_*` settings:

```cpp
static bool g_wxEnabled = false;   // Weather tile + alerts (off by default — POC)
static uint8_t g_wxLastAlert = 0;  // for onset detection (no repeated sound)
```

In the settings-load block (where `g_showSweep` etc. are read from `Preferences`), add:
```cpp
    g_wxEnabled = p.getBool("wx", false);
```

- [ ] **Step 2: Poll Open-Meteo on core 0**

In the **networking task** (core 0, the block starting ~line 97 with `lastPoll`), add a separate ~5-min cadence next to the ADS-B poll. Add a static near `lastPoll`:
```cpp
    uint32_t lastWx = 0;
```
and inside the task loop, after the ADS-B fetch block, add:
```cpp
        if (g_wxEnabled && (lastWx == 0 || millis() - lastWx >= 300000)) {   // every 5 min
            lastWx = millis();
            weather_client_poll(g_settings.homeLat, g_settings.homeLon);
        }
```
(If `g_settings.homeLat/lon` isn't visible in the task, use the same source the ADS-B fetch uses for the centre.)

- [ ] **Step 3: Push state to the UI + fire the onset alert (core 1 loop)**

In the periodic UI-update block where `ui_set_gps(...)` is called (~line 1169), add:
```cpp
        if (g_wxEnabled) {
            const weather::State &w = weather::state();
            ui_set_weather(w.alert, w.summary);
            if (w.valid && w.alert > g_wxLastAlert && w.alert >= weather::WX_RAIN) {
                audio_play(w.alert >= weather::WX_STORM ? AUDIO_EMERGENCY : AUDIO_INBOUND);  // onset only
            }
            g_wxLastAlert = w.alert;
        } else {
            ui_set_weather(0, "");   // keep icon hidden when disabled
        }
```
(Reuses existing cues for the POC; a dedicated weather cue can come later. Respects mute via `audio_play`'s existing gating if present — if `audio_play` is not mute-gated, wrap in the same `!g_muted && g_alertMode` check `checkAudioEvents` uses.)

- [ ] **Step 4: Config toggle — handler + route + NVS**

Add a handler near `handleSweep`:
```cpp
static void handleWeather() {
    if (g_web.hasArg("v")) {
        g_wxEnabled = g_web.arg("v").toInt() != 0;
        if (!g_wxEnabled) ui_set_weather(0, "");
        if (g_web.hasArg("save")) { Preferences p; p.begin("capsuleradar", false); p.putBool("wx", g_wxEnabled); p.end(); }
    }
    g_web.send(200, "text/plain", "ok");
}
```
Register it with the others (near `g_web.on("/sweep", handleSweep);`):
```cpp
    g_web.on("/weather", handleWeather);
```

- [ ] **Step 5: Config page — add the toggle to a card**

In the chunked config page (`handleRoot`), add to the **Traffic & Filters** card (or a new one-line Weather row) a checkbox wired to a `wx()` JS fetch, following the exact pattern of the existing `sw()`/airports checkboxes:
- HTML (inside the relevant card's `sendCard` inner string): `"<label><input type=checkbox class=ck %s onchange='wx(this.checked)'>Weather tile + storm alerts</label>"` with the arg `g_wxEnabled ? "checked" : ""`.
- JS (in the script chunk, next to `sw`): `"function wx(c){fetch('/weather?v='+(c?1:0)+'&save=1').then(toast);}"`.

- [ ] **Step 6: Compile firmware**

Run: `pio run -e esp32-s3-amoled-175`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat(weather): core-0 poll, onset alert sound, config toggle (off by default)"
```

---

### Task WX-A6: Version bump, flash, verify, ship

**Files:** Modify `src/config.h`

- [ ] **Step 1:** Bump `FW_VERSION` `"1.10.0"` → `"1.11.0-wxA"` in `src/config.h`.
- [ ] **Step 2:** `pio run -e esp32-s3-amoled-175 && pio run -e native` → both SUCCESS.
- [ ] **Step 3:** Flash (if the S3 is on USB): `pio run -e esp32-s3-amoled-175 -t upload --upload-port /dev/cu.usbmodem111201`. If the port is gone, skip and note it.
- [ ] **Step 4:** Commit + push. Redeploy the flasher only if flashing succeeded or Selma will flash remotely:
```bash
git add src/config.h && git commit -m "release: v1.11.0-wxA weather POC phase A (conditions + alert)"
git push && gh workflow run webflasher.yml
```
- [ ] **Step 5 (Selma, on-device):** enable Weather on `capsuleradar.local`, swipe to the Weather tile (4th view) → conditions appear within ~5 min; when a rain/storm condition exists, the HUD raincloud shows on the radar screen + the alert sound fires once on onset; confirm serial `[wx]` logs and healthy free heap/PSRAM, and the radar itself is unaffected.

---

## Self-Review

- **Spec coverage (Phase A scope):** Weather tile as 4th view (WX-A4) ✓; conditions + summary + pill (WX-A4) ✓; Open-Meteo client + WMO alert logic (WX-A2) ✓; **both** alert surfaces — tile pill + global HUD icon (WX-A3/A4) ✓; onset alert sound (WX-A5) ✓; config toggle, off by default (WX-A5) ✓; portable-state / device-fetch split so the sim compiles (WX-A1) ✓; core-0 fetch off the render path (WX-A5) ✓. **Deferred to Phase B (own plan):** PNG decoder, RainViewer tiles, the animated precip loop, the scrubber, home-centered composite.
- **Placeholder scan:** the two "follow the existing pattern" steps (WX-A5 Step 5 config HTML/JS) reference exact existing analogues (`sw()`/airports) with the exact strings to add — not vague. All new files/functions shown in full.
- **Type consistency:** `weather::State`/`weather::set`/`weather::state` used identically across A1/A2/A4/A5; `ui_set_weather(int,const char*)` matches decl (A3) + calls (A5); `ui_weather_tile_refresh` defined in A4 before its call in A3's `ui_set_weather` (same file; ensure A4's function appears above `ui_set_weather` or is forward-declared — add `static void ui_weather_tile_refresh(const char*);` near the top statics); `handleWeather`/`/weather`/`wx()` names align; `AUDIO_EMERGENCY`/`AUDIO_INBOUND` are existing cues.
- **Ordering fix applied:** add a forward declaration `static void ui_weather_tile_refresh(const char *summary);` with the ui.cpp statics so WX-A3's `ui_set_weather` compiles regardless of definition order.

## Review
Sonnet implements; Opus reviews the **core-0/core-1 shared-state safety** (weather::set mutex vs the UI read), the **alert onset logic** (fires once per transition, respects mute/alertMode), and that the **off-by-default path** adds zero cost/'`ui_set_weather(0,"")` correctly hides the icon when disabled.
