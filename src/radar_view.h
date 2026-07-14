#pragma once
// Scope rendering API (M1 scope, M2 aircraft, M3 selection). See docs/ARCHITECTURE.md.
// Visual reference: assets/plane_radar_2.0_mockup.html
#include <vector>
#include <lvgl.h>
#include "aircraft.h"

struct RadarSettings {
    double homeLat, homeLon;
    float  rangeKm;
    double rotationDeg = 0.0;   // 0 = north-up
    bool   mute = false;
};

// Selectable visual skins.
enum RadarTheme {
    THEME_PHOSPHOR=0, THEME_ORB=1, THEME_AMBER=2, THEME_MILITARY=3,
    THEME_VICE=4, THEME_MIDNIGHT=5, THEME_SILENT=6, THEME_MISSION=7, THEME_CIC=8, THEME_CLAUDEIC=9,
    THEME_BORDERLANDS=10, THEME_ALIENS=11, THEME_MASSEFFECT=12, THEME_TOPGUN=13, THEME_FIREFOX=14,
    THEME_SABER=15, THEME_LCARS=16, THEME_BROWNCOAT=17,
    THEME_COUNT=18
};

// Flattened, display-ready info for one aircraft (detail card / list view).
struct AcInfo {
    char  hex[8];
    char  call[12];
    char  type[8];
    float altFt;
    bool  onGround;
    float vsFpm;        // NaN if unknown
    float gsKt;         // NaN if unknown
    float distKm;
    float bearingDeg;
    int   squawk;       // -1 if unknown
    bool  emergency;
};

namespace radar {

// Build the radar scope (rings, crosshair, rose, sweep, center) under `parent`.
void init(void* lv_parent);                 // pass lv_obj_t*

// Rebuild the aircraft layer from the latest snapshot. Call at poll cadence.
void update(const std::vector<Aircraft>& aircraft, const RadarSettings& s);

// Nearest aircraft to (x,y) within a tap radius -> snapshot index, or -1.
int  hitTest(int x, int y);

// Selection (tracked by hex so it survives data updates). idx < 0 clears.
void select(int idx);
bool selected(AcInfo& out);                 // false if nothing selected/visible

// Snapshot access for the list / stats views.
int  count();
int  countInRange();                        // aircraft within the display range (for the HUD)
bool info(int idx, AcInfo& out);

// Sweep self-animates via an internal timer; kept for API compatibility.
void tickSweep();

// Selectable visual skin (THEME_PHOSPHOR / THEME_ORB).
void setTheme(int theme);
int  theme();
void cycleTheme();
void cycleThemeBack();
lv_color_t chromeColor();                        // active theme's primary/rings color (chrome tint)
void setThemeChangedCb(void (*cb)(int theme));   // called when the theme changes (for persistence)
void setRangeLabelVisible(bool v);               // hide the built-in range label (UI shows its own)
void setSweepEnabled(bool on);                   // show/hide the rotating sweep line
bool sweepEnabled();
void setAirportsEnabled(bool on);                // show/hide airport markers on the scope
bool airportsEnabled();
void setTrailLength(int level);                  // 0=off 1=short 2=medium 3=long (aircraft trails + flow)
void setMaxOnScreen(int n);                       // how many (nearest) aircraft to draw on the scope
void flashRefresh();                              // brief on-screen confirmation for shake-to-refresh
void flashAlert();                                // full-screen red flash for alert conditions

// Drop all accumulated trails + the persistent flow layer and force a clean repaint.
// Call after a display::setRotation(): the flow canvas is a persistent bitmap that
// isn't rebuilt on a rotation, so without this the pre-rotation tracks linger as
// scattered "stale" tails until the next reproject (zoom) wipes them.
void resetTrails();

} // namespace radar
