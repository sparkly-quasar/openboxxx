// SPDX-License-Identifier: GPL-2.0-or-later
//
// Intermediate export model. The standalone library core takes THIS (plain
// structs, no Qt/Mixxx types) and emits bytes. The Mixxx adapter (Phase 1) is
// responsible for filling it from the live library; the mapping/ layer converts
// Mixxx's semantics into these fields (see docs/export-design.md).
//
// Times are in whole milliseconds here (rekordbox's native unit); the mapping
// layer does the Mixxx-frame -> ms conversion so the writers never see frames.
#ifndef OPENBOXXX_MODEL_H
#define OPENBOXXX_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openboxxx {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// One beat marker: rekordbox stores per-beat {bar-position, tempo, time}.
struct Beat {
    uint16_t beat_number = 1;  // 1..4 (position within the bar)
    uint16_t tempo_x100 = 0;   // BPM * 100
    uint32_t time_ms = 0;
};

enum class CueKind { HotCue, MemoryCue, Loop };

struct Cue {
    CueKind kind = CueKind::HotCue;
    int hot_index = -1;                 // 0-based hot-cue slot; <0 for memory cues
    uint32_t time_ms = 0;
    std::optional<uint32_t> loop_end_ms;  // set only for loops
    std::optional<Rgb> color;
    std::string label;                  // may be empty
};

struct Track {
    uint32_t id = 0;                    // stable per-export track id
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string key;                    // musical key label; mapped to key-id downstream
    std::string comment;
    std::string composer;
    int year = 0;
    int track_number = 0;
    int duration_s = 0;
    int bitrate = 0;
    int sample_rate = 0;
    int64_t file_size = 0;
    int rating = 0;                     // 0..5 (mapped to rekordbox's 0..255 downstream)
    std::optional<Rgb> color;

    // Paths as they will live on the USB (forward-slash, media-root-relative).
    std::string file_path;              // audio file, e.g. /Contents/artist/track.flac
    std::string analyze_path;           // e.g. /PIONEER/USBANLZ/P016/0000875E/ANLZ0000.DAT

    // Absolute path to the source audio on the host, so usb/ can copy it.
    std::string source_abs_path;

    std::vector<Beat> beatgrid;
    std::vector<Cue> cues;
};

struct Playlist {
    uint32_t id = 0;
    uint32_t parent_id = 0;             // 0 = root
    bool is_folder = false;
    std::string name;
    std::vector<uint32_t> track_ids;    // ordered; empty for folders
};

struct ExportModel {
    std::vector<Track> tracks;
    std::vector<Playlist> playlists;
};

}  // namespace openboxxx

#endif  // OPENBOXXX_MODEL_H
