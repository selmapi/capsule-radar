#pragma once
// Device-only: fetch Open-Meteo conditions/forecast into weather::state().
void weather_client_poll(double lat, double lon);   // blocking HTTPS fetch; call from core-0 task
