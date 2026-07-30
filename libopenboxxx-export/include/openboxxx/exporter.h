// SPDX-License-Identifier: GPL-2.0-or-later
//
// Top-level orchestration: ExportModel -> in-memory USB image. This is the one
// entry point the Mixxx adapter (Phase 1) calls. It stays free of filesystem
// side effects so it is trivially testable; a thin caller writes the bytes out.
#ifndef OPENBOXXX_EXPORTER_H
#define OPENBOXXX_EXPORTER_H

#include <cstdint>
#include <string>
#include <vector>

#include "openboxxx/model.h"

namespace openboxxx {

// One file destined for the USB: media-root-relative path + its bytes.
struct UsbFile {
    std::string path;              // e.g. /PIONEER/rekordbox/export.pdb
    std::vector<uint8_t> bytes;
};

// The full set of files to lay onto the stick (excluding copied audio, which the
// caller streams to `track.file_path` from `track.source_abs_path`).
struct UsbImage {
    std::vector<UsbFile> files;
};

// Build the USB image: assigns ANLZ paths, writes export.pdb + per-track ANLZ.
UsbImage buildUsbImage(ExportModel model);

}  // namespace openboxxx

#endif  // OPENBOXXX_EXPORTER_H
