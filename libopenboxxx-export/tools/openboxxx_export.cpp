// SPDX-License-Identifier: GPL-2.0-or-later
//
// Standalone CLI harness. Phase 0's driver: build a tiny in-memory model, run
// the writers, and report what came out. This is how we exercise the library
// without Mixxx while the byte layout is still being nailed down.
//
// Usage:
//   openboxxx_export_cli              -- smoke test on a built-in demo model
//   openboxxx_export_cli --out DIR    -- also write the USB image under DIR
//
// TODO(phase0): load a real fixture library (JSON) instead of the demo model.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "openboxxx/diag.h"
#include "openboxxx/exporter.h"

using namespace openboxxx;

static ExportModel demoModel() {
    Track t;
    t.id = 0x875E;
    t.title = "Demo Track";
    t.artist = "openboxxx";
    t.duration_s = 180;
    t.sample_rate = 44100;
    t.file_path = "/Contents/openboxxx/demo.flac";
    t.beatgrid = {{1, 12800, 0}, {2, 12800, 469}, {3, 12800, 938}, {4, 12800, 1407}};
    t.cues.push_back({CueKind::MemoryCue, -1, 0, std::nullopt, std::nullopt, "intro"});
    t.cues.push_back({CueKind::HotCue, 0, 30000, std::nullopt, Rgb{0xF8, 0x70, 0x90}, "A"});

    ExportModel m;
    m.tracks.push_back(t);
    Playlist pl;
    pl.id = 1;
    pl.name = "Demo";
    pl.track_ids = {t.id};
    m.playlists.push_back(pl);
    return m;
}

static void writeImage(const UsbImage& image, const std::string& out_dir) {
    namespace fs = std::filesystem;
    for (const UsbFile& f : image.files) {
        // f.path is media-root-relative with a leading '/'; join under out_dir.
        const fs::path dest = fs::path(out_dir) / f.path.substr(1);
        fs::create_directories(dest.parent_path());
        std::ofstream os(dest, std::ios::binary);
        os.write(reinterpret_cast<const char*>(f.bytes.data()),
                 std::streamsize(f.bytes.size()));
    }
    std::printf("Wrote USB image under %s\n", out_dir.c_str());
}

int main(int argc, char** argv) {
    std::string out_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    ExportModel model = demoModel();
    UsbImage image = buildUsbImage(model);

    std::printf("Built USB image: %zu file(s)\n", image.files.size());
    for (const UsbFile& f : image.files) {
        std::printf("  %-48s %zu bytes\n", f.path.c_str(), f.bytes.size());
    }
    if (!out_dir.empty()) writeImage(image, out_dir);
    std::printf("\n%s\n", buildDiagnosticManifest(model).c_str());
    return 0;
}
