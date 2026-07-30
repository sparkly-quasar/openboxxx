// SPDX-License-Identifier: GPL-2.0-or-later
//
// Verification (ladder tiers 1-2, docs/export-design.md). Parses our own output
// back and diffs it against the intended model / a reference stick.
//
// TODO(phase0): tier 1 requires wiring the in-tree Kaitai parsers
// (rekordbox_pdb_t / rekordbox_anlz_t from Mixxx lib/rekordbox-metadata/) as a
// read-back dependency. Until then this is a placeholder API.
#ifndef OPENBOXXX_VERIFY_H
#define OPENBOXXX_VERIFY_H

#include <string>
#include <vector>

#include "openboxxx/model.h"

namespace openboxxx {

struct VerifyResult {
    bool ok = false;
    std::vector<std::string> issues;
};

// Round-trip check: parse `pdb_bytes` back and confirm it reproduces `model`.
VerifyResult roundTripPdb(const ExportModel& model, const std::vector<uint8_t>& pdb_bytes);

}  // namespace openboxxx

#endif  // OPENBOXXX_VERIFY_H
