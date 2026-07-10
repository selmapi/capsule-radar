#include "theme_table.h"
namespace radar {
const ThemeDesc kThemes[] = {
  // 0 Phosphor (stock green + sweep)
  {"Phosphor", 0x1DFF86,0x3DFF9A,0xEAFFF3,0x9AFFC8, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 1 Orb (grid scope) — gradient bottom handled in setTheme()
  {"Orb", 0x3F8B30,0x3F8B30,0xEAFFF3,0x9AFFC8, 0x18540F,0x4E86C6, ScopeStyle::kGrid, BlipMode::kAltRamp,0, Decoration::kNone,false},
  // 2 Amber CRT
  {"Amber CRT", 0xFFB23C,0xFFD27A,0xFFE9C2,0xFFC98A, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 3 Military
  {"Military", 0x49C46B,0x76E08C,0xE0FFE6,0x9FD7A8, 0x000000,0x4E86C6, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 4 Vice (neon pink/violet)
  {"Vice", 0xFF2A9D,0x7A2AFF,0xF0E0FF,0xF0E0FF, 0x12041F,0x7A2AFF, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 5 Midnight (navy field, stock-radar readouts) — LOGICAL values, NOT the plane-radar's pre-swapped bytes
  {"Midnight", 0x106420,0x3896AA,0xFFFFFF,0x5AC8FF, 0x040A1C,0x3896AA, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kSweep,true},
  // 6 Silent Running (mono red + slow sweep)
  {"Silent Running", 0x7A1408,0xFF3A1E,0xFF5A3A,0xFF5A3A, 0x0A0002,0x5A0F06, ScopeStyle::kRings, BlipMode::kMono,0xFF5A3A, Decoration::kSweep,true},
  // 7 Mission Control (navy/gold + starfield)
  {"Mission Control", 0xD4A544,0xD4A544,0xE8E2CE,0xB8C8E8, 0x081228,0x6A84B0, ScopeStyle::kRings, BlipMode::kAltRamp,0, Decoration::kStarfield,false},
  // 8 CIC (green vector scope, amber mono targets, no sweep)
  {"CIC", 0x2AAB5A,0x5AFF8A,0x5AFF8A,0x2AAB5A, 0x000000,0x2AAB5A, ScopeStyle::kVector, BlipMode::kMono,0xFFB428, Decoration::kNone,false},
  // 9 ClaudeIC (CIC geometry in Claude's palette; mascot added later)
  {"ClaudeIC", 0xCC785C,0xE8825A,0xF0EEE6,0xC9C4B8, 0x14100E,0xCC785C, ScopeStyle::kVector, BlipMode::kMono,0xE8825A, Decoration::kNone,false},
};
const int kThemeCount = (int)(sizeof(kThemes)/sizeof(kThemes[0]));
}  // namespace radar
