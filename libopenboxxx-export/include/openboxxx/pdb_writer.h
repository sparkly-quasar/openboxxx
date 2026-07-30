// SPDX-License-Identifier: GPL-2.0-or-later
//
// export.pdb writer. LITTLE-endian DeviceSQL page database.
// Spec: docs/research-findings.md §B.2. Byte layout follows crate-digger's
// rekordbox_pdb.ksy (EPL-1.0 spec), cross-checked field-by-field against a real
// rekordbox export via the Kaitai read-back parser (verification tier 1/2).
//
//   * PdbPage    -- a single 4096-byte page: forward heap from 0x28 + backward
//                   16-row-group index (base = len_page - group*36; row offset
//                   at base-(6+2*i); present flags at base-4).
//   * buildExportPdb -- file header + table-pointer array + per-table page chains.
//
// Observed constants are copied from a real (rekordbox 5/6) export and may be
// version-sensitive; see docs/export-design.md open questions. Row-level fields
// that only rekordbox itself consumes (index_shift, the track_row "magic" u-
// fields) are set to observed/neutral values and refined as hardware testing
// demands.
#ifndef OPENBOXXX_PDB_WRITER_H
#define OPENBOXXX_PDB_WRITER_H

#include <cstdint>
#include <vector>

#include "openboxxx/model.h"

namespace openboxxx {

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPageHeaderSize = 0x28;  // heap starts here (== heap_pos)
constexpr uint32_t kRowsPerGroup = 16;
constexpr uint8_t kPageFlagsData = 0x34;    // is_data_page == (flags & 0x40)==0

// DeviceSQL page type ids. Full standard set rekordbox emits (values per the
// crate-digger page_type enum); we populate some and emit the rest empty.
enum class PageType : uint32_t {
    Tracks = 0, Genres = 1, Artists = 2, Albums = 3, Labels = 4, Keys = 5,
    Colors = 6, PlaylistTree = 7, PlaylistEntries = 8, Unknown9 = 9, Unknown10 = 10,
    HistoryPlaylists = 11, HistoryEntries = 12, Artwork = 13, Unknown14 = 14,
    Unknown15 = 15, Columns = 16, Unknown17 = 17, Unknown18 = 18, History = 19,
};

// Index bytes consumed by `n` contiguous row offsets (no gaps): each 16-row
// group costs 36 bytes; a partial final group costs 4 + 2*rem. Verified against
// a real page (n=11 -> 26 bytes).
uint32_t pdbIndexBytes(uint32_t n);

// Builds one 4096-byte data page, packing pre-serialized rows (forward heap +
// backward row index). The page's own index is supplied at finalize time (it is
// only known after all tables have been laid out).
class PdbPage {
public:
    explicit PdbPage(PageType type) : type_(type) {}

    // Attempt to add a pre-serialized row; false (no mutation) if it won't fit.
    bool tryAddRow(const std::vector<uint8_t>& row);
    std::size_t rowCount() const { return rows_.size(); }

    std::vector<uint8_t> finalize(uint32_t page_index, uint32_t next_page_index) const;

private:
    PageType type_;
    std::size_t heap_used_ = 0;
    std::vector<std::vector<uint8_t>> rows_;
};

// Turn an ExportModel into export.pdb bytes: tracks + referenced lookup tables
// (artists/albums/genres/keys) + playlist_tree/playlist_entries.
std::vector<uint8_t> buildExportPdb(const ExportModel& model);

}  // namespace openboxxx

#endif  // OPENBOXXX_PDB_WRITER_H
