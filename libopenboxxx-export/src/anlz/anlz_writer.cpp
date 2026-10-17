// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/anlz_writer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace openboxxx {
namespace {

// The 16 bytes rekordbox writes after the PMAI frame. Their meaning isn't
// documented; they're reproduced from rekordbox's own files (rbxport,
// crates/rbl-anlz/src/write.rs, DEFAULT_HEADER_EXTRA).
constexpr std::array<uint8_t, 16> kPmaiHeaderExtra{0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};

constexpr uint32_t kNone32 = 0xFFFFFFFFu;
constexpr uint16_t kNone16 = 0xFFFF;
// Hot cues A-C go in the .DAT (what the oldest players read); every hot cue
// goes in the .EXT's PCO2, and D onwards in its PCOB too.
constexpr int kDatHotCues = 3;

// The CDJ-3000's 65-entry cue colour palette, as rbxport read it from
// firmware 3.20 and matched against rekordbox exports (crates/rbl-anlz/src/
// cues.rs, DEVICE_PALETTE). PCO2 names a colour by its index here.
constexpr std::array<std::array<uint8_t, 3>, 65> kDevicePalette{{
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xff}, {0x00, 0x1c, 0xff}, {0x00, 0x38, 0xff},
    {0x00, 0x54, 0xff}, {0x00, 0x70, 0xff}, {0x00, 0x8c, 0xff}, {0x00, 0xa8, 0xff},
    {0x00, 0xc4, 0xff}, {0x00, 0xe0, 0xff}, {0x00, 0xff, 0xff}, {0x00, 0xff, 0xe8},
    {0x00, 0xff, 0xd1}, {0x00, 0xff, 0xba}, {0x00, 0xff, 0xa3}, {0x00, 0xff, 0x8c},
    {0x00, 0xff, 0x75}, {0x00, 0xff, 0x5e}, {0x00, 0xff, 0x47}, {0x00, 0xff, 0x30},
    {0x00, 0xff, 0x1a}, {0x00, 0xff, 0x00}, {0x1a, 0xff, 0x00}, {0x33, 0xff, 0x00},
    {0x4d, 0xff, 0x00}, {0x66, 0xff, 0x00}, {0x80, 0xff, 0x00}, {0x99, 0xff, 0x00},
    {0xb3, 0xff, 0x00}, {0xcc, 0xff, 0x00}, {0xe6, 0xff, 0x00}, {0xff, 0xff, 0x00},
    {0xff, 0xe8, 0x00}, {0xff, 0xd1, 0x00}, {0xff, 0xba, 0x00}, {0xff, 0xa3, 0x00},
    {0xff, 0x8c, 0x00}, {0xff, 0x75, 0x00}, {0xff, 0x5e, 0x00}, {0xff, 0x47, 0x00},
    {0xff, 0x30, 0x00}, {0xff, 0x1a, 0x00}, {0xff, 0x00, 0x00}, {0xff, 0x00, 0x17},
    {0xff, 0x00, 0x2e}, {0xff, 0x00, 0x45}, {0xff, 0x00, 0x5c}, {0xff, 0x00, 0x73},
    {0xff, 0x00, 0x8a}, {0xff, 0x00, 0xa1}, {0xff, 0x00, 0xb8}, {0xff, 0x00, 0xcf},
    {0xff, 0x00, 0xe6}, {0xff, 0x00, 0xff}, {0xe6, 0x00, 0xff}, {0xcc, 0x00, 0xff},
    {0xb3, 0x00, 0xff}, {0x99, 0x00, 0xff}, {0x80, 0x00, 0xff}, {0x66, 0x00, 0xff},
    {0x4d, 0x00, 0xff}, {0x33, 0x00, 0xff}, {0x1a, 0x00, 0xff}, {0x00, 0x00, 0x00},
    {0xff, 0xff, 0xff},
}};

// UTF-8 -> UTF-16 code units (surrogate pairs for astral code points);
// invalid sequences are skipped.
std::vector<uint16_t> utf16(const std::string& s) {
    std::vector<uint16_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = s[i];
        uint32_t cp;
        std::size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { ++i; continue; }
        if (i + len > s.size()) break;
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = s[i + k];
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += len;
        if (!ok) continue;
        if (cp <= 0xFFFF) {
            out.push_back(uint16_t(cp));
        } else {
            cp -= 0x10000;
            out.push_back(uint16_t(0xD800 + (cp >> 10)));
            out.push_back(uint16_t(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

// Open a tagged section: fourcc + len_header + placeholder len_tag. Returns the
// offset of len_tag, for closeTag to backfill.
std::size_t openTag(ByteBuffer& out, const char* fourcc, uint32_t len_header) {
    out.putAscii(std::string(fourcc, 4));
    out.putU32BE(len_header);
    const std::size_t len_tag_off = out.size();
    out.putU32BE(0);
    return len_tag_off;
}

void closeTag(ByteBuffer& out, std::size_t len_tag_off, std::size_t tag_start) {
    out.patchU32BE(len_tag_off, uint32_t(out.size() - tag_start));
}

bool isHot(const Cue& c) { return c.kind == CueKind::HotCue; }

// 1 for hot cue A, 0 for a memory cue.
uint32_t slot(const Cue& c) { return isHot(c) ? uint32_t(c.hot_index + 1) : 0u; }

// The PCO2 colour of a cue: (palette index, RGB). A cue with its own colour
// takes the nearest palette entry; one without gets the default rekordbox
// exports give its slot (rbxport, cues.rs, export_colour).
std::pair<uint8_t, std::array<uint8_t, 3>> cueColour(const Cue& c) {
    uint8_t code = 0;
    if (c.color) {
        uint32_t best = UINT32_MAX;
        for (uint8_t i = 1; i <= 62; ++i) {
            const auto& p = kDevicePalette[i];
            const int dr = int(p[0]) - c.color->r, dg = int(p[1]) - c.color->g,
                      db = int(p[2]) - c.color->b;
            const uint32_t d = uint32_t(dr * dr + dg * dg + db * db);
            if (d < best) { best = d; code = i; }
        }
    } else {
        switch (slot(c)) {
            case 1: case 9: code = 43; break;
            case 2: case 10: code = 8; break;
            case 3: case 11: code = 23; break;
            case 4: case 12: code = 60; break;
            default: code = 0;
        }
    }
    return {code, kDevicePalette[code]};
}

std::vector<Cue> sortedHot(const std::vector<Cue>& cues) {
    std::vector<Cue> out;
    for (const Cue& c : cues) if (isHot(c)) out.push_back(c);
    std::stable_sort(out.begin(), out.end(),
                     [](const Cue& a, const Cue& b) { return a.hot_index < b.hot_index; });
    return out;
}

std::vector<Cue> sortedMemory(const std::vector<Cue>& cues) {
    std::vector<Cue> out;
    for (const Cue& c : cues) if (!isHot(c)) out.push_back(c);
    std::stable_sort(out.begin(), out.end(),
                     [](const Cue& a, const Cue& b) { return a.time_ms < b.time_ms; });
    return out;
}

// PMAI header: magic, len_header (28), len_file (backfilled; its offset is
// returned), then rekordbox's 16 bytes.
std::size_t putHeader(ByteBuffer& out) {
    out.putAscii("PMAI");
    out.putU32BE(12 + uint32_t(kPmaiHeaderExtra.size()));
    const std::size_t len_file_off = out.size();
    out.putU32BE(0);
    out.putBytes(kPmaiHeaderExtra.data(), kPmaiHeaderExtra.size());
    return len_file_off;
}

}  // namespace

void putPpth(ByteBuffer& out, const std::string& usb_path) {
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PPTH", 16);
    const std::vector<uint16_t> units = utf16(usb_path);
    out.putU32BE(uint32_t((units.size() + 1) * 2));     // includes the terminator
    for (uint16_t u : units) out.putU16BE(u);
    out.putU16BE(0);
    closeTag(out, len_tag_off, start);
}

void putPvbr(ByteBuffer& out) {
    // rekordbox writes PVBR into every .DAT, between the path and the grid,
    // and rejects a .DAT without it. With no VBR frames to index this is the
    // table it writes: a zero header word and 401 zero words (rbxport,
    // write.rs, vbr_table_zero_section; 65 of 400 reference files).
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PVBR", 16);
    out.putU32BE(0);
    out.putZeros(401 * 4);
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
    // Layout as rekordbox writes it (rbxport, cues.rs, measured on hundreds of
    // files): the header's last word is 0xFFFFFFFF for a hot list or an empty
    // one, else the memory count less one; memory cues are a chain, each
    // naming the one before and after it; status is 0 unless the loop is active.
    const uint16_t n = uint16_t(std::min<std::size_t>(cues.size(), 0xFFFF));
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PCOB", 24);
    out.putU32BE(hot ? 1u : 0u);
    out.putU16BE(0);
    out.putU16BE(n);
    out.putU32BE(hot || n == 0 ? kNone32 : uint32_t(n - 1));
    for (uint16_t i = 0; i < n; ++i) {
        const Cue& c = cues[i];
        const bool is_loop = c.loop_end_ms.has_value();
        const std::size_t e_start = out.size();
        const std::size_t e_len_off = openTag(out, "PCPT", 0x1C);
        out.putU32BE(slot(c));
        out.putU32BE(0);                                    // status: not an active loop
        out.putU32BE(0x10000);
        out.putU16BE(hot || i == 0 ? kNone16 : uint16_t(i - 1));
        out.putU16BE(hot || i + 1 == n ? kNone16 : uint16_t(i + 1));
        out.putU8(is_loop ? 2 : 1);
        out.putU8(0);
        out.putU16BE(1000);
        out.putU32BE(c.time_ms);
        out.putU32BE(is_loop ? c.loop_end_ms.value() : kNone32);
        out.putZeros(16);
        closeTag(out, e_len_off, e_start);
    }
    closeTag(out, len_tag_off, start);
}

void putPco2(ByteBuffer& out, const std::vector<Cue>& cues, bool hot) {
    // The extended cue list newer players read: every cue, with its name and
    // colour (rbxport, cues.rs; a PCP2 entry matches its rekordbox test vector).
    const uint16_t n = uint16_t(std::min<std::size_t>(cues.size(), 0xFFFF));
    const std::size_t start = out.size();
    const std::size_t len_tag_off = openTag(out, "PCO2", 20);
    out.putU32BE(hot ? 1u : 0u);
    out.putU16BE(n);
    out.putU16BE(0);
    for (uint16_t i = 0; i < n; ++i) {
        const Cue& c = cues[i];
        const bool is_loop = c.loop_end_ms.has_value();
        std::vector<uint16_t> comment = utf16(c.label);
        if (!comment.empty()) comment.push_back(0);
        const uint32_t comment_bytes = uint32_t(comment.size() * 2);
        const uint32_t length = 88 + comment_bytes;
        const std::size_t e_start = out.size();
        out.putAscii("PCP2");
        out.putU32BE(16);
        out.putU32BE(length);
        out.putU32BE(slot(c));
        out.putU8(is_loop ? 2 : 1);
        out.putU8(0);
        out.putU16BE(1000);
        out.putU32BE(c.time_ms);
        out.putU32BE(is_loop ? c.loop_end_ms.value() : kNone32);
        out.putU8(0);                                       // colour id
        out.putU8(1);
        out.putZeros(6);
        out.putU16BE(0);                                    // loop numerator
        out.putU16BE(0);                                    // loop denominator
        out.putU32BE(comment_bytes);
        for (uint16_t u : comment) out.putU16BE(u);
        const auto colour = cueColour(c);
        out.putU8(colour.first);
        out.putBytes(colour.second.data(), colour.second.size());
        out.putZeros(length - (out.size() - e_start));
    }
    closeTag(out, len_tag_off, start);
}

std::vector<uint8_t> buildAnlzDat(const Track& track) {
    // Sections in rekordbox's order: PPTH, PVBR, PQTZ, then hot cues A-C, then
    // memory cues. (Waveforms, PWAV and PWV2, aren't written yet.)
    ByteBuffer out;
    const std::size_t len_file_off = putHeader(out);
    putPpth(out, track.file_path);
    putPvbr(out);
    putPqtz(out, track.beatgrid);
    std::vector<Cue> dat_hot;
    for (const Cue& c : sortedHot(track.cues)) if (c.hot_index < kDatHotCues) dat_hot.push_back(c);
    putPcob(out, dat_hot, /*hot=*/true);
    putPcob(out, sortedMemory(track.cues), /*hot=*/false);
    out.patchU32BE(len_file_off, uint32_t(out.size()));
    return out.bytes();
}

std::vector<uint8_t> buildAnlzExt(const Track& track) {
    // PPTH, PCOB (hot cues D onwards), PCOB (empty memory list), then every
    // cue again as PCO2. (Waveforms, PWV3/PWV5/PWV4, aren't written yet.)
    ByteBuffer out;
    const std::size_t len_file_off = putHeader(out);
    putPpth(out, track.file_path);
    const std::vector<Cue> hot = sortedHot(track.cues);
    std::vector<Cue> ext_hot;
    for (const Cue& c : hot) if (c.hot_index >= kDatHotCues) ext_hot.push_back(c);
    putPcob(out, ext_hot, /*hot=*/true);
    putPcob(out, {}, /*hot=*/false);
    putPco2(out, hot, /*hot=*/true);
    putPco2(out, sortedMemory(track.cues), /*hot=*/false);
    out.patchU32BE(len_file_off, uint32_t(out.size()));
    return out.bytes();
}

}  // namespace openboxxx
