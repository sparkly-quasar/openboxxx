// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase 1 adapter (reader path): fill an ExportModel straight from a Mixxx
// library file (mixxxdb.sqlite). This is standalone -- it needs SQLite but NOT
// Qt or a Mixxx build -- so it runs anywhere and lets a beta tester export by
// pointing the tool at their DB. The eventual in-Mixxx RekordboxExportJob fills
// the SAME ExportModel from live objects, so all mapping decisions here carry
// over unchanged.
//
// Units, decoded from a real 2.x library and cross-checked (see the reader impl):
//   - cue `position`/`length` are fractional STEREO SAMPLES (frames * channels)
//   - beatgrid first-beat is in FRAMES (not samples)
//   - colors are 0x00RRGGBB (alpha byte ignored)
#ifndef OPENBOXXX_MIXXXDB_READER_H
#define OPENBOXXX_MIXXXDB_READER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "openboxxx/model.h"

namespace openboxxx {

struct MixxxReadOptions {
    // Map Mixxx Intro(6)/Outro(7) analysis cues as rekordbox memory cues.
    bool include_intro_outro = true;
    // Skip rows whose file is marked missing on disk (track_locations.fs_deleted).
    bool only_existing_files = true;
    // On-USB parent dir for copied audio; final path is <prefix>/<id>/<filename>.
    std::string contents_prefix = "/Contents";
};

// What the read produced -- surfaced to the CLI and (later) the diag bundle.
struct MixxxReadReport {
    int tracks_read = 0;
    int tracks_skipped = 0;
    int tracks_with_beatgrid = 0;
    int cues = 0;
    int playlists = 0;
    std::vector<std::string> warnings;
};

// Open `db_path` read-only and build the export model. Throws std::runtime_error
// if the database cannot be opened or a required query fails.
ExportModel readMixxxDb(const std::string& db_path,
                        const MixxxReadOptions& opts = {},
                        MixxxReadReport* report = nullptr);

// Parse a Mixxx `beats` BLOB into a synthesized per-beat grid. Supports
// "BeatGrid-2.0" (constant tempo: {bpm, first_beat_frame}) and "BeatMap-1.0"
// (explicit beat frames). Returns empty on unparseable/empty input or <2 beats.
// Exposed for unit testing against synthetic blobs.
std::vector<Beat> parseMixxxBeats(const uint8_t* data, std::size_t len,
                                  const std::string& beats_version,
                                  int sample_rate, double duration_s);

}  // namespace openboxxx

#endif  // OPENBOXXX_MIXXXDB_READER_H
