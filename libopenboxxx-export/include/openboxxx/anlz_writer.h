// SPDX-License-Identifier: GPL-2.0-or-later
//
// ANLZ analysis-file writer. BIG-endian tagged-section container (PMAI + PXXX).
// Spec: docs/research-findings.md §B.3. Layouts ported in spirit from pyrekordbox
// (MIT) -- see docs/export-design.md "license" note.
//
// MVP (.DAT): PPTH + PQTZ (beatgrid) + PCOB(memory) + PCOB(hot). Waveforms,
// PCO2/.EXT, and .2EX are Phase 2 (additive sections).
#ifndef OPENBOXXX_ANLZ_WRITER_H
#define OPENBOXXX_ANLZ_WRITER_H

#include <vector>

#include "openboxxx/byteio.h"
#include "openboxxx/model.h"

namespace openboxxx {

// Build the .DAT analysis file for one track. Returns the raw bytes.
std::vector<uint8_t> buildAnlzDat(const Track& track);

// --- section builders (exposed for unit tests) ---
// Each appends one complete tagged section to `out`.
void putPpth(ByteBuffer& out, const std::string& usb_path);          // audio path (UTF-16BE)
void putPqtz(ByteBuffer& out, const std::vector<Beat>& beatgrid);    // beat grid
// PCOB cue list. `hot` selects the hot-cue list (true) vs memory-cue list (false).
// NOTE: exact PCPT entry layout is best-effort pending validation against a real
// rekordbox stick (verification tier 2) -- see docs/export-design.md open questions.
void putPcob(ByteBuffer& out, const std::vector<Cue>& cues, bool hot);

}  // namespace openboxxx

#endif  // OPENBOXXX_ANLZ_WRITER_H
