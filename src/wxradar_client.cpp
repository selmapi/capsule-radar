// RainViewer radar tile fetch + line-by-line PNG decode into wxradar's frame cache.
// Device-only (network + PNGdec). All I/O runs on core 0 (adsb_task).
//
// Tile URL sends a real Content-Length (verified live) -> NOT chunked -> getStream()+
// readBytes is safe here. Contrast weather_client.cpp: Open-Meteo IS chunked and must
// use getString() instead. Do not "unify" the two.
//
// wxradar_step() is a state machine (see wxradar_client.h): each call does AT MOST one
// unit of network work — refresh the frame list, OR fetch one frame's tiles — so a full
// 13-frame refresh is spread across ~14 adsb_task cycles instead of blocking one cycle for
// ~208 sequential TLS handshakes (Opus review S1: that stall was long enough to trip
// main.cpp's >180s feed watchdog and reboot the board, on top of starving the primary
// aircraft feed). Within one frame, all tiles share a single HTTPClient + WiFiClientSecure
// with setReuse(true) so they share one TLS session instead of opening 16 (S1a).
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
#include <new>

static char s_status[80] = "never ran";
const char *wxradar_last_status() { return s_status; }

static const int Z   = 6;                            // ~550 km across
static const int WIN = wxradar::FRAME_PX * 2;        // 768-px source window -> /2 -> 384 cached

// --- PNG decoder object (S2: PSRAM, not internal DRAM) ---
// PNGdec's PNGIMAGE embeds ucZLIB[32768 + sizeof(inflate_state)] (~45 KB, confirmed against
// the installed PNGdec.h) directly in the PNG object. A `static PNG` puts that in .bss ->
// internal DRAM, which is exactly the contiguous-internal-RAM budget the TLS handshake for
// the PRIMARY aircraft feed needs (see display.cpp:147's rotation-buffer comment and
// photo_client.cpp:108's guard). Allocate it once in PSRAM instead and use it by pointer.
static PNG *s_png = nullptr;
static bool ensurePng() {
    if (s_png) return true;
    void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM);
    if (!mem) return false;
    s_png = new (mem) PNG();   // placement-new: mem is raw PSRAM, not a real PNG yet
    return true;
}

// --- per-decode context (PNGdec callbacks are C-style, so these are file-static) ---
static uint16_t *s_dst   = nullptr;   // target frame (384x384 RGB565)
static int       s_offX  = 0, s_offY = 0;   // this tile's top-left within the WIN window

// PNG_DRAW_CALLBACK is `int (*)(PNGDRAW*)` (installed PNGdec.h); return value is unused
// by decode() but must be present to match the signature.
static int pngDraw(PNGDRAW *d) {
    if (!s_png || !s_dst) return 1;
    if (d->iWidth > 256) return 1;    // guard: `line` below is sized for 256px source tiles
    // One source line -> downscale by 2 (keep even lines/cols) -> RGB565 into the frame.
    const int sy = d->y + s_offY;
    if (sy < 0 || sy >= WIN || (sy & 1)) return 1;    // odd lines dropped by the /2 downscale
    const int dy = sy >> 1;
    if (dy < 0 || dy >= wxradar::FRAME_PX) return 1;
    static uint16_t line[256];
    // S4: LVGL (LV_COLOR_16_SWAP 0 in lv_conf.h) wants native little-endian RGB565 — the
    // panel's big-endian need is handled at flush time in display.cpp, not here (the
    // sibling JPEG path in photo_client.cpp also decodes with setSwapBytes(false)).
    s_png->getLineAsRGB565(d, line, PNG_RGB565_LITTLE_ENDIAN, 0x0000);
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

// Fetch one tile and decode it into s_dst at (s_offX,s_offY). `http`/`client` are supplied
// by the caller so multiple tiles of the same frame can share one TLS session (S1a).
static bool fetchTile(HTTPClient &http, WiFiClientSecure &client, const String &url,
                       uint16_t *dst, int offX, int offY) {
    if (!http.begin(client, url)) return false;
    http.setConnectTimeout(6000);
    if (http.GET() != 200) { http.end(); return false; }
    const int len = http.getSize();                  // Content-Length IS present (verified)
    if (len <= 0 || len > 64000) { http.end(); return false; }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { http.end(); return false; }
    const int got = http.getStream().readBytes(buf, len);   // safe: not chunked
    // end() with http.setReuse(true) (set once by the caller) keeps the TCP/TLS session
    // open for the next begin()+GET() IF the server answered HTTP/1.1 without
    // "Connection: close" (HTTPClient tracks that as _canReuse internally). Whether that
    // actually elides the handshake is logged below so it's visible in the serial log.
    http.end();
    bool ok = false;
    if (got == len) {
        s_dst = dst; s_offX = offX; s_offY = offY;
        if (s_png->openRAM(buf, len, pngDraw) == PNG_SUCCESS) {
            ok = (s_png->decode(nullptr, 0) == PNG_SUCCESS);
            s_png->close();
        }
    }
    free(buf);
    return ok;
}

// ---------------------------------------------------------------------------------------
// Frame-list state: the RainViewer frame index (host + per-frame path/time), refreshed at
// most every ~5 min or when lat/lon has moved materially. Kept newest-first (entries[0] is
// the newest frame) so the per-frame cursor below can process newest-to-oldest.
// ---------------------------------------------------------------------------------------
namespace {
struct FrameList {
    String   host;
    struct Entry { uint32_t time = 0; char path[40] = ""; };
    Entry    entries[wxradar::FRAMES];
    int      n = 0;              // valid entries in `entries` (<= wxradar::FRAMES)
    uint32_t fetchedAtMs = 0;
    double   lat = 0, lon = 0;   // location this list was fetched for
    bool     valid = false;
};
FrameList s_list;
int       s_k = 0;               // next newest-first entry to process (>= s_list.n = idle)

// ~0.1 deg (~11 km at mid-latitudes) is well inside the ~550 km window at z=6 — this just
// catches "home actually moved" (new location saved), not GPS jitter.
const double MOVE_THRESHOLD_DEG = 0.1;
const uint32_t LIST_MAX_AGE_MS  = 300000UL;   // ~5 min

bool listStale(double lat, double lon) {
    if (!s_list.valid) return true;
    if (millis() - s_list.fetchedAtMs > LIST_MAX_AGE_MS) return true;
    if (fabs(lat - s_list.lat) > MOVE_THRESHOLD_DEG || fabs(lon - s_list.lon) > MOVE_THRESHOLD_DEG) return true;
    return false;
}

// One network round-trip: fetch + parse the frame list only. Does NOT fetch any tiles.
bool refreshList(double lat, double lon) {
    WiFiClientSecure c; c.setInsecure();
    HTTPClient http;
    if (!http.begin(c, "https://api.rainviewer.com/public/weather-maps.json")) {
        snprintf(s_status, sizeof(s_status), "maps begin failed"); return false;
    }
    http.setConnectTimeout(6000);
    if (http.GET() != 200) { snprintf(s_status, sizeof(s_status), "maps HTTP fail"); http.end(); return false; }
    const String body = http.getString();   // small; de-chunks if the server ever chunks it
    http.end();
    JsonDocument filter; filter["host"] = true; filter["radar"]["past"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        snprintf(s_status, sizeof(s_status), "maps json err"); return false;
    }
    const char *host = doc["host"] | "";
    JsonArray past = doc["radar"]["past"];
    if (!host[0] || past.isNull() || past.size() == 0) {
        snprintf(s_status, sizeof(s_status), "maps empty"); return false;
    }

    const int total = (int)past.size();
    const int n = total < wxradar::FRAMES ? total : wxradar::FRAMES;
    // S6: `past` is chronological (oldest first); if RainViewer ever returns MORE than
    // FRAMES entries, offset by (total - n) so we keep the NEWEST n, not the oldest n.
    const int base = total - n;

    s_list.host = host;
    s_list.n    = n;
    for (int k = 0; k < n; ++k) {                     // k=0 -> newest
        const int pIdx = base + (n - 1 - k);
        JsonObject fr = past[pIdx];
        s_list.entries[k].time = fr["time"] | 0;
        snprintf(s_list.entries[k].path, sizeof(s_list.entries[k].path), "%s",
                 (const char *)(fr["path"] | ""));
    }
    s_list.fetchedAtMs = millis();
    s_list.lat = lat; s_list.lon = lon;
    s_list.valid = true;
    s_k = 0;                                          // restart the per-frame cursor at "newest"
    snprintf(s_status, sizeof(s_status), "list %d/%d frames", n, total);
    return true;
}

// Fetch tiles for exactly one frame (the one at cursor s_k) and advance the cursor.
void processOneFrame(double lat, double lon) {
    if (s_k >= s_list.n) return;                      // cycle complete; idle until list goes stale
    const int k = s_k++;
    const FrameList::Entry &e = s_list.entries[k];
    if (!e.path[0]) return;

    // Slot mapping: k=0 (newest) -> slot n-1, k=n-1 (oldest kept) -> slot 0, so slot index
    // stays chronological (0=oldest..n-1=newest) — that's what ui.cpp's animator timer
    // expects when it walks px[] forward and wraps.
    const int slot = s_list.n - 1 - k;
    uint16_t *dst = wxradar::alloc_frame(slot);
    if (!dst) { snprintf(s_status, sizeof(s_status), "frame %d OOM", slot); return; }
    memset(dst, 0, (size_t)wxradar::FRAME_PX * wxradar::FRAME_PX * 2);

    const wxtile::Window w = wxtile::window(lat, lon, Z, WIN);

    // S1a: one WiFiClientSecure + HTTPClient for every tile of this frame (same host) ->
    // ~1 TLS handshake for the whole frame instead of one per tile.
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setReuse(true);

    bool any = false;
    const uint32_t frameT0 = millis();
    for (int ty = 0; ty < w.nty; ++ty) {
        for (int tx = 0; tx < w.ntx; ++tx) {
            char url[220];
            snprintf(url, sizeof(url), "%s%s/256/%d/%d/%d/4/1_1.png",
                     s_list.host.c_str(), e.path, Z, w.tx0 + tx, w.ty0 + ty);
            const int offX = (int)lround((w.tx0 + tx) * 256.0 - w.x0);
            const int offY = (int)lround((w.ty0 + ty) * 256.0 - w.y0);
            const uint32_t tileT0 = millis();
            const bool tileOk = fetchTile(http, client, String(url), dst, offX, offY);
            if (tileOk) any = true;
            // Instrumentation for S1a verification: the first tile pays the TLS handshake;
            // if reuse is working, tiles after it should be noticeably faster and
            // http.connected() should read true going into the next begin(). If reuse
            // silently fails (server closes the connection, or ESP32 HTTPClient/BearSSL
            // doesn't actually keep the session open across begin() calls), every tile will
            // show roughly the same latency as tile 0 -- watch for that in the serial log.
            Serial.printf("[wxradar] slot%d tile(%d,%d) %s %ums connected=%d\n",
                          slot, tx, ty, tileOk ? "ok" : "fail",
                          (unsigned)(millis() - tileT0), (int)http.connected());
        }
    }
    client.stop();   // release this frame's session; next frame (next call) opens its own

    wxradar::Frames &F = wxradar::frames();
    if (any) {
        F.time[slot] = e.time;
        if (k == 0) { F.ready = true; F.play = slot; }   // newest frame loaded -> show it now
    }
    // S5: recompute count as each frame completes (not just once at the very end of a full
    // refresh) so ui.cpp's `F.count > 0` gate — and therefore the animation — starts as soon
    // as the first frame is in, instead of waiting minutes for all 13.
    int loaded = 0;
    for (int i = 0; i < wxradar::FRAMES; ++i) if (F.px[i]) ++loaded;
    F.count = loaded;

    snprintf(s_status, sizeof(s_status), "radar %d/%d frames z%d (last frame %ums)",
             loaded, s_list.n, Z, (unsigned)(millis() - frameT0));
    vTaskDelay(pdMS_TO_TICKS(10));   // yield to other core-0 work before returning
}
}  // namespace

void wxradar_step(double lat, double lon) {
    // S3: TLS memory guard, same pattern as photo_client.cpp:108 — a tile (or the frame
    // list) fetch needs a TLS handshake; skip gracefully on a tight-memory moment instead
    // of failing the handshake (or worse, fragmenting further).
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 28000) {
        snprintf(s_status, sizeof(s_status), "low memory, skipping");
        return;
    }
    if (!ensurePng()) {
        snprintf(s_status, sizeof(s_status), "PNG alloc failed (PSRAM OOM)");
        return;
    }
    if (listStale(lat, lon)) {
        refreshList(lat, lon);        // one network round-trip; frame fetches resume next call
        return;                       // S1b: at most one unit of network work per call
    }
    processOneFrame(lat, lon);        // fetches exactly one frame's tiles, then returns
}
