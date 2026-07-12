#pragma once
#include <cstdint>

namespace radar {

enum class ScopeStyle : uint8_t { kRings, kGrid, kVector };   // kRings=stock, kGrid=Orb, kVector=CIC/ClaudeIC
enum class BlipMode   : uint8_t { kAltRamp, kMono };          // altitude color-code vs single hue
enum class Decoration : uint8_t { kNone, kSweep, kStarfield };
enum class BlipShape  : uint8_t { kAuto, kChevron, kSilhouette, kDiamond };
enum BlipFx : uint8_t { FX_LEADER = 1u<<0, FX_VECTOR = 1u<<1, FX_ALTSIZE = 1u<<2 };

// One theme = chrome palette + background + scope geometry + blip policy + decoration.
// Colors are 0xRRGGBB logical RGB (fed to lv_color_hex). `layer` tints the
// coastline/airports/flow so they don't clash with the palette; kVector themes
// override this with natural map colors in the vector draw path.
struct ThemeDesc {
    const char* name;
    uint32_t ring;    // rings / crosshair / sweep base   (-> s_cRing)
    uint32_t lead;    // sweep leading edge / accents      (-> s_cLead)
    uint32_t ink;     // labels / center / selection       (-> s_cInk)
    uint32_t soft;    // secondary labels                  (-> s_cSoft)
    uint32_t bg;      // background fill
    uint32_t layer;   // coastline/airport/flow tint
    ScopeStyle scope;
    BlipMode   blips;
    uint32_t   mono;  // blip color when blips == kMono
    Decoration decor;
    bool       sweep;
    BlipShape  shape;
    bool       outline;
    uint8_t    blipFx;
};

extern const ThemeDesc kThemes[];
extern const int kThemeCount;

}  // namespace radar
