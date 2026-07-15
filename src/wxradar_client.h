#pragma once
// Device-only: fetch RainViewer frames into wxradar::frames().
//
// wxradar_step() is a state machine, NOT a one-shot poll: it does at most one unit of
// network work per call (either refresh the frame list, or fetch one frame's tiles) and
// returns quickly. Call it every adsb_task cycle when weather is enabled — it self-throttles
// (frame list refreshes at most every ~5 min or on a material lat/lon change; each call
// advances at most one frame) so it never turns into a 200+ sequential-TLS-handshake stall
// that could trip the >180s feed watchdog in main.cpp. Do NOT gate calls behind a 5-min
// timer yourself; that would defeat the one-frame-per-cycle throttling.
void wxradar_step(double lat, double lon);
const char *wxradar_last_status();                // diagnostic, served at /wx
