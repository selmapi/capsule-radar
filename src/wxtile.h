#pragma once
#include <cmath>
#include <cstdint>

namespace wxtile {

// Web-Mercator pixel coords at zoom z (256-px tiles).
inline double lonToPx(double lon, int z) { return (lon + 180.0) / 360.0 * 256.0 * (1 << z); }
inline double latToPx(double lat, int z) {
    const double s = std::sin(lat * M_PI / 180.0);
    return (0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * 256.0 * (1 << z);
}

// The home-centered window: WIN x WIN px at zoom z, centered on (lat,lon).
// Returns the top-left pixel of the window and the tile range covering it.
struct Window {
    double px, py;        // home's pixel position at zoom z
    double x0, y0;        // window top-left in pixel space
    int    tx0, ty0;      // first tile index covering the window
    int    ntx, nty;      // tile count in each axis
};
inline Window window(double lat, double lon, int z, int win) {
    Window w;
    w.px = lonToPx(lon, z); w.py = latToPx(lat, z);
    w.x0 = w.px - win / 2.0; w.y0 = w.py - win / 2.0;
    w.tx0 = (int)std::floor(w.x0 / 256.0);
    w.ty0 = (int)std::floor(w.y0 / 256.0);
    const int tx1 = (int)std::floor((w.x0 + win - 1) / 256.0);
    const int ty1 = (int)std::floor((w.y0 + win - 1) / 256.0);
    w.ntx = tx1 - w.tx0 + 1; w.nty = ty1 - w.ty0 + 1;
    return w;
}

}  // namespace wxtile
