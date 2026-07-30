// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/exporter.h"

#include "openboxxx/anlz_writer.h"
#include "openboxxx/pdb_writer.h"
#include "openboxxx/usb_layout.h"

namespace openboxxx {

UsbImage buildUsbImage(ExportModel model) {
    assignAnlzPaths(model);  // sets track.analyze_path so the PDB links match

    UsbImage image;

    // The database.
    image.files.push_back({"/PIONEER/rekordbox/export.pdb", buildExportPdb(model)});

    // Per-track analysis files.
    for (const Track& t : model.tracks) {
        image.files.push_back({t.analyze_path, buildAnlzDat(t)});
    }

    return image;
}

}  // namespace openboxxx
