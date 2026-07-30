// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/mapping.h"

#include <array>
#include <cstdint>

namespace openboxxx {
namespace {

// The 8 rekordbox palette colours, indexed by id-1. RGB values are the inverse
// of the importer's kColorForID* constants (research-findings.md §A.4).
struct Pal { uint8_t id; Rgb rgb; };
constexpr std::array<Pal, 8> kPalette{{
    {1, {0xF8, 0x70, 0xF8}},  // Pink
    {2, {0xF8, 0x70, 0x90}},  // Red
    {3, {0xF8, 0xA0, 0x30}},  // Orange
    {4, {0xF8, 0xE3, 0x31}},  // Yellow
    {5, {0x1E, 0xE0, 0x00}},  // Green
    {6, {0x16, 0xC0, 0xF8}},  // Aqua
    {7, {0x01, 0x50, 0xF8}},  // Blue
    {8, {0x98, 0x08, 0xF8}},  // Purple
}};

uint32_t dist2(const Rgb& a, const Rgb& b) {
    const int dr = int(a.r) - int(b.r);
    const int dg = int(a.g) - int(b.g);
    const int db = int(a.b) - int(b.b);
    return uint32_t(dr * dr + dg * dg + db * db);
}

}  // namespace

uint8_t rekordboxColorId(const Rgb& c) {
    uint8_t best_id = 0;
    uint32_t best = UINT32_MAX;
    for (const Pal& p : kPalette) {
        const uint32_t d = dist2(c, p.rgb);
        if (d < best) { best = d; best_id = p.id; }
    }
    return best_id;
}

uint8_t rekordboxRating(int mixxx_rating_0_5) {
    if (mixxx_rating_0_5 <= 0) return 0;
    if (mixxx_rating_0_5 >= 5) return 255;
    // rekordbox stores star ratings at fixed points: 51/102/153/204/255.
    return uint8_t(mixxx_rating_0_5 * 51);
}

}  // namespace openboxxx
