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

// Invariant: each px[i] buffer is alloc-once (alloc_frame is idempotent — it returns the
// existing pointer if already allocated) and NEVER realloc'd; wxradar_client.cpp's
// wxradar_step() only ever writes *into* an already-allocated buffer. Only reset() may
// free them, and reset() must only be called from core 1 (LVGL/UI), because ui.cpp's
// s_wxDsc.data points directly at a px[] buffer with no copy — freeing one out from under
// an in-flight draw on core 1 (or a decode write on core 0) would be a use-after-free.
// As of this writing reset() has no callers; wire it up (e.g. "home moved") with that
// core-1-only rule in mind.
Frames &frames();                   // shared (single writer: core 0; reader: core 1)
uint16_t *alloc_frame(int i);       // PSRAM alloc for slot i (idempotent); null on OOM
void      reset();                  // free all frames (e.g. home moved) — core 1 only, see above

}  // namespace wxradar
