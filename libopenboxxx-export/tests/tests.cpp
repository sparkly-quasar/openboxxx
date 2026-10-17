// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.h"

#include <algorithm>
#include <string>

#include "openboxxx/anlz_writer.h"
#include "openboxxx/byteio.h"
#include "openboxxx/device_sql_string.h"
#include "openboxxx/exporter.h"
#include "openboxxx/mapping.h"
#include "openboxxx/pdb_writer.h"
#include "openboxxx/usb_layout.h"

using namespace openboxxx;

// --- byteio: explicit endianness is the cross-platform guarantee ---
TEST(byteio_endianness) {
    ByteBuffer b;
    b.putU32LE(0x11223344);
    b.putU32BE(0x11223344);
    b.putU16LE(0xABCD);
    b.putU16BE(0xABCD);
    const auto& d = b.bytes();
    CHECK(d.size() == 12);
    CHECK(d[0] == 0x44 && d[1] == 0x33 && d[2] == 0x22 && d[3] == 0x11);  // LE
    CHECK(d[4] == 0x11 && d[5] == 0x22 && d[6] == 0x33 && d[7] == 0x44);  // BE
    CHECK(d[8] == 0xCD && d[9] == 0xAB);
    CHECK(d[10] == 0xAB && d[11] == 0xCD);
}

TEST(byteio_patch) {
    ByteBuffer b;
    b.putU32BE(0);          // placeholder
    b.putBytes({1, 2, 3});
    b.patchU32BE(0, uint32_t(b.size()));
    CHECK(b.bytes()[3] == 7);  // total length backfilled
}

// --- device_sql_string: the three encodings ---
TEST(dsql_empty_is_0x03) {
    ByteBuffer b; putDeviceSqlString(b, "");
    CHECK(b.size() == 1 && b.bytes()[0] == 0x03);
}

TEST(dsql_short_ascii) {
    ByteBuffer b; putDeviceSqlString(b, "AB");
    // length_and_kind = (2+1)*2+1 = 7, then "AB"
    CHECK(b.size() == 3);
    CHECK(b.bytes()[0] == 7);
    CHECK(b.bytes()[1] == 'A' && b.bytes()[2] == 'B');
}

TEST(dsql_long_utf16_for_nonascii) {
    ByteBuffer b; putDeviceSqlString(b, "\xC3\xA9");  // "é" in UTF-8
    CHECK(b.bytes()[0] == 0x90);       // long UTF-16 marker
    // header(4) + 1 code unit * 2 = 6 bytes total
    CHECK(b.size() == 6);
    CHECK(b.bytes()[1] == 6 && b.bytes()[2] == 0);  // u2 length LE, then pad
    CHECK(b.bytes()[4] == 0xE9 && b.bytes()[5] == 0x00);  // é = U+00E9, UTF-16LE
}

// --- mapping ---
TEST(mapping_color_exact_and_nearest) {
    CHECK(rekordboxColorId(Rgb{0xF8, 0x70, 0x90}) == 2);  // exact Red
    CHECK(rekordboxColorId(Rgb{0x00, 0xFF, 0x00}) == 5);  // nearest Green
    CHECK(rekordboxRating(0) == 0);
    CHECK(rekordboxRating(3) == 153);
    CHECK(rekordboxRating(5) == 255);
}

// --- ANLZ framing ---
TEST(anlz_container_and_sections) {
    Track t;
    t.file_path = "/Contents/x.flac";
    t.beatgrid = {{1, 12800, 0}, {2, 12800, 500}};
    t.cues.push_back({CueKind::HotCue, 0, 1000, std::nullopt, std::nullopt, ""});
    auto dat = buildAnlzDat(t);
    CHECK(dat.size() > 28);
    CHECK(dat[0] == 'P' && dat[1] == 'M' && dat[2] == 'A' && dat[3] == 'I');
    // len_file (BE u4 at offset 8) equals actual size.
    const uint32_t len_file =
        (uint32_t(dat[8]) << 24) | (dat[9] << 16) | (dat[10] << 8) | dat[11];
    CHECK(len_file == dat.size());
    // First section right after the 28-byte header is PPTH.
    CHECK(dat[28] == 'P' && dat[29] == 'P' && dat[30] == 'T' && dat[31] == 'H');
    // rekordbox's 16 header bytes after the frame.
    const uint8_t extra[16] = {0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    CHECK(std::equal(extra, extra + 16, dat.begin() + 12));
}

namespace {
uint32_t be32(const std::vector<uint8_t>& d, std::size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) | d[o + 3];
}
uint16_t be16(const std::vector<uint8_t>& d, std::size_t o) {
    return uint16_t((d[o] << 8) | d[o + 1]);
}
// The sections of an ANLZ file, as (fourcc, offset) pairs.
std::vector<std::pair<std::string, std::size_t>> sections(const std::vector<uint8_t>& d) {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (std::size_t o = be32(d, 4); o + 12 <= d.size(); o += be32(d, o + 8)) {
        out.push_back({std::string(d.begin() + o, d.begin() + o + 4), o});
    }
    return out;
}
std::string names(const std::vector<std::pair<std::string, std::size_t>>& s) {
    std::string out;
    for (const auto& [n, o] : s) out += n + " ";
    return out;
}
Track cuedTrack() {
    Track t;
    t.file_path = "/Contents/x.flac";
    t.cues.push_back({CueKind::HotCue, 3, 4000, std::nullopt, std::nullopt, "D"});
    t.cues.push_back({CueKind::MemoryCue, -1, 9000, std::nullopt, std::nullopt, ""});
    t.cues.push_back({CueKind::HotCue, 0, 1000, std::nullopt, std::nullopt, "Intro"});
    t.cues.push_back({CueKind::Loop, -1, 2000, 6000u, std::nullopt, ""});
    return t;
}
}  // namespace

// --- ANLZ layout as rekordbox writes it ---
TEST(anlz_dat_sections_in_rekordbox_order) {
    auto dat = buildAnlzDat(cuedTrack());
    auto s = sections(dat);
    CHECK(names(s) == "PPTH PVBR PQTZ PCOB PCOB ");
    CHECK(be32(dat, s[1].second + 4) == 16);                 // PVBR: 16-byte header...
    CHECK(be32(dat, s[1].second + 8) == 16 + 401 * 4);       // ...and 401 zero words
    // First PCOB is the hot list (type 1) with A only: D lives in the .EXT.
    const std::size_t hot = s[3].second, mem = s[4].second;
    CHECK(be32(dat, hot + 12) == 1);
    CHECK(be16(dat, hot + 18) == 1);
    CHECK(be32(dat, hot + 20) == 0xFFFFFFFFu);
    CHECK(be32(dat, hot + 24 + 12) == 1);                    // hot cue A
    // Memory list: the loop at 2 s then the cue at 9 s, chained together.
    CHECK(be32(dat, mem + 12) == 0);
    CHECK(be16(dat, mem + 18) == 2);
    CHECK(be32(dat, mem + 20) == 1);                         // count less one
    const std::size_t e0 = mem + 24, e1 = e0 + 56;
    CHECK(be32(dat, e0 + 32) == 2000 && be32(dat, e0 + 36) == 6000 && dat[e0 + 28] == 2);
    CHECK(be16(dat, e0 + 24) == 0xFFFF && be16(dat, e0 + 26) == 1);
    CHECK(be16(dat, e1 + 24) == 0 && be16(dat, e1 + 26) == 0xFFFF);
    CHECK(be32(dat, e0 + 16) == 0);                          // status: not an active loop
}

TEST(anlz_ext_carries_named_cues) {
    auto ext = buildAnlzExt(cuedTrack());
    auto s = sections(ext);
    CHECK(names(s) == "PPTH PCOB PCOB PCO2 PCO2 ");
    CHECK(be16(ext, s[1].second + 18) == 1);                 // hot cue D only
    CHECK(be32(ext, s[1].second + 24 + 12) == 4);
    CHECK(be16(ext, s[2].second + 18) == 0);                 // empty memory list
    const std::size_t pco2 = s[3].second;
    CHECK(be16(ext, pco2 + 16) == 2);                        // A and D
    const std::size_t a = pco2 + 20;                         // first PCP2: A, "Intro"
    CHECK(std::string(ext.begin() + a, ext.begin() + a + 4) == "PCP2");
    CHECK(be32(ext, a + 12) == 1 && be32(ext, a + 20) == 1000);
    CHECK(be32(ext, a + 40) == 12);                          // "Intro" + NUL, UTF-16
    CHECK(be16(ext, a + 44) == 'I');
    CHECK(ext[a + 56] == 43 && ext[a + 57] == 0xFF && ext[a + 59] == 0x17);  // A's red
}

TEST(anlz_pcp2_matches_rekordbox_entry) {
    // rbxport's test vector, from a rekordbox export: hot cue A at 83,032 ms.
    // (Its colour code was 46; this cue has none, so A's default follows.)
    ByteBuffer b;
    putPco2(b, {{CueKind::HotCue, 0, 83032, std::nullopt, std::nullopt, ""}}, true);
    const uint8_t expect[] = {0x50, 0x43, 0x50, 0x32, 0, 0, 0, 0x10, 0, 0, 0, 0x58, 0, 0, 0, 1,
                              1, 0, 3, 0xe8, 0, 1, 0x44, 0x58, 0xff, 0xff, 0xff, 0xff, 0, 1};
    CHECK(std::equal(expect, expect + sizeof(expect), b.bytes().begin() + 20));
}

// --- PDB page framing invariants ---
TEST(pdb_page_is_4096_and_tracks_rows) {
    PdbPage page(PageType::Tracks);
    CHECK(page.tryAddRow(std::vector<uint8_t>(10, 0xAB)));
    CHECK(page.tryAddRow(std::vector<uint8_t>(20, 0xCD)));
    CHECK(page.rowCount() == 2);
    auto bytes = page.finalize(/*page_index=*/1, /*next_page_index=*/2);
    CHECK(bytes.size() == kPageSize);
    // page header sanity: page_index at 0x04, type at 0x08, next_page at 0x0c (LE)
    CHECK(bytes[4] == 1);
    CHECK(bytes[8] == uint8_t(PageType::Tracks));
    CHECK(bytes[12] == 2);
    // data-page flag at 0x1b; num_row_offsets/num_rows packed 24-bit at 0x18.
    CHECK(bytes[0x1b] == kPageFlagsData);
    CHECK(bytes[0x18] == 2);   // num_row_offsets low byte == 2
}

TEST(pdb_rows_start_on_four_byte_boundaries) {
    PdbPage page(PageType::Artists);
    CHECK(page.tryAddRow(std::vector<uint8_t>(13, 1)));
    CHECK(page.tryAddRow(std::vector<uint8_t>(7, 2)));
    auto bytes = page.finalize(1, 0);
    // Second row's offset (index slot 1, at len_page - 8) is the first's 13 padded to 16.
    CHECK(bytes[kPageSize - 8] == 16 && bytes[kPageSize - 7] == 0);
}

TEST(pdb_index_page) {
    auto page = buildIndexPage(1, PageType::Tracks, {2, 3});
    CHECK(page[0x1b] == kPageFlagsIndex);
    CHECK(page[0x0c] == 2);                                  // first data page
    CHECK(page[0x26] == 2 && page[0x38] == 2);               // two entries
    CHECK(page[0x3c] == (2 << 3) && page[0x40] == (3 << 3));
    CHECK(page[0x44] == 0xF8 && page[0x47] == 0x1F);         // the rest unused
    auto empty = buildIndexPage(5, PageType::Genres, {});
    CHECK(empty[0x0c] == 0xFF && empty[0x0f] == 0x03);       // no data pages
}

TEST(pdb_index_bytes_formula) {
    CHECK(pdbIndexBytes(0) == 0);
    CHECK(pdbIndexBytes(1) == 6);    // 4 + 2
    CHECK(pdbIndexBytes(11) == 26);  // matches a real page (free/used reconciles)
    CHECK(pdbIndexBytes(16) == 36);  // one full group
    CHECK(pdbIndexBytes(17) == 42);  // full group + 4 + 2
}

TEST(pdb_page_rejects_overflow) {
    PdbPage page(PageType::Tracks);
    // A single row too large to fit alongside the header + one row group.
    CHECK(!page.tryAddRow(std::vector<uint8_t>(kPageSize, 0)));
    CHECK(page.rowCount() == 0);
}

// --- exporter end-to-end (pipeline compiles + links files) ---
TEST(exporter_produces_pdb_plus_anlz_per_track) {
    ExportModel m;
    Track t; t.id = 0x100; t.file_path = "/Contents/a.flac";
    m.tracks.push_back(t);
    auto image = buildUsbImage(m);
    // export.pdb + the track's .DAT and .EXT.
    CHECK(image.files.size() == 3);
    CHECK(image.files[0].path == "/PIONEER/rekordbox/export.pdb");
    // A whole number of 4096-byte pages: the header, then per table an index
    // page and a spare, plus a data page for the two tables with rows (the
    // track and the colour palette): 1 + 20 * 2 + 2 = 43.
    CHECK(!image.files[0].bytes.empty());
    CHECK(image.files[0].bytes.size() % kPageSize == 0);
    CHECK(image.files[0].bytes.size() == 43 * kPageSize);
    // ANLZ paths were assigned and match the track record.
    CHECK(!m.tracks.empty());
    CHECK(image.files[1].path == anlzPathForTrack(0x100));
    CHECK(image.files[2].path == extPathFor(anlzPathForTrack(0x100)));
    CHECK(image.files[2].path.substr(image.files[2].path.size() - 4) == ".EXT");
}

int main() {
    for (auto& [name, fn] : obxtest::registry()) {
        std::printf("[ %s ]\n", name.c_str());
        fn();
    }
    const int f = obxtest::failures();
    std::printf("\n%s (%d failure%s)\n", f == 0 ? "PASS" : "FAIL", f, f == 1 ? "" : "s");
    return f == 0 ? 0 : 1;
}
