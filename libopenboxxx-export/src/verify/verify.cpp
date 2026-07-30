// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/verify.h"

namespace openboxxx {

VerifyResult roundTripPdb(const ExportModel& /*model*/,
                          const std::vector<uint8_t>& /*pdb_bytes*/) {
    // TODO(phase0): parse pdb_bytes with the Kaitai rekordbox_pdb parser and diff
    // track/playlist/cue/beatgrid fields against `model`. Stubbed for now.
    VerifyResult r;
    r.ok = false;
    r.issues.push_back("roundTripPdb not implemented (needs Kaitai parser wiring)");
    return r;
}

}  // namespace openboxxx
