#include "weather_client.h"
#include "weather.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <math.h>

static bool isRain(int c)  { return (c>=51&&c<=55)||(c>=61&&c<=65)||(c>=80&&c<=82); }
static bool isStorm(int c) { return c==95||c==96||c==99; }

// Diagnostic: last fetch outcome, served at /wx. Serial is unreadable over this board's
// USB-CDC in practice, so this is how the weather path reports what happened.
static char s_status[72] = "never ran";
static uint32_t s_calls = 0;
const char *weather_client_last_status() { return s_status; }

void weather_client_poll(double lat, double lon) {
    ++s_calls;
    snprintf(s_status, sizeof(s_status), "call#%u started", (unsigned)s_calls);
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
    if (!http.begin(client, url)) {
        Serial.println("[wx] begin failed");
        snprintf(s_status, sizeof(s_status), "call#%u begin failed", (unsigned)s_calls);
        return;
    }
    http.setConnectTimeout(6000);
    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[wx] HTTP %d\n", code);
        snprintf(s_status, sizeof(s_status), "call#%u HTTP %d", (unsigned)s_calls, code);
        http.end();
        return;
    }

    // Open-Meteo replies with `Transfer-Encoding: chunked` and NO Content-Length.
    // HTTPClient::getStream() hands back the RAW socket — chunk-size markers and all —
    // and does NOT de-chunk. Feeding that to a *filtered* parse fails silently: ArduinoJson
    // skips the unrecognised chunk headers rather than erroring, returning Ok with `current`
    // missing (temp stuck at 0/--). getString() de-chunks properly. Body is <1 KB.
    // NOTE: adsb_client can use getStream() only because airplanes.live sends Content-Length.
    const String body = http.getString();
    http.end();
    if (body.isEmpty()) {
        snprintf(s_status, sizeof(s_status), "call#%u empty body", (unsigned)s_calls);
        return;
    }

    // Filter to the fields we use -> much smaller parsed document.
    StaticJsonDocument<256> filter;
    filter["current"] = true;
    JsonObject fm = filter.createNestedObject("minutely_15");
    fm["weather_code"] = true; fm["precipitation"] = true;
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[wx] json %s\n", err.c_str());
        snprintf(s_status, sizeof(s_status), "call#%u json err: %s", (unsigned)s_calls, err.c_str());
        return;
    }
    if (doc["current"].isNull()) {   // parsed but the object we need never arrived
        snprintf(s_status, sizeof(s_status), "call#%u parsed but current[] missing", (unsigned)s_calls);
        return;
    }

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
    snprintf(s_status, sizeof(s_status), "call#%u ok %.1fC code=%d", (unsigned)s_calls, s.tempC, s.code);
    Serial.printf("[wx] %.0fC code=%d alert=%d eta=%d\n", s.tempC, s.code, s.alert, s.etaMin);
}
