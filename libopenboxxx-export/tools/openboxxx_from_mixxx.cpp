// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase 1 reader-path driver: read a Mixxx library file and write a CDJ USB
// image, no Mixxx build required. This is what a beta tester runs.
//
// Usage:
//   openboxxx_from_mixxx --db PATH [--out DIR] [--copy-audio]
//                        [--no-intro-outro] [--limit N]
//
//   --db PATH        path to mixxxdb.sqlite (required)
//   --out DIR        write the PIONEER/ tree under DIR (omit for a dry run)
//   --copy-audio     also copy each track's audio file onto the image
//   --no-intro-outro don't map Mixxx Intro/Outro cues as memory cues
//   --limit N        only export the first N tracks (handy for quick tests)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "openboxxx/exporter.h"
#include "openboxxx/mixxxdb_reader.h"

using namespace openboxxx;
namespace fs = std::filesystem;

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

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) db_path = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--copy-audio") copy_audio = true;
        else if (a == "--no-intro-outro") opts.include_intro_outro = false;
        else if (a == "--limit" && i + 1 < argc) limit = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "unknown/again arg: %s\n", a.c_str()); }
    }
    if (db_path.empty()) {
        std::fprintf(stderr,
                     "usage: openboxxx_from_mixxx --db PATH [--out DIR] "
                     "[--copy-audio] [--no-intro-outro] [--limit N]\n");
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

    if (limit > 0 && int(model.tracks.size()) > limit) {
        model.tracks.resize(size_t(limit));
        // Drop playlist references to trimmed tracks so the PDB stays consistent.
        std::vector<uint32_t> kept;
        for (const Track& t : model.tracks) kept.push_back(t.id);
        for (Playlist& pl : model.playlists) {
            std::vector<uint32_t> filtered;
            for (uint32_t id : pl.track_ids)
                for (uint32_t k : kept)
                    if (id == k) { filtered.push_back(id); break; }
            pl.track_ids = std::move(filtered);
        }
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
