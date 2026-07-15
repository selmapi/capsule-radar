#pragma once
// Device-only: fetch RainViewer frames into wxradar::frames().
void wxradar_poll(double lat, double lon);        // newest-first, backfills older; call from core-0
const char *wxradar_last_status();                // diagnostic, served at /wx
