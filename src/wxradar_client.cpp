// RainViewer radar tile fetch + line-by-line PNG decode into wxradar's frame cache.
// Device-only (network + PNGdec). All I/O runs on core 0 (adsb_task).
//
// Tile URL sends a real Content-Length (verified live) -> NOT chunked -> getStream()+
// readBytes is safe here. Contrast weather_client.cpp: Open-Meteo IS chunked and must
// use getString() instead. Do not "unify" the two.
#include "wxradar_client.h"
#include "wxradar.h"
#include "wxtile.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
// lvgl.h MUST come before PNGdec.h: PNGdec's zutil.h #defines `local` to `static`,
// which collides with a parameter named `local` in LVGL's lv_meter.h.
#include <lvgl.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <cmath>

static char s_status[80] = "never ran";
const char *wxradar_last_status() { return s_status; }

static const int Z   = 6;                            // ~550 km across
static const int WIN = wxradar::FRAME_PX * 2;        // 768-px source window -> /2 -> 384 cached

// --- per-decode context (PNGdec callbacks are C-style, so these are file-static) ---
static uint16_t *s_dst   = nullptr;   // target frame (384x384 RGB565)
static int       s_offX  = 0, s_offY = 0;   // this tile's top-left within the WIN window
static PNG       s_png;

// PNG_DRAW_CALLBACK is `int (*)(PNGDRAW*)` (installed PNGdec.h); return value is unused
// by decode() but must be present to match the signature.
static int pngDraw(PNGDRAW *d) {
    // One source line -> downscale by 2 (keep even lines/cols) -> RGB565 into the frame.
    const int sy = d->y + s_offY;
    if (sy < 0 || sy >= WIN || (sy & 1)) return 1;    // odd lines dropped by the /2 downscale
    const int dy = sy >> 1;
    if (dy < 0 || dy >= wxradar::FRAME_PX) return 1;
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
    return 1;
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
