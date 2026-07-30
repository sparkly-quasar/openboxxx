// SPDX-License-Identifier: GPL-2.0-or-later
//
// Diagnostics bundle for the in-app "report a bug" flow (docs/export-design.md).
// Produces a STRUCTURAL manifest only -- counts, which sections were written,
// per-track cue/beatgrid presence, verifier results, content hashes -- and
// never the user's music files or full library metadata.
#ifndef OPENBOXXX_DIAG_H
#define OPENBOXXX_DIAG_H

#include <string>

#include "openboxxx/model.h"

namespace openboxxx {

// Render a human-and-machine-readable structural manifest (Markdown/JSON-ish)
// safe to attach to a public bug report.
std::string buildDiagnosticManifest(const ExportModel& model);

}  // namespace openboxxx

#endif  // OPENBOXXX_DIAG_H
