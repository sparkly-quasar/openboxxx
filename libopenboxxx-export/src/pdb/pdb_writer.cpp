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

std::vector<uint8_t> buildTrackRow(const Track& t, uint32_t artist_id, uint32_t album_id,
                                   uint32_t genre_id, uint32_t key_id, uint8_t color_id) {
    ByteBuffer r;
    r.putU16LE(0x24);                 // subtype (0x04 bit -> 16-bit string offsets)
    r.putU16LE(0);                    // index_shift (rekordbox-internal; neutral 0)
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
    r.putU16LE(0);                    // index_shift
    r.putU32LE(id);
    r.putU8(0x03);                    // unknown
    r.putU8(0x0A);                    // ofs_name_near: name starts at row_base+10
    putDeviceSqlString(r, name);
    return r.bytes();
}

std::vector<uint8_t> buildAlbumRow(uint32_t id, uint32_t artist_id, const std::string& name) {
    ByteBuffer r;
    r.putU16LE(0x80);                 // subtype (near)
    r.putU16LE(0);                    // index_shift
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

struct PageSpan { uint32_t first; uint32_t last; };

}  // namespace

uint32_t pdbIndexBytes(uint32_t n) {
    const uint32_t full = n / kRowsPerGroup;
    const uint32_t rem = n % kRowsPerGroup;
    return full * 36 + (rem ? (4 + 2 * rem) : 0);
}

bool PdbPage::tryAddRow(const std::vector<uint8_t>& row) {
    const std::size_t new_heap = heap_used_ + row.size();
    const uint32_t new_index = pdbIndexBytes(uint32_t(rows_.size() + 1));
    if (kPageHeaderSize + new_heap + new_index > kPageSize) return false;
    rows_.push_back(row);
    heap_used_ = new_heap;
    return true;
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
    // 0x20..0x27 transaction + unknown fields left 0.
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
        const uint32_t aid = intern(artist_ids, t.artist);
        const uint32_t alb = intern(album_ids, t.album);
        const uint32_t gid = intern(genre_ids, t.genre);
        const uint32_t kid = intern(key_ids, t.key);
        const uint8_t cid = t.color ? rekordboxColorId(*t.color) : 0;
        track_rows.push_back(buildTrackRow(t, aid, alb, gid, kid, cid));
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

    // --- lay out pages (page 0 = file header; tables start at page 1) ---
    // Emit the full standard table set in rekordbox's order; unpopulated tables
    // get a single empty data page so their pointers still resolve.
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
        {PageType::Columns, &empty},
        {PageType::Unknown17, &empty},
        {PageType::Unknown18, &empty},
        {PageType::History, &empty},
    };

    std::vector<std::vector<uint8_t>> body_pages;  // pages 1..N
    std::vector<PageSpan> spans;
    uint32_t next_index = 1;
    for (const Tbl& tb : tables) {
        // Chunk rows into PdbPages (at least one page, even if empty).
        std::vector<PdbPage> chunk;
        chunk.emplace_back(tb.type);
        for (const auto& row : *tb.rows) {
            if (!chunk.back().tryAddRow(row)) {
                chunk.emplace_back(tb.type);
                chunk.back().tryAddRow(row);  // a single row always fits an empty page
            }
        }
        const uint32_t first = next_index;
        const uint32_t last = next_index + uint32_t(chunk.size()) - 1;
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            const uint32_t idx = first + uint32_t(i);
            const uint32_t next = (idx == last) ? 0 : idx + 1;
            body_pages.push_back(chunk[i].finalize(idx, next));
        }
        spans.push_back({first, last});
        next_index = last + 1;
    }

    const uint32_t total_pages = next_index;  // page 0 header + body pages

    // --- file header (page 0) ---
    ByteBuffer hdr;
    hdr.putU32LE(0);                       // gap
    hdr.putU32LE(kPageSize);               // len_page
    hdr.putU32LE(uint32_t(tables.size())); // num_tables
    hdr.putU32LE(total_pages);             // next_unused_page
    hdr.putU32LE(0);                       // unknown
    hdr.putU32LE(1);                       // sequence
    hdr.putU32LE(0);                       // 4-byte gap
    for (std::size_t i = 0; i < tables.size(); ++i) {
        hdr.putU32LE(uint32_t(tables[i].type));
        hdr.putU32LE(0);                   // empty_candidate (TODO: real spare page)
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
