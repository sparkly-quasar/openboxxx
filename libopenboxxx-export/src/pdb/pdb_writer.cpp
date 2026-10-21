// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/pdb_writer.h"

#include <algorithm>
#include <map>
#include <string>

#include "openboxxx/byteio.h"
#include "openboxxx/device_sql_string.h"
#include "openboxxx/mapping.h"

namespace openboxxx {
namespace {

// Observed track_row constants from a real rekordbox 5/6 export (may be
// version-sensitive). See docs/export-design.md.
constexpr uint32_t kTrackBitmask = 0xC0700;
constexpr uint16_t kTrackU7 = 44990;
constexpr uint16_t kTrackU8 = 54115;
constexpr uint16_t kTrackU26 = 41;
constexpr uint16_t kTrackU29 = 11;
constexpr uint16_t kTrackU30 = 3;
constexpr uint16_t kTrackStringCount = 21;

std::string baseName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// --- row serializers (return the raw row bytes; strings via device_sql_string) ---

// rekordbox's index_shift steps by 0x20 a row in each table.
uint16_t indexShift(std::size_t row) { return uint16_t(row * 0x20); }

std::vector<uint8_t> buildTrackRow(const Track& t, uint32_t artist_id, uint32_t album_id,
                                   uint32_t genre_id, uint32_t key_id, uint8_t color_id,
                                   uint16_t index_shift) {
    ByteBuffer r;
    r.putU16LE(0x24);                 // subtype (0x04 bit -> 16-bit string offsets)
    r.putU16LE(index_shift);
    r.putU32LE(kTrackBitmask);
    r.putU32LE(uint32_t(t.sample_rate));
    r.putU32LE(0);                    // composer_id
    r.putU32LE(uint32_t(t.file_size));
    r.putU32LE(0);                    // unknown id (u6)
    r.putU16LE(kTrackU7);
    r.putU16LE(kTrackU8);
    r.putU32LE(0);                    // artwork_id
    r.putU32LE(key_id);
    r.putU32LE(0);                    // original_artist_id
    r.putU32LE(0);                    // label_id
    r.putU32LE(0);                    // remixer_id
    r.putU32LE(uint32_t(t.bitrate));
    r.putU32LE(uint32_t(t.track_number));
    const uint32_t tempo = t.beatgrid.empty() ? 0 : t.beatgrid.front().tempo_x100;
    r.putU32LE(tempo);
    r.putU32LE(genre_id);
    r.putU32LE(album_id);
    r.putU32LE(artist_id);
    r.putU32LE(t.id);
    r.putU16LE(0);                    // disc_number
    r.putU16LE(0);                    // play_count
    r.putU16LE(uint16_t(t.year));
    r.putU16LE(16);                   // sample_depth
    r.putU16LE(uint16_t(t.duration_s));
    r.putU16LE(kTrackU26);
    r.putU8(color_id);
    r.putU8(uint8_t(t.rating));
    r.putU16LE(kTrackU29);
    r.putU16LE(kTrackU30);

    const std::size_t ofs_table = r.size();
    for (int i = 0; i < kTrackStringCount; ++i) r.putU16LE(0);  // placeholders

    // Fill the meaningful strings; the rest stay empty (0x03).
    std::string strs[kTrackStringCount];
    strs[14] = t.analyze_path;
    strs[16] = t.comment;
    strs[17] = t.title;
    strs[19] = baseName(t.file_path);
    strs[20] = t.file_path;

    for (int i = 0; i < kTrackStringCount; ++i) {
        r.patchU16LE(ofs_table + 2 * i, uint16_t(r.size()));  // ofs relative to row_base
        putDeviceSqlString(r, strs[i]);
    }
    return r.bytes();
}

std::vector<uint8_t> buildArtistRow(uint32_t id, const std::string& name) {
    ByteBuffer r;
    r.putU16LE(0x60);                 // subtype (near, 8-bit name offset)
    r.putU16LE(indexShift(id - 1));   // ids run 1, 2, 3... in row order
    r.putU32LE(id);
    r.putU8(0x03);                    // unknown
    r.putU8(0x0A);                    // ofs_name_near: name starts at row_base+10
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildAlbumRow(uint32_t id, uint32_t artist_id, const std::string& name) {
    ByteBuffer r;
    r.putU16LE(0x80);                 // subtype (near)
    r.putU16LE(indexShift(id - 1));   // ids run 1, 2, 3... in row order
    r.putU32LE(0);                    // unknown
    r.putU32LE(artist_id);
    r.putU32LE(id);
    r.putU32LE(0);                    // unknown
    r.putU8(0x03);                    // unknown
    r.putU8(0x16);                    // ofs_name_near: name at row_base+22
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildIdNameRow(uint32_t id, const std::string& name) {  // genre
    ByteBuffer r;
    r.putU32LE(id);
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildKeyRow(uint32_t id, const std::string& name) {
    ByteBuffer r;
    r.putU32LE(id);
    r.putU32LE(id);                   // id2
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildColorRow(uint16_t id, const std::string& name) {
    ByteBuffer r;
    r.putZeros(5);                    // unknown prefix (rekordbox-internal)
    r.putU16LE(id);
    r.putU8(0);                       // unknown
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildPlaylistTreeRow(const Playlist& p, uint32_t sort_order) {
    ByteBuffer r;
    r.putU32LE(p.parent_id);
    r.putU32LE(0);                    // unknown
    r.putU32LE(sort_order);
    r.putU32LE(p.id);
    r.putU32LE(p.is_folder ? 1u : 0u);
    putDeviceSqlString(r, p.name);
    return r.bytes();
}

std::vector<uint8_t> buildPlaylistEntryRow(uint32_t entry_index, uint32_t track_id,
                                           uint32_t playlist_id) {
    ByteBuffer r;
    r.putU32LE(entry_index);
    r.putU32LE(track_id);
    r.putU32LE(playlist_id);
    return r.bytes();
}

// The browse menu, from rekordbox's export of its demo tracks
// (tests/fixtures/rekordcrate-demo). Columns: the categories a player can
// browse by, the same in every rekordbox export.
struct ColumnDef { uint16_t id; uint16_t field; const char* name; };
const ColumnDef kMenuColumns[] = {
    {1, 0x80, "GENRE"}, {2, 0x81, "ARTIST"}, {3, 0x82, "ALBUM"}, {4, 0x83, "TRACK"},
    {5, 0x85, "BPM"}, {6, 0x86, "RATING"}, {7, 0x87, "YEAR"}, {8, 0x88, "REMIXER"},
    {9, 0x89, "LABEL"}, {10, 0x8A, "ORIGINAL ARTIST"}, {11, 0x8B, "KEY"},
    {12, 0x8D, "CUE"}, {13, 0x8E, "COLOR"}, {14, 0x92, "TIME"}, {15, 0x93, "BITRATE"},
    {16, 0x94, "FILE NAME"}, {17, 0x84, "PLAYLIST"}, {18, 0x98, "HOT CUE BANK"},
    {19, 0x95, "HISTORY"}, {20, 0x91, "SEARCH"}, {21, 0x96, "COMMENTS"},
    {22, 0x8C, "DATE ADDED"}, {23, 0x97, "DJ PLAY COUNT"}, {24, 0x90, "FOLDER"},
    {25, 0xA1, "DEFAULT"}, {26, 0xA2, "ALPHABET"}, {27, 0xAA, "MATCHING"},
};
// Unknown17 and Unknown18: which categories the menu shows and in what order.
// rekordbox's Preferences can change these; these are its demo export's.
const uint16_t kMenu17[][4] = {
    {0x01, 0x01, 0x163, 0x00}, {0x05, 0x06, 0x105, 0x00}, {0x06, 0x07, 0x163, 0x00},
    {0x07, 0x08, 0x163, 0x00}, {0x08, 0x09, 0x163, 0x00}, {0x09, 0x0a, 0x163, 0x00},
    {0x0a, 0x0b, 0x163, 0x00}, {0x0d, 0x0f, 0x163, 0x00}, {0x0e, 0x13, 0x104, 0x00},
    {0x0f, 0x14, 0x106, 0x00}, {0x10, 0x15, 0x163, 0x00}, {0x12, 0x17, 0x163, 0x00},
    {0x02, 0x02, 0x02, 0x01}, {0x03, 0x03, 0x03, 0x02}, {0x04, 0x04, 0x01, 0x03},
    {0x0b, 0x0c, 0x63, 0x04}, {0x11, 0x05, 0x63, 0x05}, {0x13, 0x16, 0x63, 0x06},
    {0x14, 0x12, 0x63, 0x07}, {0x1b, 0x1a, 0x263, 0x08}, {0x18, 0x11, 0x63, 0x09},
    {0x16, 0x1b, 0x63, 0x0a},
};
const uint16_t kMenu18[][4] = {
    {0x01, 0x06, 0x01, 0x00}, {0x15, 0x07, 0x01, 0x00}, {0x0e, 0x08, 0x01, 0x00},
    {0x08, 0x09, 0x01, 0x00}, {0x09, 0x0a, 0x01, 0x00}, {0x0a, 0x0b, 0x01, 0x00},
    {0x0f, 0x0d, 0x01, 0x00}, {0x0d, 0x0f, 0x01, 0x00}, {0x17, 0x10, 0x01, 0x00},
    {0x16, 0x11, 0x01, 0x00}, {0x19, 0x00, 0x100, 0x00}, {0x1a, 0x01, 0x200, 0x00},
    {0x02, 0x02, 0x300, 0x00}, {0x03, 0x03, 0x400, 0x00}, {0x05, 0x04, 0x500, 0x00},
    {0x06, 0x05, 0x600, 0x00}, {0x0b, 0x0c, 0x700, 0x00},
};

std::vector<uint8_t> buildColumnRow(const ColumnDef& c) {
    ByteBuffer r;
    r.putU16LE(c.id);
    r.putU16LE(c.field);
    // The name is marked off by U+FFFA and U+FFFB, which also makes it UTF-16.
    putDeviceSqlString(r, std::string("\xEF\xBF\xBA") + c.name + "\xEF\xBF\xBB");
    return r.bytes();
}

std::vector<uint8_t> buildMenuRow(const uint16_t (&values)[4]) {
    ByteBuffer r;
    for (uint16_t v : values) r.putU16LE(v);
    return r.bytes();
}

struct PageSpan { uint32_t spare; uint32_t first; uint32_t last; };

}  // namespace

uint32_t pdbIndexBytes(uint32_t n) {
    const uint32_t full = n / kRowsPerGroup;
    const uint32_t rem = n % kRowsPerGroup;
    return full * 36 + (rem ? (4 + 2 * rem) : 0);
}

bool PdbPage::tryAddRow(const std::vector<uint8_t>& row) {
    std::vector<uint8_t> padded = row;          // rows start on 4-byte boundaries
    padded.resize((row.size() + 3) & ~std::size_t(3), 0);
    const std::size_t new_heap = heap_used_ + padded.size();
    const uint32_t new_index = pdbIndexBytes(uint32_t(rows_.size() + 1));
    if (kPageHeaderSize + new_heap + new_index > kPageSize) return false;
    rows_.push_back(std::move(padded));
    heap_used_ = new_heap;
    return true;
}

std::vector<uint8_t> buildIndexPage(uint32_t page_index, PageType type,
                                    const std::vector<uint32_t>& data_pages, uint32_t spare) {
    std::vector<uint8_t> page(kPageSize, 0);
    const uint32_t first = data_pages.empty() ? kNoPage : data_pages.front();
    const uint32_t n = uint32_t(data_pages.size());
    auto put = [&](std::size_t o, const ByteBuffer& b) {
        std::copy(b.bytes().begin(), b.bytes().end(), page.begin() + o);
    };
    ByteBuffer head;
    head.putU32LE(page_index);
    head.putU32LE(uint32_t(type));
    head.putU32LE(data_pages.empty() ? spare : first);
    put(0x04, head);
    page[0x1b] = kPageFlagsIndex;
    ByteBuffer counts;
    counts.putU16LE(0x1FFF);
    counts.putU16LE(0x1FFF);
    counts.putU16LE(uint16_t(kIndexEntries));
    counts.putU16LE(uint16_t(n));
    put(0x20, counts);
    ByteBuffer body;
    body.putU32LE(page_index);
    body.putU32LE(first);
    body.putU32LE(kNoPage);
    body.putU32LE(0);
    body.putU16LE(uint16_t(n));
    body.putU16LE(0x1FFF);
    for (uint32_t p : data_pages) body.putU32LE(p << 3);
    for (uint32_t i = n; i < kIndexEntries; ++i) body.putU32LE(kEmptyIndexEntry);
    put(kPageHeaderSize, body);
    return page;
}

std::vector<uint8_t> PdbPage::finalize(uint32_t page_index, uint32_t next_page_index) const {
    std::vector<uint8_t> page(kPageSize, 0);
    auto wU16 = [&](std::size_t o, uint16_t v) {
        page[o] = uint8_t(v); page[o + 1] = uint8_t(v >> 8);
    };
    auto wU32 = [&](std::size_t o, uint32_t v) {
        page[o] = uint8_t(v); page[o + 1] = uint8_t(v >> 8);
        page[o + 2] = uint8_t(v >> 16); page[o + 3] = uint8_t(v >> 24);
    };

    // Forward heap from 0x28; record each row's offset relative to heap start.
    std::vector<uint16_t> ofs(rows_.size());
    std::size_t pos = kPageHeaderSize;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        ofs[i] = uint16_t(pos - kPageHeaderSize);
        std::copy(rows_[i].begin(), rows_[i].end(), page.begin() + pos);
        pos += rows_[i].size();
    }
    const uint32_t n = uint32_t(rows_.size());

    // Backward row index: group g at base = len_page - g*36; offset for row's
    // local slot at base-(6+2*local); present flags at base-4.
    for (uint32_t r = 0; r < n; ++r) {
        const uint32_t g = r / kRowsPerGroup, local = r % kRowsPerGroup;
        const std::size_t base = kPageSize - g * 36;
        wU16(base - (6 + 2 * local), ofs[r]);
    }
    const uint32_t ngroups = n == 0 ? 0 : (n - 1) / kRowsPerGroup + 1;
    for (uint32_t g = 0; g < ngroups; ++g) {
        const uint32_t cnt = std::min<uint32_t>(kRowsPerGroup, n - g * kRowsPerGroup);
        const uint16_t present = cnt >= 16 ? 0xFFFF : uint16_t((1u << cnt) - 1);
        wU16((kPageSize - g * 36) - 4, present);
        wU16((kPageSize - g * 36) - 2, present);   // all came in the page's transaction
    }

    // Header.
    wU32(0x04, page_index);
    wU32(0x08, uint32_t(type_));
    wU32(0x0c, next_page_index);
    // 0x10 sequence, 0x14 gap left 0.
    const uint32_t packed = (n & 0x1FFF) | ((n & 0x7FF) << 13);  // 24-bit field
    page[0x18] = uint8_t(packed);
    page[0x19] = uint8_t(packed >> 8);
    page[0x1a] = uint8_t(packed >> 16);
    page[0x1b] = kPageFlagsData;
    const uint32_t used = uint32_t(heap_used_);
    const uint32_t free = kPageSize - kPageHeaderSize - used - pdbIndexBytes(n);
    wU16(0x1c, uint16_t(free));
    wU16(0x1e, uint16_t(used));
    wU16(0x20, uint16_t(n));          // every row added in one transaction
    // 0x22..0x27 transaction index + unknown fields left 0.
    return page;
}

std::vector<uint8_t> buildExportPdb(const ExportModel& model) {
    // --- assign lookup ids from unique, non-empty values ---
    std::map<std::string, uint32_t> artist_ids, album_ids, genre_ids, key_ids;
    auto intern = [](std::map<std::string, uint32_t>& m, const std::string& s) -> uint32_t {
        if (s.empty()) return 0;
        auto it = m.find(s);
        if (it != m.end()) return it->second;
        const uint32_t id = uint32_t(m.size()) + 1;
        m.emplace(s, id);
        return id;
    };

    std::vector<std::vector<uint8_t>> track_rows;
    for (const Track& t : model.tracks) {
        const uint16_t shift = indexShift(track_rows.size());
        const uint32_t aid = intern(artist_ids, t.artist);
        const uint32_t alb = intern(album_ids, t.album);
        const uint32_t gid = intern(genre_ids, t.genre);
        const uint32_t kid = intern(key_ids, t.key);
        const uint8_t cid = t.color ? rekordboxColorId(*t.color) : 0;
        track_rows.push_back(buildTrackRow(t, aid, alb, gid, kid, cid, shift));
    }

    // Colours: the fixed 8-entry rekordbox palette (ids 1..8), always emitted so
    // any track colour_id resolves. Names match a real export.
    static const char* const kColorNames[8] = {
        "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple"};
    std::vector<std::vector<uint8_t>> color_rows;
    for (uint16_t i = 0; i < 8; ++i) color_rows.push_back(buildColorRow(i + 1, kColorNames[i]));

    auto lookupRows = [](const std::map<std::string, uint32_t>& m,
                         std::vector<uint8_t> (*fn)(uint32_t, const std::string&)) {
        std::vector<std::pair<uint32_t, std::string>> sorted;
        for (const auto& kv : m) sorted.push_back({kv.second, kv.first});
        std::sort(sorted.begin(), sorted.end());
        std::vector<std::vector<uint8_t>> rows;
        for (const auto& p : sorted) rows.push_back(fn(p.first, p.second));
        return rows;
    };

    std::vector<std::vector<uint8_t>> artist_rows = lookupRows(artist_ids, buildArtistRow);
    std::vector<std::vector<uint8_t>> genre_rows = lookupRows(genre_ids, buildIdNameRow);
    std::vector<std::vector<uint8_t>> key_rows = lookupRows(key_ids, buildKeyRow);

    // Albums need their artist id; rebuild directly.
    std::vector<std::vector<uint8_t>> album_rows;
    {
        std::vector<std::pair<uint32_t, std::string>> sorted;
        for (const auto& kv : album_ids) sorted.push_back({kv.second, kv.first});
        std::sort(sorted.begin(), sorted.end());
        // album->artist link: first track that has this album.
        std::map<std::string, uint32_t> album_artist;
        for (const Track& t : model.tracks) {
            if (!t.album.empty() && album_artist.find(t.album) == album_artist.end()) {
                auto it = artist_ids.find(t.artist);
                album_artist[t.album] = it == artist_ids.end() ? 0 : it->second;
            }
        }
        for (const auto& p : sorted)
            album_rows.push_back(buildAlbumRow(p.first, album_artist[p.second], p.second));
    }

    // Playlists + entries.
    std::vector<std::vector<uint8_t>> pl_tree_rows, pl_entry_rows;
    uint32_t sort_order = 0;
    for (const Playlist& p : model.playlists) {
        pl_tree_rows.push_back(buildPlaylistTreeRow(p, sort_order++));
        uint32_t entry_index = 1;
        for (uint32_t tid : p.track_ids)
            pl_entry_rows.push_back(buildPlaylistEntryRow(entry_index++, tid, p.id));
    }

    // The browse menu.
    std::vector<std::vector<uint8_t>> column_rows, menu17_rows, menu18_rows;
    for (const ColumnDef& c : kMenuColumns) column_rows.push_back(buildColumnRow(c));
    for (const auto& v : kMenu17) menu17_rows.push_back(buildMenuRow(v));
    for (const auto& v : kMenu18) menu18_rows.push_back(buildMenuRow(v));

    // --- lay out pages (page 0 = file header; tables start at page 1) ---
    // Emit the full standard table set in rekordbox's order. Each table is an
    // index page, its data pages (none for an empty table), then a blank spare.
    static const std::vector<std::vector<uint8_t>> empty;
    struct Tbl { PageType type; const std::vector<std::vector<uint8_t>>* rows; };
    const std::vector<Tbl> tables = {
        {PageType::Tracks, &track_rows},
        {PageType::Genres, &genre_rows},
        {PageType::Artists, &artist_rows},
        {PageType::Albums, &album_rows},
        {PageType::Labels, &empty},
        {PageType::Keys, &key_rows},
        {PageType::Colors, &color_rows},
        {PageType::PlaylistTree, &pl_tree_rows},
        {PageType::PlaylistEntries, &pl_entry_rows},
        {PageType::Unknown9, &empty},
        {PageType::Unknown10, &empty},
        {PageType::HistoryPlaylists, &empty},
        {PageType::HistoryEntries, &empty},
        {PageType::Artwork, &empty},
        {PageType::Unknown14, &empty},
        {PageType::Unknown15, &empty},
        {PageType::Columns, &column_rows},
        {PageType::Unknown17, &menu17_rows},
        {PageType::Unknown18, &menu18_rows},
        {PageType::History, &empty},
    };

    std::vector<std::vector<uint8_t>> body_pages;  // pages 1..N
    std::vector<PageSpan> spans;
    uint32_t next_index = 1;
    for (const Tbl& tb : tables) {
        std::vector<PdbPage> chunk;
        for (const auto& row : *tb.rows) {
            if (chunk.empty() || !chunk.back().tryAddRow(row)) {
                chunk.emplace_back(tb.type);
                chunk.back().tryAddRow(row);  // a single row always fits an empty page
            }
        }
        const uint32_t index = next_index;
        std::vector<uint32_t> data;
        for (std::size_t i = 0; i < chunk.size(); ++i) data.push_back(index + 1 + uint32_t(i));
        const uint32_t spare = index + 1 + uint32_t(chunk.size());
        body_pages.push_back(buildIndexPage(index, tb.type, data, spare));
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            const uint32_t next = i + 1 < chunk.size() ? data[i + 1] : spare;
            body_pages.push_back(chunk[i].finalize(data[i], next));
        }
        body_pages.emplace_back(kPageSize, 0);  // the empty candidate
        spans.push_back({spare, index, data.empty() ? index : data.back()});
        next_index = spare + 1;
    }

    const uint32_t total_pages = next_index;  // page 0 header + body pages

    // --- file header (page 0) ---
    ByteBuffer hdr;
    hdr.putU32LE(0);                       // gap
    hdr.putU32LE(kPageSize);               // len_page
    hdr.putU32LE(uint32_t(tables.size())); // num_tables
    hdr.putU32LE(total_pages);             // next_unused_page
    // Unknown, but 5 in real exports, and an XDJ-RX2 rewrote a 0 here to 5 when
    // it read the stick. Deep-Symmetry/crate-digger#46 reports it must be 5.
    hdr.putU32LE(5);
    hdr.putU32LE(1);                       // sequence
    hdr.putU32LE(0);                       // 4-byte gap
    for (std::size_t i = 0; i < tables.size(); ++i) {
        hdr.putU32LE(uint32_t(tables[i].type));
        hdr.putU32LE(spans[i].spare);      // empty_candidate
        hdr.putU32LE(spans[i].first);
        hdr.putU32LE(spans[i].last);
    }

    // --- assemble the file: page 0 (header, padded) then body pages ---
    std::vector<uint8_t> out;
    out.reserve(total_pages * kPageSize);
    out.insert(out.end(), hdr.bytes().begin(), hdr.bytes().end());
    out.resize(kPageSize, 0);              // pad page 0
    for (const auto& pg : body_pages) out.insert(out.end(), pg.begin(), pg.end());
    return out;
}

}  // namespace openboxxx
