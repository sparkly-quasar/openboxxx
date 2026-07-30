// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/usb_layout.h"

#include <cstdio>

namespace openboxxx {

std::string anlzPathForTrack(uint32_t track_id) {
    // rekordbox uses a P<xxx>/<hex> scheme keyed off internal ids; the exact
    // bucket is cosmetic as long as it's unique and the DB string matches.
    // We derive a stable bucket from the id. (See §B.1 open question on hashing.)
    char buf[64];
    const unsigned bucket = track_id % 1000u;  // P000..P999
    std::snprintf(buf, sizeof(buf),
                  "/PIONEER/USBANLZ/P%03u/%08X/ANLZ0000.DAT", bucket, track_id);
    return std::string(buf);
}

void assignAnlzPaths(ExportModel& model) {
    for (Track& t : model.tracks) {
        t.analyze_path = anlzPathForTrack(t.id);
    }
}

}  // namespace openboxxx
