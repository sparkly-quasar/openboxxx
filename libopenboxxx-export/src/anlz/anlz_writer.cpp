// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/anlz_writer.h"

#include <cstdint>

namespace openboxxx {
namespace {

// Append UTF-16BE code units for an ASCII/UTF-8 path plus a null terminator.
// ANLZ paths are stored big-endian (unlike the LE PDB payloads). Non-ASCII is
// left as a Phase-2 refinement; MVP paths we generate are ASCII by construction.
void putUtf16BeWithNull(ByteBuffer& out, const std::string& s) {
    for (unsigned char c : s) {
        out.putU16BE(uint16_t(c));  // ASCII fast path
    }
    out.putU16BE(0);  // null terminator
}

// Open a tagged section: writes fourcc + placeholder len_header/len_tag and
// returns the buffer offset of len_tag so the caller can backfill it.
std::size_t openTag(ByteBuffer& out, const char* fourcc, uint32_t len_header) {
    out.putAscii(std::string(fourcc, 4));
    out.putU32BE(len_header);
    const std::size_t len_tag_off = out.size();
    out.putU32BE(0);  // len_tag placeholder, backfilled in closeTag()
    return len_tag_off;
}

void closeTag(ByteBuffer& out, std::size_t len_tag_off, std::size_t tag_start) {
    out.patchU32BE(len_tag_off, uint32_t(out.size() - tag_start));
}

}  // namespace

void putPpth(ByteBuffer& out, const std::string& usb_path) {
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PPTH", 16);
    // len_path counts the UTF-16BE bytes including the null terminator.
    const uint32_t len_path = uint32_t((usb_path.size() + 1) * 2);
    out.putU32BE(len_path);
    putUtf16BeWithNull(out, usb_path);
    closeTag(out, len_tag_off, start);
}

void putPqtz(ByteBuffer& out, const std::vector<Beat>& beatgrid) {
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PQTZ", 24);
    out.putU32BE(0);
    out.putU32BE(0x80000);
    out.putU32BE(uint32_t(beatgrid.size()));
    for (const Beat& b : beatgrid) {
        out.putU16BE(b.beat_number);
        out.putU16BE(b.tempo_x100);
        out.putU32BE(b.time_ms);
    }
    closeTag(out, len_tag_off, start);
}

void putPcob(ByteBuffer& out, const std::vector<Cue>& cues, bool hot) {
    // Count entries belonging to this list.
    uint16_t num = 0;
    for (const Cue& c : cues) {
        const bool is_hot = (c.kind == CueKind::HotCue);
        if (is_hot == hot) ++num;
    }

    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PCOB", 24);
    out.putU32BE(hot ? 1u : 0u);   // cue_type: 0=memory, 1=hot
    out.putU16BE(0);
    out.putU16BE(num);
    out.putU32BE(num);             // memory_count (== num for a fresh export)

    // PCPT entries. Layout is best-effort (see header note); validated later.
    for (const Cue& c : cues) {
        const bool is_hot = (c.kind == CueKind::HotCue);
        if (is_hot != hot) continue;
        const bool is_loop = c.loop_end_ms.has_value();

        // PCPT entry layout confirmed against pyrekordbox structs (verification
        // tier 2). openTag writes fourcc + len_header(28); closeTag backfills the
        // second u32 to the whole entry size (== len_entry, 56).
        const std::size_t e_start = out.size();
        const std::size_t e_len_off = openTag(out, "PCPT", 0x1C);
        out.putU32BE(is_hot ? uint32_t(c.hot_index + 1) : 0u);  // hot_cue #
        out.putU32BE(4);                                        // status = enabled
        out.putU32BE(0x10000);                                  // u1 const
        out.putU16BE(0xFFFF);                                   // order_first (refine later)
        out.putU16BE(0xFFFF);                                   // order_last
        out.putU8(is_loop ? 2 : 1);                             // type: 1=cue, 2=loop
        out.putU8(0);                                           // padding(1)
        out.putU16BE(1000);                                     // u2 const (0x3E8)
        out.putU32BE(c.time_ms);
        out.putU32BE(is_loop ? c.loop_end_ms.value() : 0xFFFFFFFFu);  // loop_time (-1)
        out.putZeros(16);                                      // padding(16)
        closeTag(out, e_len_off, e_start);
    }
    closeTag(out, len_tag_off, start);
}

std::vector<uint8_t> buildAnlzDat(const Track& track) {
    ByteBuffer out;
    // PMAI container header. len_header=28: 12 (magic+lens) + 16 bytes of pad.
    out.putAscii("PMAI");
    out.putU32BE(28);
    const std::size_t len_file_off = out.size();
    out.putU32BE(0);       // len_file, backfilled below
    out.putZeros(16);      // pad to len_header (4 x u4 unknown)

    putPpth(out, track.file_path);
    putPqtz(out, track.beatgrid);
    putPcob(out, track.cues, /*hot=*/false);  // memory-cue list
    putPcob(out, track.cues, /*hot=*/true);   // hot-cue list

    out.patchU32BE(len_file_off, uint32_t(out.size()));
    return out.bytes();
}

}  // namespace openboxxx
