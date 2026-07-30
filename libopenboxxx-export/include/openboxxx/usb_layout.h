// SPDX-License-Identifier: GPL-2.0-or-later
//
// USB directory layout. Assigns each track its ANLZ path and lays down the
// PIONEER/ tree. Spec: docs/research-findings.md §B.1.
//
// Linkage is by stored string: track.analyze_path MUST match the file actually
// written. ⚠️ Confirm the ANLZ-path hash quirk (newer firmware may require a
// specific folder-path hash) before relying on free folder naming.
#ifndef OPENBOXXX_USB_LAYOUT_H
#define OPENBOXXX_USB_LAYOUT_H

#include <string>

#include "openboxxx/model.h"

namespace openboxxx {

// Fill each track's analyze_path with a media-root-relative
// /PIONEER/USBANLZ/P<xxx>/<8-hex>/ANLZ0000.DAT path, derived from its id.
// Deterministic so re-exports are stable.
void assignAnlzPaths(ExportModel& model);

// Compute the ANLZ path for a single track id (exposed for tests).
std::string anlzPathForTrack(uint32_t track_id);

}  // namespace openboxxx

#endif  // OPENBOXXX_USB_LAYOUT_H
