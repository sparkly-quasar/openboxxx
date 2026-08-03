// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reader path of the Phase 1 adapter: mixxxdb.sqlite -> ExportModel. See
// mixxxdb_reader.h for the unit conventions decoded from a real library.
#include "openboxxx/mixxxdb_reader.h"

#include <sqlite3.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openboxxx {
namespace {

// Mixxx CueType (src/track/cue.h). We only need the ones we map.
enum MixxxCueType {
    kCueHot = 1,
    kCueMainCue = 2,
    kCueLoop = 4,
    kCueIntro = 6,
    kCueOutro = 7,
    // 8 (N60dBSound/AudioSegment) and others are analysis-only; skipped.
};

// RAII wrapper so we never leak a statement on an early return/throw.
class Stmt {
   public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("prepare failed: ") +
                                     sqlite3_errmsg(db));
        }
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    sqlite3_stmt* get() { return stmt_; }

   private:
    sqlite3_stmt* stmt_ = nullptr;
};

std::string colText(sqlite3_stmt* s, int i) {
    const unsigned char* t = sqlite3_column_text(s, i);
    return t ? reinterpret_cast<const char*>(t) : std::string();
}

bool colIsNull(sqlite3_stmt* s, int i) {
    return sqlite3_column_type(s, i) == SQLITE_NULL;
}

// Leading-integer parse: handles "2019", "2019-05-01", "" -> 0.
int leadingInt(const std::string& s) { return std::atoi(s.c_str()); }

// Mixxx stores key labels with unicode sharp/flat; rekordbox wants ASCII.
std::string asciiKey(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        const unsigned char c = in[i];
        // U+266F MUSIC SHARP = E2 99 AF ; U+266D MUSIC FLAT = E2 99 AD
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x99) {
            const unsigned char c3 = in[i + 2];
            if (c3 == 0xAF) { out += '#'; i += 3; continue; }
            if (c3 == 0xAD) { out += 'b'; i += 3; continue; }
        }
        out += char(c);
        ++i;
    }
    return out;
}

// stereo-sample position -> whole milliseconds.
uint32_t samplesToMs(double samples, int sample_rate, int channels) {
    if (sample_rate <= 0) return 0;
    const int ch = channels > 0 ? channels : 2;
    const double ms = samples / ch / sample_rate * 1000.0;
    return ms > 0.0 ? uint32_t(std::lround(ms)) : 0u;
}

std::optional<Rgb> intToRgb(sqlite3_stmt* s, int i) {
    if (colIsNull(s, i)) return std::nullopt;
    const int64_t v = sqlite3_column_int64(s, i);
    if (v == 0) return std::nullopt;  // Mixxx "no color"
    const uint32_t rgb = uint32_t(v) & 0xFFFFFFu;  // drop alpha byte
    return Rgb{uint8_t((rgb >> 16) & 0xFF), uint8_t((rgb >> 8) & 0xFF),
               uint8_t(rgb & 0xFF)};
}

std::string usbFileName(const std::string& location) {
    const std::size_t slash = location.find_last_of("/\\");
    return slash == std::string::npos ? location : location.substr(slash + 1);
}

}  // namespace

ExportModel readMixxxDb(const std::string& db_path,
                        const MixxxReadOptions& opts,
                        MixxxReadReport* report) {
    sqlite3* db = nullptr;
    // Open read-only; never modify the user's live library.
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        const std::string msg = db ? sqlite3_errmsg(db) : "unknown error";
        sqlite3_close(db);
        throw std::runtime_error("cannot open mixxxdb: " + msg);
    }

    ExportModel model;
    MixxxReadReport rep;
    std::unordered_set<uint32_t> track_ids;      // ids we actually export
    std::unordered_map<uint32_t, int> channels_by_id;  // for cue sample->ms
    std::unordered_map<uint32_t, int> sr_by_id;        // sample rate per track

    // --- tracks ---
    {
        std::string sql =
            "SELECT l.id, l.artist, l.title, l.album, l.genre, l.year, "
            "l.tracknumber, l.comment, l.composer, l.duration, l.bitrate, "
            "l.samplerate, l.channels, l.rating, l.color, l.key, l.beats, "
            "l.beats_version, l.filetype, tl.location, tl.filesize, tl.fs_deleted "
            "FROM library l JOIN track_locations tl ON l.location = tl.id "
            "WHERE l.mixxx_deleted = 0 ORDER BY l.id";
        Stmt st(db, sql.c_str());
        while (st.step()) {
            sqlite3_stmt* s = st.get();
            const int fs_deleted = sqlite3_column_int(s, 21);
            if (opts.only_existing_files && fs_deleted != 0) {
                ++rep.tracks_skipped;
                continue;
            }
            Track t;
            t.id = uint32_t(sqlite3_column_int64(s, 0));
            t.artist = colText(s, 1);
            t.title = colText(s, 2);
            t.album = colText(s, 3);
            t.genre = colText(s, 4);
            t.year = leadingInt(colText(s, 5));
            t.track_number = leadingInt(colText(s, 6));
            t.comment = colText(s, 7);
            t.composer = colText(s, 8);
            t.duration_s = int(std::lround(sqlite3_column_double(s, 9)));
            t.bitrate = sqlite3_column_int(s, 10);
            t.sample_rate = sqlite3_column_int(s, 11);
            const int channels = sqlite3_column_int(s, 12);
            t.rating = sqlite3_column_int(s, 13);
            t.color = intToRgb(s, 14);
            t.key = asciiKey(colText(s, 15));

            // beats blob -> synthesized grid.
            const void* beats_blob = sqlite3_column_blob(s, 16);
            const int beats_len = sqlite3_column_bytes(s, 16);
            const std::string beats_version = colText(s, 17);
            if (beats_blob && beats_len > 0) {
                t.beatgrid = parseMixxxBeats(
                    static_cast<const uint8_t*>(beats_blob),
                    std::size_t(beats_len), beats_version, t.sample_rate,
                    double(t.duration_s));
                if (!t.beatgrid.empty()) ++rep.tracks_with_beatgrid;
            }

            const std::string location = colText(s, 19);
            t.file_size = sqlite3_column_int64(s, 20);
            t.source_abs_path = location;
            t.file_path = opts.contents_prefix + "/" + std::to_string(t.id) +
                          "/" + usbFileName(location);

            // Channel count is needed later to convert cue sample offsets to ms;
            // keep it in a side map rather than widening the Track struct.
            channels_by_id[t.id] = channels > 0 ? channels : 2;
            sr_by_id[t.id] = t.sample_rate;
            model.tracks.push_back(std::move(t));
            track_ids.insert(model.tracks.back().id);
            ++rep.tracks_read;
        }
    }

    // --- cues (single pass, bucketed by track) ---
    {
        std::unordered_map<uint32_t, std::vector<Cue>> cues_by_track;
        Stmt st(db,
                "SELECT track_id, type, position, length, hotcue, label, color "
                "FROM cues ORDER BY track_id, hotcue, position");
        while (st.step()) {
            sqlite3_stmt* s = st.get();
            const uint32_t track_id = uint32_t(sqlite3_column_int64(s, 0));
            if (track_ids.find(track_id) == track_ids.end()) continue;
            const int type = sqlite3_column_int(s, 1);
            const double position = sqlite3_column_double(s, 2);
            const double length = sqlite3_column_double(s, 3);
            const int hotcue = sqlite3_column_int(s, 4);
            // Mixxx stores -1 as the "unset" sentinel for cues that were never
            // placed (a main/intro/outro with no position). Hot cues and loops
            // always denote a real point; a slightly-negative value is a cue
            // nudged just before the start, which rekordbox clamps to 0 rather
            // than dropping (samplesToMs clamps the negative to 0). Preserve
            // those so we don't silently lose a hot cue.
            const bool always_positional = (type == kCueHot || type == kCueLoop);
            if (position < 0 && !always_positional) continue;  // unset cue

            auto chit = channels_by_id.find(track_id);
            const int ch = chit != channels_by_id.end() ? chit->second : 2;
            auto srit = sr_by_id.find(track_id);
            const int sr = srit != sr_by_id.end() && srit->second > 0
                               ? srit->second
                               : 44100;

            Cue c;
            c.time_ms = samplesToMs(position, sr, ch);
            c.label = colText(s, 5);
            c.color = intToRgb(s, 6);

            bool keep = true;
            switch (type) {
                case kCueHot:
                    c.kind = CueKind::HotCue;
                    c.hot_index = hotcue;
                    break;
                case kCueMainCue:
                    c.kind = CueKind::MemoryCue;
                    c.hot_index = -1;
                    break;
                case kCueLoop:
                    c.kind = CueKind::Loop;
                    c.hot_index = hotcue;  // >=0 => saved (hot) loop
                    c.loop_end_ms = samplesToMs(position + length, sr, ch);
                    break;
                case kCueIntro:
                case kCueOutro:
                    if (!opts.include_intro_outro) { keep = false; break; }
                    c.kind = CueKind::MemoryCue;
                    c.hot_index = -1;
                    if (c.label.empty()) c.label = (type == kCueIntro) ? "Intro"
                                                                       : "Outro";
                    break;
                default:
                    keep = false;  // analysis-only cue types
                    break;
            }
            if (!keep) continue;
            cues_by_track[track_id].push_back(std::move(c));
            ++rep.cues;
        }
        for (Track& t : model.tracks) {
            auto it = cues_by_track.find(t.id);
            if (it == cues_by_track.end()) continue;
            // Drop redundant memory cues that land on the same millisecond: a
            // Mixxx MainCue and Intro often coincide, and rekordbox would
            // otherwise show two memory points stacked at the same spot. Hot
            // cues keep their own slots, so they're never deduped here.
            std::unordered_set<uint32_t> seen_mem_ms;
            std::vector<Cue> deduped;
            deduped.reserve(it->second.size());
            for (Cue& c : it->second) {
                if (c.kind == CueKind::MemoryCue &&
                    !seen_mem_ms.insert(c.time_ms).second) {
                    continue;  // duplicate memory cue at this millisecond
                }
                deduped.push_back(std::move(c));
            }
            t.cues = std::move(deduped);
        }
    }

    // --- playlists (visible only; ordered) ---
    {
        std::unordered_map<uint32_t, std::size_t> index_of;  // id -> model slot
        {
            Stmt st(db,
                    "SELECT id, name FROM Playlists WHERE hidden = 0 "
                    "ORDER BY position, id");
            while (st.step()) {
                Playlist pl;
                pl.id = uint32_t(sqlite3_column_int64(st.get(), 0));
                pl.name = colText(st.get(), 1);
                index_of[pl.id] = model.playlists.size();
                model.playlists.push_back(std::move(pl));
            }
        }
        Stmt st(db,
                "SELECT playlist_id, track_id FROM PlaylistTracks "
                "ORDER BY playlist_id, position");
        while (st.step()) {
            const uint32_t pid = uint32_t(sqlite3_column_int64(st.get(), 0));
            const uint32_t tid = uint32_t(sqlite3_column_int64(st.get(), 1));
            auto pit = index_of.find(pid);
            if (pit == index_of.end()) continue;             // hidden playlist
            if (track_ids.find(tid) == track_ids.end()) continue;  // dropped track
            model.playlists[pit->second].track_ids.push_back(tid);
        }
        rep.playlists = int(model.playlists.size());
    }

    sqlite3_close(db);
    if (report) *report = std::move(rep);
    return model;
}

}  // namespace openboxxx
