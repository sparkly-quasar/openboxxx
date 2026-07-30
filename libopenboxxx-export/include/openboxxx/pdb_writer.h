// SPDX-License-Identifier: GPL-2.0-or-later
//
// export.pdb writer. LITTLE-endian DeviceSQL page database.
// Spec: docs/research-findings.md §B.2. Byte layout follows crate-digger's
// rekordbox_pdb.ksy (EPL-1.0 spec, not copied code).
//
// This file scaffolds the two hardest, most format-specific pieces:
//   * PdbPage    -- a single 4096-byte page: forward heap + backward 16-row-group
//                   index. The page-fill bookkeeping is implemented here.
//   * PdbWriter  -- orchestrates the file header + per-table page chains.
//
// ⚠️ The exact row-group offset semantics and the track_row magic constants are
// best-effort until validated against a real rekordbox stick (verification
// tier 2). See docs/export-design.md "Open questions carried into Phase 0".
#ifndef OPENBOXXX_PDB_WRITER_H
#define OPENBOXXX_PDB_WRITER_H

#include <cstdint>
#include <vector>

#include "openboxxx/model.h"

namespace openboxxx {

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPageHeaderSize = 0x28;
constexpr uint32_t kRowGroupBytes = 0x24;  // 16 offsets(32) + present(2) + txn(2)
constexpr uint32_t kRowsPerGroup = 16;

// DeviceSQL page type ids (subset needed for the MVP). Values per §B.2.
enum class PageType : uint32_t {
    Tracks = 0, Genres = 1, Artists = 2, Albums = 3, Labels = 4, Keys = 5,
    Colors = 6, PlaylistTree = 7, PlaylistEntries = 8, Artwork = 13, Columns = 16,
};

// Builds one 4096-byte data page and manages the forward-heap / backward-index
// packing. Rows are pre-serialized byte blobs supplied by the table writers.
class PdbPage {
public:
    PdbPage(uint32_t page_index, PageType type) : page_index_(page_index), type_(type) {}

    // Attempt to add a pre-serialized row. Returns false (without mutating) if it
    // would collide the forward heap with the backward row index.
    bool tryAddRow(const std::vector<uint8_t>& row);

    std::size_t rowCount() const { return rows_.size(); }

    // Serialize to exactly kPageSize bytes. `next_page_index` links the chain.
    std::vector<uint8_t> finalize(uint32_t next_page_index) const;

private:
    std::size_t indexBytesFor(std::size_t num_rows) const {
        const std::size_t groups = (num_rows + kRowsPerGroup - 1) / kRowsPerGroup;
        return groups * kRowGroupBytes;
    }
    std::size_t heapBytes() const { return heap_used_; }

    uint32_t page_index_;
    PageType type_;
    std::size_t heap_used_ = 0;
    std::vector<std::vector<uint8_t>> rows_;
};

// Top-level: turn an ExportModel into export.pdb bytes.
// TODO(phase0): implement the track / lookup / playlist row serializers and the
// file header + table-pointer array. Currently returns an empty header-only stub
// so the pipeline compiles and can be wired end-to-end.
std::vector<uint8_t> buildExportPdb(const ExportModel& model);

}  // namespace openboxxx

#endif  // OPENBOXXX_PDB_WRITER_H
