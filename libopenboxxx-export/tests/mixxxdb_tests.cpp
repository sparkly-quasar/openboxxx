// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for the Phase 1 reader adapter. These stand in for a real mixxxdb in CI:
// parseMixxxBeats runs on a hand-built protobuf blob, and readMixxxDb runs on a
// temp SQLite fixture we populate with the Mixxx schema. The byte layouts and
// unit conversions here were confirmed against a real 2.x library.
#include "check.h"

#include <sqlite3.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "openboxxx/mixxxdb_reader.h"

using namespace openboxxx;

// --- beats protobuf parser ---

TEST(beats_beatgrid_2_0_decodes_bpm_and_phase) {
    // BeatGrid-2.0: Bpm{double 120.0} + FramePos{varint 1000}.
    //   0A 09 09 <120.0 LE double> 12 03 08 <varint 1000>
    const std::vector<uint8_t> blob = {
        0x0A, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5E, 0x40,
        0x12, 0x03, 0x08, 0xE8, 0x07};
    // sr 48000, bpm 120 => 24000 frames/beat => 500 ms/beat.
    auto beats = parseMixxxBeats(blob.data(), blob.size(), "BeatGrid-2.0",
                                 /*sample_rate=*/48000, /*duration_s=*/10.0);
    CHECK(!beats.empty());
    CHECK(beats.front().tempo_x100 == 12000);      // 120.00 BPM
    CHECK(beats.front().beat_number == 1);          // first emitted = downbeat
    // first_beat 1000 frames -> ~21 ms; next beat +500 ms.
    CHECK(beats.front().time_ms == 21);
    CHECK(beats.size() >= 2);
    CHECK(beats[1].time_ms - beats[0].time_ms == 500);
    CHECK(beats[1].beat_number == 2);
}

TEST(beats_rejects_empty_and_unknown) {
    CHECK(parseMixxxBeats(nullptr, 0, "BeatGrid-2.0", 44100, 100.0).empty());
    const std::vector<uint8_t> junk = {0x01, 0x02, 0x03};
    CHECK(parseMixxxBeats(junk.data(), junk.size(), "SomethingElse", 44100, 100.0)
              .empty());
}

TEST(beats_large_first_frame_terminates) {
    // Regression: a huge first-beat must not spin (was an unbounded back-walk).
    // Bpm{120.0} + FramePos{varint ~4.3e9}.
    const std::vector<uint8_t> blob = {
        0x0A, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5E, 0x40,
        0x12, 0x06, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    auto beats = parseMixxxBeats(blob.data(), blob.size(), "BeatGrid-2.0",
                                 44100, 5.0);
    CHECK(beats.size() < 20001);  // bounded by the safety cap, not hung
}

// --- readMixxxDb against a temp SQLite fixture ---

namespace {

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::printf("  fixture exec failed: %s\n", err ? err : "?");
        sqlite3_free(err);
    }
}

// Write a minimal Mixxx-shaped DB to `path` and return true on success.
bool buildFixture(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) return false;

    exec(db,
         "CREATE TABLE track_locations(id INTEGER PRIMARY KEY, location TEXT, "
         "filename TEXT, directory TEXT, filesize INTEGER, fs_deleted INTEGER, "
         "needs_verification INTEGER);");
    exec(db,
         "CREATE TABLE library(id INTEGER PRIMARY KEY, artist TEXT, title TEXT, "
         "album TEXT, year TEXT, genre TEXT, tracknumber TEXT, location INTEGER, "
         "comment TEXT, composer TEXT, duration FLOAT, bitrate INTEGER, "
         "samplerate INTEGER, channels INTEGER, rating INTEGER, key TEXT, "
         "beats BLOB, beats_version TEXT, filetype TEXT, color INTEGER, "
         "mixxx_deleted INTEGER);");
    exec(db,
         "CREATE TABLE cues(id INTEGER PRIMARY KEY, track_id INTEGER, "
         "type INTEGER, position REAL, length REAL, hotcue INTEGER, label TEXT, "
         "color INTEGER);");
    exec(db,
         "CREATE TABLE Playlists(id INTEGER PRIMARY KEY, name TEXT, "
         "position INTEGER, hidden INTEGER);");
    exec(db,
         "CREATE TABLE PlaylistTracks(id INTEGER PRIMARY KEY, "
         "playlist_id INTEGER, track_id INTEGER, position INTEGER);");

    exec(db,
         "INSERT INTO track_locations VALUES"
         "(10,'/music/artist/song.flac','song.flac','/music/artist',123,0,0),"
         "(11,'/music/gone.mp3','gone.mp3','/music',55,1,0);");  // fs_deleted

    // Track 1: present, stereo 48k, color Red 0x00F87090, with a beats blob.
    exec(db,
         "INSERT INTO library(id,artist,title,album,year,genre,tracknumber,"
         "location,comment,composer,duration,bitrate,samplerate,channels,rating,"
         "key,beats,beats_version,filetype,color,mixxx_deleted) VALUES"
         "(1,'Earthly','Tall Tree','Heart','2019','Techno','3',10,'c','',10.0,"
         "320,48000,2,4,'F',X'0A09090000000000005E40120308E807',"
         "'BeatGrid-2.0','flac',0xF87090,0);");  // color 0x00F87090 (Red)
    // Track 2: file missing (fs_deleted) -> must be skipped by default.
    exec(db,
         "INSERT INTO library(id,title,location,duration,samplerate,channels,"
         "mixxx_deleted) VALUES(2,'Missing',11,5.0,44100,2,0);");

    // Cues for track 1: a hot cue @ 1000ms and a loop 2000-3000ms.
    // stereo samples: pos 96000 -> 96000/2/48000*1000 = 1000 ms.
    exec(db,
         "INSERT INTO cues(id,track_id,type,position,length,hotcue,label,color) "
         "VALUES"
         "(1,1,1,96000,0,0,'A',0),"          // hot cue slot 0 @ 1000ms
         "(2,1,4,192000,96000,-1,'',0),"     // loop 2000..3000ms
         "(3,1,8,50,0,-1,'',0);");           // analysis cue -> skipped

    exec(db, "INSERT INTO Playlists VALUES(1,'My Set',0,0),(2,'AutoDJ',1,2);");
    exec(db,
         "INSERT INTO PlaylistTracks VALUES(1,1,1,0),(2,1,2,1),(3,2,1,0);");

    sqlite3_close(db);
    return true;
}

}  // namespace

TEST(reader_maps_tracks_cues_playlists) {
    namespace fs = std::filesystem;
    const fs::path db = fs::temp_directory_path() / "openboxxx_fixture.sqlite";
    fs::remove(db);
    CHECK(buildFixture(db.string()));

    MixxxReadReport rep;
    ExportModel m = readMixxxDb(db.string(), MixxxReadOptions{}, &rep);

    // Only the present track survives (missing-file track skipped).
    CHECK(m.tracks.size() == 1);
    CHECK(rep.tracks_skipped == 1);
    if (m.tracks.empty()) return;  // avoid UB if the fixture regressed
    const Track& t = m.tracks.front();
    CHECK(t.id == 1);
    CHECK(t.title == "Tall Tree");
    CHECK(t.artist == "Earthly");
    CHECK(t.year == 2019);
    CHECK(t.sample_rate == 48000);
    CHECK(t.source_abs_path == "/music/artist/song.flac");
    CHECK(t.file_path == "/Contents/1/song.flac");

    // Color decoded from 0x00F87090.
    CHECK(t.color.has_value());
    CHECK(t.color.has_value() && t.color->r == 0xF8 && t.color->g == 0x70 &&
          t.color->b == 0x90);

    // Beatgrid synthesized.
    CHECK(!t.beatgrid.empty());
    CHECK(!t.beatgrid.empty() && t.beatgrid.front().tempo_x100 == 12000);

    // Cues: hot @ 1000ms, loop 2000..3000ms; analysis cue dropped.
    CHECK(t.cues.size() == 2);
    const Cue* hot = nullptr;
    const Cue* loop = nullptr;
    for (const Cue& c : t.cues) {
        if (c.kind == CueKind::HotCue) hot = &c;
        if (c.kind == CueKind::Loop) loop = &c;
    }
    CHECK(hot != nullptr);
    CHECK(hot && hot->hot_index == 0 && hot->time_ms == 1000);
    CHECK(loop != nullptr);
    CHECK(loop && loop->time_ms == 2000);
    CHECK(loop && loop->loop_end_ms.has_value() && loop->loop_end_ms.value() == 3000);

    // Playlists: only the visible one, referencing only the present track.
    CHECK(m.playlists.size() == 1);
    CHECK(m.playlists.front().name == "My Set");
    CHECK(m.playlists.front().track_ids.size() == 1);
    CHECK(m.playlists.front().track_ids.front() == 1);

    fs::remove(db);
}

// Regression fixture for two tier-2 fidelity fixes found against a real library:
// (1) a hot cue nudged before the start (negative position) must be clamped to 0
//     and kept, not dropped; (2) a MainCue and an Intro that coincide must not
//     produce two stacked memory cues at the same millisecond.
namespace {
bool buildCueFixture(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) return false;
    exec(db,
         "CREATE TABLE track_locations(id INTEGER PRIMARY KEY, location TEXT, "
         "filename TEXT, directory TEXT, filesize INTEGER, fs_deleted INTEGER, "
         "needs_verification INTEGER);");
    exec(db,
         "CREATE TABLE library(id INTEGER PRIMARY KEY, artist TEXT, title TEXT, "
         "album TEXT, year TEXT, genre TEXT, tracknumber TEXT, location INTEGER, "
         "comment TEXT, composer TEXT, duration FLOAT, bitrate INTEGER, "
         "samplerate INTEGER, channels INTEGER, rating INTEGER, key TEXT, "
         "beats BLOB, beats_version TEXT, filetype TEXT, color INTEGER, "
         "mixxx_deleted INTEGER);");
    exec(db,
         "CREATE TABLE cues(id INTEGER PRIMARY KEY, track_id INTEGER, "
         "type INTEGER, position REAL, length REAL, hotcue INTEGER, label TEXT, "
         "color INTEGER);");
    exec(db,
         "CREATE TABLE Playlists(id INTEGER PRIMARY KEY, name TEXT, "
         "position INTEGER, hidden INTEGER);");
    exec(db,
         "CREATE TABLE PlaylistTracks(id INTEGER PRIMARY KEY, "
         "playlist_id INTEGER, track_id INTEGER, position INTEGER);");
    exec(db,
         "INSERT INTO track_locations VALUES"
         "(20,'/music/x/song.flac','song.flac','/music/x',123,0,0);");
    exec(db,
         "INSERT INTO library(id,title,location,duration,samplerate,channels,"
         "mixxx_deleted) VALUES(1,'Cued',20,200.0,48000,2,0);");
    // stereo 48k: 96000 samples -> 1000 ms.
    exec(db,
         "INSERT INTO cues(id,track_id,type,position,length,hotcue,label,color) "
         "VALUES"
         "(1,1,1,-4800,0,2,'',0),"       // hot cue nudged before start -> clamp 0
         "(2,1,2,96000,0,-1,'',0),"      // MainCue @ 1000ms
         "(3,1,6,96000,0,-1,'',0);");    // Intro   @ 1000ms (coincides -> dedup)
    sqlite3_close(db);
    return true;
}
}  // namespace

TEST(reader_clamps_negative_hotcue_and_dedupes_memory) {
    namespace fs = std::filesystem;
    const fs::path db = fs::temp_directory_path() / "openboxxx_cue_fixture.sqlite";
    fs::remove(db);
    CHECK(buildCueFixture(db.string()));

    ExportModel m = readMixxxDb(db.string());
    CHECK(m.tracks.size() == 1);
    if (m.tracks.empty()) return;
    const Track& t = m.tracks.front();

    int hot = 0, mem = 0;
    const Cue* hotcue = nullptr;
    for (const Cue& c : t.cues) {
        if (c.kind == CueKind::HotCue) { ++hot; hotcue = &c; }
        if (c.kind == CueKind::MemoryCue) ++mem;
    }
    // The negative-position hot cue survives, clamped to 0 ms.
    CHECK(hot == 1);
    CHECK(hotcue && hotcue->time_ms == 0);
    // MainCue + Intro at the same ms collapse to a single memory cue.
    CHECK(mem == 1);

    fs::remove(db);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // flush immediately so a crash is visible
    for (auto& [name, fn] : obxtest::registry()) {
        std::printf("[ %s ]\n", name.c_str());
        fn();
    }
    const int f = obxtest::failures();
    std::printf("\n%s (%d failure%s)\n", f == 0 ? "PASS" : "FAIL", f,
                f == 1 ? "" : "s");
    return f == 0 ? 0 : 1;
}
