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
