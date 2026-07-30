// SPDX-License-Identifier: GPL-2.0-or-later
//
// Standalone CLI harness. Phase 0's driver: build a tiny in-memory model, run
// the writers, and report what came out. This is how we exercise the library
// without Mixxx while the byte layout is still being nailed down.
//
// TODO(phase0): load a real fixture library (JSON) and actually write files to a
// target directory. For now it runs a built-in one-track model as a smoke test.
#include <cstdio>

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

int main() {
    ExportModel model = demoModel();
    UsbImage image = buildUsbImage(model);

    std::printf("Built USB image: %zu file(s)\n", image.files.size());
    for (const UsbFile& f : image.files) {
        std::printf("  %-48s %zu bytes\n", f.path.c_str(), f.bytes.size());
    }
    std::printf("\n%s\n", buildDiagnosticManifest(model).c_str());
    return 0;
}
