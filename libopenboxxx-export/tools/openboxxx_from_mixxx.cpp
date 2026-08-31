// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase 1 reader-path driver: read a Mixxx library file and write a CDJ USB
// image, no Mixxx build required. This is what a beta tester runs.
//
// Usage:
//   openboxxx_from_mixxx --db PATH [--out DIR] [--copy-audio]
//                        [--no-intro-outro] [--limit N] [--ids A,B,C]
//
// Run with --help for the same list at the terminal.
//
//   --db PATH        path to mixxxdb.sqlite (required)
//   --out DIR        write the PIONEER/ tree under DIR (omit for a dry run)
//   --copy-audio     also copy each track's audio file onto the image
//   --no-intro-outro don't map Mixxx Intro/Outro cues as memory cues
//   --limit N        only export the first N tracks (handy for quick tests)
//   --ids A,B,C      only export these Mixxx track ids (targeted test sets)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "openboxxx/exporter.h"
#include "openboxxx/mixxxdb_reader.h"

using namespace openboxxx;
namespace fs = std::filesystem;

static void usage(std::FILE* out) {
    std::fprintf(out,
"usage: openboxxx_from_mixxx --db PATH [options]\n"
"\n"
"Reads a Mixxx library (read-only -- your library is never modified) and writes\n"
"a rekordbox/CDJ USB image: PIONEER/rekordbox/export.pdb plus per-track ANLZ\n"
"analysis files.\n"
"\n"
"  --db PATH          path to mixxxdb.sqlite (required)\n"
"  --out DIR          write the USB tree under DIR; omit for a dry run\n"
"  --copy-audio       also copy the audio files, so the device actually plays\n"
"  --limit N          export only the first N tracks (handy for a first test)\n"
"  --ids A,B,C        export only these Mixxx track ids (targeted test sets)\n"
"  --no-intro-outro   don't map Mixxx Intro/Outro cues as memory cues\n"
"  -h, --help         show this help\n"
"\n"
"Without --out nothing is written: the library is read and the counts reported.\n"
"\n"
"Typical mixxxdb.sqlite locations:\n"
"  Linux     ~/.mixxx/mixxxdb.sqlite\n"
"  Windows   %%LOCALAPPDATA%%\\Mixxx\\mixxxdb.sqlite\n"
"  macOS     ~/Library/Application Support/Mixxx/mixxxdb.sqlite\n"
"  macOS     ~/Library/Containers/org.mixxx.mixxx/Data/Library/\n"
"  (sandboxed)   Application Support/Mixxx/mixxxdb.sqlite\n");
}

static void writeFile(const fs::path& dest, const std::vector<uint8_t>& bytes) {
    fs::create_directories(dest.parent_path());
    std::ofstream os(dest, std::ios::binary);
    os.write(reinterpret_cast<const char*>(bytes.data()),
             std::streamsize(bytes.size()));
}

int main(int argc, char** argv) {
    std::string db_path, out_dir;
    bool copy_audio = false;
    MixxxReadOptions opts;
    int limit = 0;
    std::set<uint32_t> only_ids;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) db_path = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--copy-audio") copy_audio = true;
        else if (a == "--no-intro-outro") opts.include_intro_outro = false;
        else if (a == "--limit" && i + 1 < argc) limit = std::atoi(argv[++i]);
        else if (a == "--ids" && i + 1 < argc) {
            const std::string csv = argv[++i];
            for (std::size_t p = 0; p < csv.size();) {
                std::size_t c = csv.find(',', p);
                if (c == std::string::npos) c = csv.size();
                const std::string tok = csv.substr(p, c - p);
                if (!tok.empty()) only_ids.insert(uint32_t(std::atoi(tok.c_str())));
                p = c + 1;
            }
        }
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else {
            std::fprintf(stderr, "unrecognised argument: %s\n\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }
    if (db_path.empty()) {
        std::fprintf(stderr, "error: --db is required\n\n");
        usage(stderr);
        return 2;
    }

    MixxxReadReport rep;
    ExportModel model;
    try {
        model = readMixxxDb(db_path, opts, &rep);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "read failed: %s\n", e.what());
        return 1;
    }

    // Repair playlist references after any track-set trimming so the PDB stays
    // consistent (only reference tracks that survived).
    auto repairPlaylists = [&]() {
        std::set<uint32_t> kept;
        for (const Track& t : model.tracks) kept.insert(t.id);
        for (Playlist& pl : model.playlists) {
            std::vector<uint32_t> filtered;
            for (uint32_t id : pl.track_ids)
                if (kept.count(id)) filtered.push_back(id);
            pl.track_ids = std::move(filtered);
        }
    };

    if (!only_ids.empty()) {
        std::vector<Track> picked;
        for (Track& t : model.tracks)
            if (only_ids.count(t.id)) picked.push_back(std::move(t));
        model.tracks = std::move(picked);
        repairPlaylists();
    }

    if (limit > 0 && int(model.tracks.size()) > limit) {
        model.tracks.resize(size_t(limit));
        repairPlaylists();
    }

    std::printf("Read %s\n", db_path.c_str());
    std::printf("  tracks read     : %d\n", int(model.tracks.size()));
    std::printf("  tracks skipped  : %d (missing files)\n", rep.tracks_skipped);
    std::printf("  with beatgrid   : %d\n", rep.tracks_with_beatgrid);
    std::printf("  cues mapped     : %d\n", rep.cues);
    std::printf("  playlists       : %d\n", rep.playlists);
    for (const std::string& w : rep.warnings)
        std::printf("  warning: %s\n", w.c_str());

    UsbImage image = buildUsbImage(model);
    std::printf("Built USB image: %zu generated file(s)\n", image.files.size());

    if (out_dir.empty()) {
        std::printf("(dry run -- pass --out DIR to write the stick)\n");
        return 0;
    }

    size_t bytes_written = 0;
    for (const UsbFile& f : image.files) {
        writeFile(fs::path(out_dir) / f.path.substr(1), f.bytes);
        bytes_written += f.bytes.size();
    }
    std::printf("Wrote %zu generated file(s) (%zu bytes) under %s\n",
                image.files.size(), bytes_written, out_dir.c_str());

    if (copy_audio) {
        int copied = 0, missing = 0;
        for (const Track& t : model.tracks) {
            if (t.source_abs_path.empty() || t.file_path.empty()) continue;
            std::error_code ec;
            const fs::path dest = fs::path(out_dir) / t.file_path.substr(1);
            fs::create_directories(dest.parent_path(), ec);
            fs::copy_file(t.source_abs_path, dest,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) { ++missing; } else { ++copied; }
        }
        std::printf("Copied %d audio file(s); %d could not be copied\n", copied,
                    missing);
    } else {
        std::printf("(audio not copied -- pass --copy-audio for a playable stick)\n");
    }
    return 0;
}
