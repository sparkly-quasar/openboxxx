// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/diag.h"

#include <sstream>

namespace openboxxx {

std::string buildDiagnosticManifest(const ExportModel& model) {
    std::size_t with_grid = 0, with_cues = 0, total_cues = 0;
    for (const Track& t : model.tracks) {
        if (!t.beatgrid.empty()) ++with_grid;
        if (!t.cues.empty()) ++with_cues;
        total_cues += t.cues.size();
    }

    std::ostringstream os;
    os << "# openboxxx export diagnostics\n\n"
       << "tracks: " << model.tracks.size() << "\n"
       << "playlists: " << model.playlists.size() << "\n"
       << "tracks_with_beatgrid: " << with_grid << "\n"
       << "tracks_with_cues: " << with_cues << "\n"
       << "total_cues: " << total_cues << "\n"
       << "\n(structural only -- no audio or personal metadata included)\n";
    return os.str();
}

}  // namespace openboxxx
