# Weather POC — Phase B (animated 2-hour precipitation loop) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Add the animated 2-hour precipitation loop to the Weather tile: 13 RainViewer radar frames, home-centered, drawn over the theme-tinted coastline, auto-playing with a −2h→now scrubber.

**Architecture:** A device-only tile client (`wxradar_client.*`) fetches RainViewer's frame list, then for each frame fetches the 2×2 block of 256-px PNG tiles covering a home-centered window at zoom 6, decodes them **line-by-line** with PNGdec (never holding a full RGBA image) and blit-downscales directly into a per-frame **384×384 RGB565** buffer in PSRAM. A portable `wxradar.*` holds the frame cache + playhead (compiled in both firmware and sim, like `weather.*`). `ui.cpp` draws the active frame on the Weather tile under the existing coastline and animates via an LVGL timer.

**Tech Stack:** C++17, Arduino, LVGL v8, **bitbank2/PNGdec** (new lib_dep), PlatformIO. Verified by firmware + native compile, host-side tile-math test, and on-device via `/wx`.

---

## RECON (already verified — do not re-litigate)

- Frame list: `GET https://api.rainviewer.com/public/weather-maps.json` → `host` + `radar.past[]` = **13 frames**, each `{time, path}`, spanning exactly **2 h** (10-min steps).
- Tile URL: `{host}{path}/256/{z}/{x}/{y}/{color}/{smooth}_{snow}.png` — use `color=4`, `smooth=1`, `snow=1`.
- **Verified live:** `HTTP/2 200`, `content-type: image/png`, **`content-length: 2980`** (≈3 KB/tile), `PNG 256x256 8-bit RGBA non-interlaced`.
- **`Content-Length` IS present ⇒ NOT chunked ⇒ `http.getStream()` is SAFE here.** (Contrast: Open-Meteo is chunked and required `getString()` — see `weather_client.cpp`'s comment. Do not "fix" this one to getString; streaming binary is what we want.)
- Total download per full refresh ≈ 13 frames × 4 tiles × 3 KB ≈ **160 KB**.

---

### Task WX-B1: Add PNGdec + a host-side tile-math test

**Files:** Modify `platformio.ini`; Create `src/wxtile.h`

- [ ] **Step 1: Add the decoder dependency**

In `platformio.ini` `lib_deps` (after `bodmer/TJpg_Decoder`), add:
```
    bitbank2/PNGdec                                   ; PNG decode for RainViewer radar tiles
```

- [ ] **Step 2: Create the pure tile-math header (portable, testable on host)**

`src/wxtile.h` — Web-Mercator math, no Arduino deps so it compiles in the sim and a host test:

```cpp
#pragma once
#include <cmath>
#include <cstdint>

namespace wxtile {

// Web-Mercator pixel coords at zoom z (256-px tiles).
inline double lonToPx(double lon, int z) { return (lon + 180.0) / 360.0 * 256.0 * (1 << z); }
inline double latToPx(double lat, int z) {
    const double s = std::sin(lat * M_PI / 180.0);
    return (0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * 256.0 * (1 << z);
}

// The home-centered window: WIN x WIN px at zoom z, centered on (lat,lon).
// Returns the top-left pixel of the window and the tile range covering it.
struct Window {
    double px, py;        // home's pixel position at zoom z
    double x0, y0;        // window top-left in pixel space
    int    tx0, ty0;      // first tile index covering the window
    int    ntx, nty;      // tile count in each axis
};
inline Window window(double lat, double lon, int z, int win) {
    Window w;
    w.px = lonToPx(lon, z); w.py = latToPx(lat, z);
    w.x0 = w.px - win / 2.0; w.y0 = w.py - win / 2.0;
    w.tx0 = (int)std::floor(w.x0 / 256.0);
    w.ty0 = (int)std::floor(w.y0 / 256.0);
    const int tx1 = (int)std::floor((w.x0 + win - 1) / 256.0);
    const int ty1 = (int)std::floor((w.y0 + win - 1) / 256.0);
    w.ntx = tx1 - w.tx0 + 1; w.nty = ty1 - w.ty0 + 1;
    return w;
}

}  // namespace wxtile
```

- [ ] **Step 3: Host test the math against a known value**

Create a throwaway host test (do NOT commit it) and run it:
```bash
cat > /tmp/tt.cpp <<'EOF'
#include "wxtile.h"
#include <cstdio>
int main(){
  // Kernersville NC, z=6, 512-px window
  auto w = wxtile::window(36.05777, -80.32965, 6, 512);
  printf("px=%.1f py=%.1f tx0=%d ty0=%d ntx=%d nty=%d\n", w.px, w.py, w.tx0, w.ty0, w.ntx, w.nty);
  return 0;
}
EOF
g++ -std=c++17 -I src /tmp/tt.cpp -o /tmp/tt && /tmp/tt
```
Expected: `tx0`/`ty0` around **17/25** (the tile we already fetched successfully during recon), `ntx`/`nty` = **2 or 3** (a 512 window spans 2–3 tiles depending on where home falls). Sanity: `px/256` must be within `[tx0, tx0+ntx)`. If ntx/nty is 3, that's expected and fine — the fetch loop below handles any count.

- [ ] **Step 4: Compile both envs (PNGdec must resolve)**

Run: `pio run -e esp32-s3-amoled-175 && pio run -e native`
Expected: both SUCCESS (PNGdec downloads for the device env; `wxtile.h` is header-only).

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/wxtile.h
git commit -m "feat(wxradar): PNGdec dep + portable Web-Mercator tile math"
```

---

### Task WX-B2: Portable frame cache (`wxradar.h` / `wxradar.cpp`)

**Files:** Create `src/wxradar.h`, `src/wxradar.cpp`; Modify `platformio.ini:65` (native filter)

- [ ] **Step 1: `src/wxradar.h`**

```cpp
#pragma once
#include <cstdint>

namespace wxradar {

static const int FRAMES = 13;      // RainViewer free = 13 past frames (2 h)
static const int FRAME_PX = 384;   // cached frame size (displayed scaled to the tile)

struct Frames {
    uint16_t *px[FRAMES] = {nullptr};  // RGB565 FRAME_PX*FRAME_PX, PSRAM; null = not loaded
    uint32_t  time[FRAMES] = {0};      // unix time of each frame
    int       count = 0;               // how many are loaded
    int       play = 0;                // playhead index
    bool      ready = false;           // at least the newest frame is loaded
};

Frames &frames();                   // shared (single writer: core 0; reader: core 1)
uint16_t *alloc_frame(int i);       // PSRAM alloc for slot i (idempotent); null on OOM
void      reset();                  // free all frames (e.g. home moved)

}  // namespace wxradar
```

- [ ] **Step 2: `src/wxradar.cpp`**

```cpp
#include "wxradar.h"
#include <cstdlib>
#include <cstring>
#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace wxradar {

static Frames s_f;

Frames &frames() { return s_f; }

uint16_t *alloc_frame(int i) {
    if (i < 0 || i >= FRAMES) return nullptr;
    if (s_f.px[i]) return s_f.px[i];
    const size_t bytes = (size_t)FRAME_PX * FRAME_PX * sizeof(uint16_t);
#if defined(ARDUINO)
    s_f.px[i] = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    s_f.px[i] = (uint16_t *)malloc(bytes);
#endif
    if (s_f.px[i]) memset(s_f.px[i], 0, bytes);
    return s_f.px[i];
}

void reset() {
    for (int i = 0; i < FRAMES; ++i) { if (s_f.px[i]) { free(s_f.px[i]); s_f.px[i] = nullptr; } }
    s_f.count = 0; s_f.play = 0; s_f.ready = false;
}

}  // namespace wxradar
```

- [ ] **Step 3: Native filter**

In `platformio.ini` line 65, append `+<wxradar.cpp>` (the sim links the cache; it just never fills it).

- [ ] **Step 4: Compile both**

Run: `pio run -e esp32-s3-amoled-175 && pio run -e native`
Expected: both SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/wxradar.h src/wxradar.cpp platformio.ini
git commit -m "feat(wxradar): portable PSRAM frame cache"
```

---

### Task WX-B3: Tile client — fetch + line-by-line decode + downscale (`wxradar_client.*`)

Device-only (network + PNGdec). NOT added to the native filter.

**Files:** Create `src/wxradar_client.h`, `src/wxradar_client.cpp`

- [ ] **Step 1: `src/wxradar_client.h`**

```cpp
#pragma once
// Device-only: fetch RainViewer frames into wxradar::frames().
void wxradar_poll(double lat, double lon);        // newest-first, backfills older; call from core-0
const char *wxradar_last_status();                // diagnostic, served at /wx
```

- [ ] **Step 2: `src/wxradar_client.cpp`**

Key points: window is `FRAME_PX*2 = 768` px at z=6 then /2 downscale → each cached frame covers a 768-px window (≈ 3 tiles across). PNGdec's `draw` callback gives one decoded line at a time (`PNGDRAW *`), which we blit-downscale straight into the frame — never a full RGBA image.

```cpp
#include "wxradar_client.h"
#include "wxradar.h"
#include "wxtile.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PNGdec.h>
#include <lvgl.h>

static char s_status[80] = "never ran";
const char *wxradar_last_status() { return s_status; }

static const int Z   = 6;                            // ~550 km across
static const int WIN = wxradar::FRAME_PX * 2;        // 768-px source window -> /2 -> 384 cached

// --- per-decode context (PNGdec callbacks are C-style, so these are file-static) ---
static uint16_t *s_dst   = nullptr;   // target frame (384x384 RGB565)
static int       s_offX  = 0, s_offY = 0;   // this tile's top-left within the WIN window
static PNG       s_png;

static void pngDraw(PNGDRAW *d) {
    // One source line -> downscale by 2 (keep even lines/cols) -> RGB565 into the frame.
    const int sy = d->y + s_offY;
    if (sy < 0 || sy >= WIN || (sy & 1)) return;      // odd lines dropped by the /2 downscale
    const int dy = sy >> 1;
    if (dy < 0 || dy >= wxradar::FRAME_PX) return;
    static uint16_t line[256];
    s_png.getLineAsRGB565(d, line, PNG_RGB565_BIG_ENDIAN, 0x0000);
    for (int x = 0; x < d->iWidth; ++x) {
        const int sx = x + s_offX;
        if (sx < 0 || sx >= WIN || (sx & 1)) continue;
        const int dx = sx >> 1;
        if (dx < 0 || dx >= wxradar::FRAME_PX) continue;
        const uint16_t c = line[x];
        if (c) s_dst[dy * wxradar::FRAME_PX + dx] = c;   // 0 = transparent -> leave the map showing
    }
}

// Fetch one tile and decode it into s_dst at (s_offX,s_offY).
static bool fetchTile(const String &url, uint16_t *dst, int offX, int offY) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.setConnectTimeout(6000);
    if (http.GET() != 200) { http.end(); return false; }
    const int len = http.getSize();                  // Content-Length IS present (verified)
    if (len <= 0 || len > 64000) { http.end(); return false; }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { http.end(); return false; }
    const int got = http.getStream().readBytes(buf, len);   // safe: not chunked
    http.end();
    bool ok = false;
    if (got == len) {
        s_dst = dst; s_offX = offX; s_offY = offY;
        if (s_png.openRAM(buf, len, pngDraw) == PNG_SUCCESS) {
            ok = (s_png.decode(nullptr, 0) == PNG_SUCCESS);
            s_png.close();
        }
    }
    free(buf);
    return ok;
}

void wxradar_poll(double lat, double lon) {
    // 1) frame list
    WiFiClientSecure c; c.setInsecure();
    HTTPClient http;
    if (!http.begin(c, "https://api.rainviewer.com/public/weather-maps.json")) {
        snprintf(s_status, sizeof(s_status), "maps begin failed"); return;
    }
    http.setConnectTimeout(6000);
    if (http.GET() != 200) { snprintf(s_status, sizeof(s_status), "maps HTTP fail"); http.end(); return; }
    const String body = http.getString();   // small; de-chunks if the server ever chunks it
    http.end();
    JsonDocument filter; filter["host"] = true; filter["radar"]["past"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        snprintf(s_status, sizeof(s_status), "maps json err"); return;
    }
    const char *host = doc["host"] | "";
    JsonArray past = doc["radar"]["past"];
    if (!host[0] || past.isNull() || past.size() == 0) {
        snprintf(s_status, sizeof(s_status), "maps empty"); return;
    }

    const wxtile::Window w = wxtile::window(lat, lon, Z, WIN);
    wxradar::Frames &F = wxradar::frames();
    const int n = (int)past.size() < wxradar::FRAMES ? (int)past.size() : wxradar::FRAMES;

    // 2) newest first, then backfill older (progressive: tile is useful immediately)
    int loaded = 0;
    for (int k = 0; k < n; ++k) {
        const int idx = n - 1 - k;                    // n-1 = newest
        JsonObject fr = past[idx];
        const uint32_t t = fr["time"] | 0;
        const char *path = fr["path"] | "";
        if (!path[0]) continue;
        uint16_t *dst = wxradar::alloc_frame(idx);
        if (!dst) { snprintf(s_status, sizeof(s_status), "frame %d OOM", idx); break; }
        memset(dst, 0, (size_t)wxradar::FRAME_PX * wxradar::FRAME_PX * 2);
        bool any = false;
        for (int ty = 0; ty < w.nty; ++ty) {
            for (int tx = 0; tx < w.ntx; ++tx) {
                char url[220];
                snprintf(url, sizeof(url), "%s%s/256/%d/%d/%d/4/1_1.png",
                         host, path, Z, w.tx0 + tx, w.ty0 + ty);
                const int offX = (int)lround((w.tx0 + tx) * 256.0 - w.x0);
                const int offY = (int)lround((w.ty0 + ty) * 256.0 - w.y0);
                if (fetchTile(String(url), dst, offX, offY)) any = true;
            }
        }
        if (any) { F.time[idx] = t; ++loaded; if (k == 0) { F.ready = true; F.play = idx; } }
        vTaskDelay(pdMS_TO_TICKS(20));                // stay polite / let other tasks run
    }
    F.count = loaded;
    snprintf(s_status, sizeof(s_status), "radar %d/%d frames z%d", loaded, n, Z);
}
```

- [ ] **Step 3: Compile firmware**

Run: `pio run -e esp32-s3-amoled-175`
Expected: SUCCESS. If PNGdec's API names differ in the installed version, check `.pio/libdeps/esp32-s3-amoled-175/PNGdec/src/PNGdec.h` for the exact signatures of `openRAM`, `decode`, `getLineAsRGB565`, and `PNGDRAW` — adapt, do not guess.

- [ ] **Step 4: Commit**

```bash
git add src/wxradar_client.h src/wxradar_client.cpp
git commit -m "feat(wxradar): RainViewer tile fetch + line-by-line PNG decode into frame cache"
```

---

### Task WX-B4: Draw + animate on the Weather tile

**Files:** Modify `src/ui.cpp`

- [ ] **Step 1: Add a canvas + timer statics**

Near the other Weather statics (`s_tileWx`…) add:
```cpp
static lv_obj_t  *s_wxImg = nullptr;      // radar frame image
static lv_timer_t *s_wxTimer = nullptr;   // loop animator
static lv_img_dsc_t s_wxDsc;              // descriptor pointing at the active frame
```

- [ ] **Step 2: Create the image + animation timer inside the `if (s_wxEnabled)` block in `ui_create()`**

Add after the existing Weather widgets are created (still inside the enabled branch), so the image sits BEHIND the text (create it first, then move it to the background):
```cpp
        s_wxImg = lv_img_create(s_tileWx);
        lv_obj_align(s_wxImg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(s_wxImg);
        lv_obj_add_flag(s_wxImg, LV_OBJ_FLAG_HIDDEN);
        s_wxTimer = lv_timer_create([](lv_timer_t *) {
            wxradar::Frames &F = wxradar::frames();
            if (!F.ready || F.count <= 0) return;
            int next = F.play + 1;                       // advance, skipping unloaded slots
            for (int i = 0; i < wxradar::FRAMES; ++i, ++next) {
                if (next >= wxradar::FRAMES) next = 0;
                if (F.px[next]) break;
            }
            F.play = next;
            ui_weather_frame_refresh();
        }, 320, nullptr);   // ~3 fps
```
Add `#include "wxradar.h"` at the top of ui.cpp.

- [ ] **Step 3: Add `ui_weather_frame_refresh()`**

Add next to `ui_weather_tile_refresh` (and a forward declaration `static void ui_weather_frame_refresh();` with the top statics):
```cpp
// Point the image at the active cached frame (no copy — the descriptor references PSRAM).
static void ui_weather_frame_refresh() {
    wxradar::Frames &F = wxradar::frames();
    if (!s_wxImg) return;
    uint16_t *p = (F.play >= 0 && F.play < wxradar::FRAMES) ? F.px[F.play] : nullptr;
    if (!p) { lv_obj_add_flag(s_wxImg, LV_OBJ_FLAG_HIDDEN); return; }
    s_wxDsc.header.cf   = LV_IMG_CF_TRUE_COLOR;
    s_wxDsc.header.w    = wxradar::FRAME_PX;
    s_wxDsc.header.h    = wxradar::FRAME_PX;
    s_wxDsc.data_size   = (uint32_t)wxradar::FRAME_PX * wxradar::FRAME_PX * 2;
    s_wxDsc.data        = (const uint8_t *)p;
    lv_img_set_src(s_wxImg, &s_wxDsc);
    lv_obj_clear_flag(s_wxImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_wxImg);
}
```

- [ ] **Step 4: Compile both**

Run: `pio run -e esp32-s3-amoled-175 && pio run -e native`
Expected: both SUCCESS (the sim links `wxradar.cpp`; frames stay null so the image stays hidden — the tile still shows conditions).

- [ ] **Step 5: Commit**

```bash
git add src/ui.cpp
git commit -m "feat(wxradar): draw + auto-animate the 2h loop on the Weather tile"
```

---

### Task WX-B5: Wire the poll + extend `/wx`; ship

**Files:** Modify `src/main.cpp`, `src/config.h`

- [ ] **Step 1: Poll on core 0**

In `main.cpp` add `#include "wxradar_client.h"` and, in the `adsb_task` weather block, refresh the radar frames on the same 5-min cadence right after the Open-Meteo poll:
```cpp
            if (g_wxEnabled && (lastWx == 0 || millis() - lastWx >= 300000)) {   // every 5 min
                lastWx = millis();
                weather_client_poll(g_settings.homeLat, g_settings.homeLon);
                wxradar_poll(g_settings.homeLat, g_settings.homeLon);
            }
```

- [ ] **Step 2: Report it at `/wx`**

In `handleWx()`, add `#include "wxradar.h"` + `#include "wxradar_client.h"` at the top of main.cpp, and extend the dump (append to the format string + args):
```cpp
        "radarStatus=%s\nradarFrames=%d\nradarReady=%d\nradarPlay=%d\n"
```
with args `wxradar_last_status(), wxradar::frames().count, (int)wxradar::frames().ready, wxradar::frames().play`.

- [ ] **Step 3: Bump + compile**

`src/config.h`: `FW_VERSION` → `"1.12.0-wxB"`. Run: `pio run -e esp32-s3-amoled-175 && pio run -e native` → both SUCCESS.

- [ ] **Step 4: Ship (OTA — USB-CDC on this board is wedged)**

```bash
git add src/main.cpp src/config.h
git commit -m "release: v1.12.0-wxB animated 2h precipitation loop"
curl -s -F "f=@.pio/build/esp32-s3-amoled-175/firmware.bin" http://192.168.1.106/update
git push && gh workflow run webflasher.yml
```

- [ ] **Step 5: Verify on-device**

Wait ~60 s (frame fetch takes longer than the Open-Meteo call), then:
```bash
curl -s http://192.168.1.106/wx
```
Expected: `radarStatus=radar 13/13 frames z6`, `radarFrames=13`, `radarReady=1`. Then Selma swipes to the Weather tile: precipitation animates over the coastline, centered on home. (If it's dry across the whole 550 km window, frames are legitimately near-empty — check `radarFrames` to distinguish "working but no rain" from "broken".)

---

## Self-Review

- **Spec coverage (Phase B scope):** 13-frame/2 h loop (B3) ✓; home-centered composite via 2×2+ tiles (B1 math + B3 fetch loop) ✓; PNG decode (B1 dep + B3) ✓; ~3.7 MB budget — 13 × 384² × 2 = **3.83 MB** in PSRAM (B2) ✓; progressive newest-first + backfill (B3) ✓; auto-play ~3 fps (B4) ✓; drawn over theme-tinted coastline, precip in RainViewer's own colors (B4 draws the frame behind the tile's text; the coastline/chrome remain the theme's) ✓; `/wx` diagnostics (B5) ✓.
- **Deferred (documented non-goals, unchanged):** the drag-to-scrub interaction and the −2h→now scrubber *widget* are NOT built here (auto-play only — the spec called scrubbing a nice-to-have); no forecast/nowcast frames; no zoom control.
- **Placeholder scan:** none — every step has real code. The one adaptive instruction (B3 Step 3, PNGdec API names) points at the exact header to read rather than guessing.
- **Type consistency:** `wxradar::FRAMES/FRAME_PX/Frames/frames()/alloc_frame/reset` consistent across B2/B3/B4; `wxtile::window` fields (`x0,y0,tx0,ty0,ntx,nty`) defined in B1 and used in B3; `wxradar_poll`/`wxradar_last_status` declared B3, used B5; `ui_weather_frame_refresh` forward-declared (B4 Step 3) before its use in the B4 Step 2 timer lambda.
- **Chunked-trap check (today's lesson):** RainViewer tiles send **Content-Length** (verified live) → `readBytes` on `getStream()` is correct. The frame-list JSON uses `getString()` anyway, so it's safe even if RainViewer ever chunks it.

## Review
Sonnet implements; Opus reviews **memory** (3.83 MB frame cache + per-tile PSRAM buffer vs the TLS handshake / ADS-B path — the contiguous-internal-RAM concern that shaped `photo_client`), the **PNGdec callback contract** (file-static context is not re-entrant — confirm no concurrent decode), **LVGL image-descriptor lifetime** (`s_wxDsc.data` points at PSRAM that `wxradar::reset()` could free while the image still references it), and the **core-0/core-1 race** on `frames()` (written during poll, read by the animation timer).
