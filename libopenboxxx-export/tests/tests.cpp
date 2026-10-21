// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.h"

#include <fstream>
#include <iterator>
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

// --- laid out like a real rekordbox export (tests/fixtures/rekordcrate-demo) ---
static std::vector<uint8_t> readFixture(const char* name) {
    std::ifstream f(std::string(OPENBOXXX_FIXTURES) + "/" + name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static uint32_t u32At(const std::vector<uint8_t>& d, std::size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 |
           uint32_t(d[o + 3]) << 24;
}

// A table's (empty_candidate, first page, last page) from the file header.
struct Span { uint32_t spare, first, last; };
static Span tableSpan(const std::vector<uint8_t>& d, PageType type) {
    const std::size_t o = 0x1C + 16 * std::size_t(type);
    CHECK(u32At(d, o) == uint32_t(type));
    return {u32At(d, o + 4), u32At(d, o + 8), u32At(d, o + 12)};
}

static ExportModel oneTrack() {
    ExportModel m;
    Track t; t.id = 1; t.title = "T"; t.file_path = "/Contents/A/B/t.mp3";
    m.tracks.push_back(t);
    return m;
}

TEST(pdb_browse_menu_is_rekordboxs) {
    // Without these tables a player has no categories to browse by, and an
    // XDJ-RX2 showed none of the tracks on a stick written without them.
    const std::vector<uint8_t> real = readFixture("rekordcrate-demo/export.pdb");
    CHECK(real.size() > 40 * kPageSize);
    if (real.size() <= 40 * kPageSize) return;
    const std::vector<uint8_t> ours = buildExportPdb(oneTrack());
    for (PageType type : {PageType::Columns, PageType::Unknown17, PageType::Unknown18}) {
        const Span r = tableSpan(real, type), o = tableSpan(ours, type);
        CHECK(r.last == r.first + 1);    // an index page, then one of rows
        CHECK(o.last == o.first + 1);
        // The rows, their index at the end of the page, and the counts and flags
        // in its header; not the page numbers or edit sequence.
        CHECK(std::equal(ours.begin() + o.last * kPageSize + 0x18,
                         ours.begin() + (o.last + 1) * kPageSize,
                         real.begin() + r.last * kPageSize + 0x18));
    }
}

TEST(pdb_tables_start_on_an_index_page_and_end_on_a_spare) {
    const std::vector<uint8_t> real = readFixture("rekordcrate-demo/export.pdb");
    const std::vector<uint8_t> ours = buildExportPdb(oneTrack());
    for (const std::vector<uint8_t>* d : {&real, &ours}) {
        if (d->size() < kPageSize) { CHECK(false); continue; }
        int empty = 0;
        for (uint32_t t = 0; t < 20; ++t) {
            const Span s = tableSpan(*d, PageType(t));
            CHECK((*d)[s.first * kPageSize + 0x1b] == kPageFlagsIndex);
            // the chain ends on the spare: an empty table's index page leads
            // straight to it, otherwise the last data page does
            CHECK(u32At(*d, s.last * kPageSize + 0x0c) == s.spare);
            if (s.first == s.last) ++empty;
        }
        CHECK(empty > 0);
    }
}

TEST(pdb_file_header_field_0x10_is_rekordboxs) {
    const std::vector<uint8_t> real = readFixture("rekordcrate-demo/export.pdb");
    const std::vector<uint8_t> ours = buildExportPdb(oneTrack());
    CHECK(real.size() > 0x14 && u32At(real, 0x10) == 5);
    CHECK(u32At(ours, 0x10) == 5);
}

TEST(pdb_fresh_pages_look_like_rekordboxs_fresh_pages) {
    // In real exports a page no row was ever deleted from is flagged 0x24 and
    // counts its rows as one transaction, in its header and per row group.
    PdbPage page(PageType::Genres);
    for (uint32_t i = 1; i <= 3; ++i) {
        ByteBuffer r; r.putU32LE(i); putDeviceSqlString(r, "G");
        CHECK(page.tryAddRow(r.bytes()));
    }
    const auto bytes = page.finalize(5, 6);
    CHECK(bytes[0x1b] == 0x24);
    CHECK(bytes[0x20] == 3 && bytes[0x21] == 0);
    CHECK(bytes[kPageSize - 4] == 0x07 && bytes[kPageSize - 2] == 0x07);
    // rows start on 4-byte boundaries: 6-byte rows take 8
    CHECK(bytes[kPageSize - 6] == 0 && bytes[kPageSize - 8] == 8);
}

// --- exporter end-to-end (pipeline compiles + links files) ---
TEST(exporter_produces_pdb_plus_anlz_per_track) {
    ExportModel m;
    Track t; t.id = 0x100; t.file_path = "/Contents/a.flac";
    m.tracks.push_back(t);
    auto image = buildUsbImage(m);
    // export.pdb + one ANLZ.
    CHECK(image.files.size() == 2);
    CHECK(image.files[0].path == "/PIONEER/rekordbox/export.pdb");
    // PDB is now real: non-empty and a whole number of 4096-byte pages: page 0
    // header + an index page and a spare page per table (20 x 2) + data pages
    // for the tracks, colours and the three browse-menu tables = 46 pages.
    CHECK(!image.files[0].bytes.empty());
    CHECK(image.files[0].bytes.size() % kPageSize == 0);
    CHECK(image.files[0].bytes.size() == 46 * kPageSize);
    // ANLZ path was assigned and matches the track record.
    CHECK(!m.tracks.empty());
    CHECK(image.files[1].path == anlzPathForTrack(0x100));
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
