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
