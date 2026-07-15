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
