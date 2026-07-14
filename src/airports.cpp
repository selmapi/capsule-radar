#include "airports.h"
#include "airports_data.h"
#include "geo.h"
#include <vector>
#include <math.h>
#include <string.h>

struct Apt { lv_point_t pos; char iata[4]; uint8_t large; };
static std::vector<Apt> s_apts;

void airports_project(double homeLat, double homeLon, double rangeKm,
                      float cx, float cy, float rOuterPx) {
    s_apts.clear();
    if (rangeKm <= 0) return;

    const double rangeDeg  = rangeKm / 111.0;
    const double latMargin = rangeDeg * 1.05;
    const double cosLat    = cos(homeLat * M_PI / 180.0);
    const double lonMargin = latMargin / (cosLat < 0.15 ? 0.15 : cosLat);

    for (int i = 0; i < AIRPORT_NUM; ++i) {
        const double lat = AIRPORT_LAT[i] / (double)AIRPORT_SCALE;
        const double lon = AIRPORT_LON[i] / (double)AIRPORT_SCALE;
        const double dlon = lon - homeLon;
        if (fabs(lat - homeLat) > latMargin) continue;                      // cheap bbox reject
        if (fabs(dlon) > lonMargin && fabs(fabs(dlon) - 360.0) > lonMargin) continue;
        const double dist = geo::haversineKm(homeLat, homeLon, lat, lon);
        if (dist > rangeKm) continue;                                       // only inside the scope
        const double brg = geo::bearingDeg(homeLat, homeLon, lat, lon);
        const double rPx = (dist / rangeKm) * rOuterPx;
        const double a   = brg * M_PI / 180.0;
        Apt ap;
        ap.pos.x = (lv_coord_t)lroundf((float)(cx + rPx * sin(a)));
        ap.pos.y = (lv_coord_t)lroundf((float)(cy - rPx * cos(a)));
        memcpy(ap.iata, AIRPORT_IATA[i], 4);
        ap.large = AIRPORT_LARGE[i];
        s_apts.push_back(ap);
    }
}

void airports_draw(lv_draw_ctx_t *ctx, lv_color_t color, lv_opa_t opa) {
    if (s_apts.empty()) return;

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = color; ring.width = 2; ring.opa = opa;

    lv_draw_line_dsc_t diamond;                 // small-airport glyph: a hollow diamond
    lv_draw_line_dsc_init(&diamond);
    diamond.color = color; diamond.opa = opa; diamond.width = 1;
    diamond.round_start = 1; diamond.round_end = 1;

    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color = color; lbl.opa = opa; lbl.font = &lv_font_montserrat_12;

    for (const Apt &ap : s_apts) {
        if (ap.large) {
            lv_draw_arc(ctx, &ring, &ap.pos, 3, 0, 360);                    // small hollow ring
            if (ap.iata[0]) {
                lv_area_t la = { (lv_coord_t)(ap.pos.x + 5), (lv_coord_t)(ap.pos.y - 7),
                                 (lv_coord_t)(ap.pos.x + 44), (lv_coord_t)(ap.pos.y + 7) };
                lv_draw_label(ctx, &lbl, &la, ap.iata, NULL);
            }
        } else {
            // small airport: a tiny hollow diamond (reads as a marker, not a dead pixel)
            const lv_coord_t r = 3;
            lv_point_t top = { ap.pos.x, (lv_coord_t)(ap.pos.y - r) };
            lv_point_t rgt = { (lv_coord_t)(ap.pos.x + r), ap.pos.y };
            lv_point_t bot = { ap.pos.x, (lv_coord_t)(ap.pos.y + r) };
            lv_point_t lft = { (lv_coord_t)(ap.pos.x - r), ap.pos.y };
            lv_draw_line(ctx, &diamond, &top, &rgt);
            lv_draw_line(ctx, &diamond, &rgt, &bot);
            lv_draw_line(ctx, &diamond, &bot, &lft);
            lv_draw_line(ctx, &diamond, &lft, &top);
        }
    }
}
