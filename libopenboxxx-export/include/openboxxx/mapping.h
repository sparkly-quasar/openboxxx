// SPDX-License-Identifier: GPL-2.0-or-later
//
// Mapping helpers: rekordbox-specific encodings that invert what the Mixxx
// *importer* already does (docs/research-findings.md §A.4). Kept dependency-free
// so both the standalone tests and the Mixxx adapter can use them.
#ifndef OPENBOXXX_MAPPING_H
#define OPENBOXXX_MAPPING_H

#include <cstdint>

#include "openboxxx/model.h"

namespace openboxxx {

// rekordbox's fixed 8-colour palette id (1..8), or 0 for "no colour".
// Inverts the importer's colorFromID() table by nearest-RGB match.
uint8_t rekordboxColorId(const Rgb& c);

// Mixxx rating 0..5 -> rekordbox 0..255 (importer uses rating*51-ish; the Engine
// exporter uses *20 for a 0..100 scale -- rekordbox's own scale is 0..255).
uint8_t rekordboxRating(int mixxx_rating_0_5);

}  // namespace openboxxx

#endif  // OPENBOXXX_MAPPING_H
