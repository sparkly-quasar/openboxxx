// SPDX-License-Identifier: GPL-2.0-or-later
//
// ANLZ analysis-file writer. BIG-endian tagged-section container (PMAI + PXXX).
// Spec: docs/research-findings.md §B.3. Layouts ported in spirit from pyrekordbox
// (MIT) -- see docs/export-design.md "license" note. Section order, the PMAI
// header bytes, PVBR and the exact cue-list shapes follow rbxport
// (github.com/chrisle/rbxport, GPL-2.0-or-later), which measured them on
// hundreds of files rekordbox wrote.
//
// .DAT: PPTH, PVBR, PQTZ (beatgrid), PCOB (hot cues A-C), PCOB (memory cues).
// .EXT: PPTH, PCOB (hot cues D on), PCOB (empty), PCO2 (hot), PCO2 (memory).
// Waveforms (PWAV/PWV2 in .DAT, PWV3/PWV4/PWV5 in .EXT, the .2EX file) are
// Phase 2 (additive sections).
#ifndef OPENBOXXX_ANLZ_WRITER_H
#define OPENBOXXX_ANLZ_WRITER_H

#include <vector>

#include "openboxxx/byteio.h"
#include "openboxxx/model.h"

namespace openboxxx {

// Build the .DAT analysis file for one track. Returns the raw bytes.
std::vector<uint8_t> buildAnlzDat(const Track& track);

// Build the .EXT analysis file for one track: the extended cue lists newer
// players read (names, colours, hot cues past C). Same folder and name as the
// .DAT, extension swapped; players find it from the .DAT's path.
std::vector<uint8_t> buildAnlzExt(const Track& track);

// --- section builders (exposed for unit tests) ---
// Each appends one complete tagged section to `out`.
void putPpth(ByteBuffer& out, const std::string& usb_path);          // audio path (UTF-16BE)
void putPvbr(ByteBuffer& out);                                       // empty VBR seek table
void putPqtz(ByteBuffer& out, const std::vector<Beat>& beatgrid);    // beat grid
// A PCOB cue list of exactly `cues`, already chosen and ordered: `hot` marks
// the hot-cue list (true) or the memory-cue list (false), which rekordbox
// chains together.
void putPcob(ByteBuffer& out, const std::vector<Cue>& cues, bool hot);
// A PCO2 extended cue list: every cue, with its label and colour.
void putPco2(ByteBuffer& out, const std::vector<Cue>& cues, bool hot);

}  // namespace openboxxx

#endif  // OPENBOXXX_ANLZ_WRITER_H
