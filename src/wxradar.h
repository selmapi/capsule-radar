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
