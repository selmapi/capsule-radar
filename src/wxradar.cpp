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
