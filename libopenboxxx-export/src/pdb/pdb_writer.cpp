// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/pdb_writer.h"

#include "openboxxx/byteio.h"

namespace openboxxx {

bool PdbPage::tryAddRow(const std::vector<uint8_t>& row) {
    const std::size_t new_heap = heap_used_ + row.size();
    const std::size_t new_index = indexBytesFor(rows_.size() + 1);
    // Forward heap starts after the header; backward index grows from page end.
    if (kPageHeaderSize + new_heap + new_index > kPageSize) {
        return false;  // caller should start a new page
    }
    rows_.push_back(row);
    heap_used_ = new_heap;
    return true;
}

std::vector<uint8_t> PdbPage::finalize(uint32_t next_page_index) const {
    ByteBuffer buf;

    // --- page header (0x28 bytes) ---
    buf.putU32LE(0);                       // 0x00 zero gap
    buf.putU32LE(page_index_);             // 0x04 page_index (== own index)
    buf.putU32LE(uint32_t(type_));         // 0x08 type
    buf.putU32LE(next_page_index);         // 0x0c next_page
    buf.putU32LE(0);                       // 0x10 sequence (fresh export)
    buf.putU32LE(0);                       // 0x14 unknown
    // 0x18: num_row_offsets (13 bits) + num_rows (11 bits) packed in a u4.
    // For a from-scratch page these are equal (no deleted rows).
    const uint32_t n = uint32_t(rows_.size());
    buf.putU32LE((n & 0x1FFF) | ((n & 0x7FF) << 13));
    buf.putU8(0x24);                       // 0x1b page_flags (data page; bit 0x40 clear)
    // free_size / used_size describe heap occupancy.
    const uint32_t used = uint32_t(heap_used_);
    const uint32_t free = kPageSize - kPageHeaderSize - used - uint32_t(indexBytesFor(n));
    buf.putU16LE(uint16_t(free));          // 0x1c free_size
    buf.putU16LE(uint16_t(used));          // 0x1e used_size
    buf.putU16LE(0);                       // 0x20 transaction_row_count
    buf.putU16LE(0);                       // 0x22 transaction_row_index
    // header is 0x24 so far; pad to 0x28 heap start.
    buf.putU16LE(0);                       // 0x24 unknown
    buf.putU16LE(0);                       // 0x26 unknown

    // --- forward heap: rows in order, recording each row's start offset ---
    std::vector<uint16_t> row_offsets;
    row_offsets.reserve(rows_.size());
    for (const auto& row : rows_) {
        row_offsets.push_back(uint16_t(buf.size() - kPageHeaderSize));
        buf.putBytes(row);
    }

    // --- pad heap out to where the backward index begins ---
    const std::size_t index_bytes = indexBytesFor(rows_.size());
    const std::size_t index_start = kPageSize - index_bytes;
    while (buf.size() < index_start) buf.putU8(0);

    // --- backward row-group index ---
    // ASSUMPTION (to validate on a real stick): groups fill from the page end;
    // group g occupies [kPageSize - (g+1)*0x24, kPageSize - g*0x24). Within a
    // group, offsets are laid low-to-high address for rows high-to-low index.
    std::vector<uint8_t> index(index_bytes, 0);
    for (std::size_t g = 0; g * kRowsPerGroup < rows_.size(); ++g) {
        const std::size_t gend = index_bytes - g * kRowGroupBytes;  // relative to index_start
        uint16_t present = 0;
        for (std::size_t j = 0; j < kRowsPerGroup; ++j) {
            const std::size_t r = g * kRowsPerGroup + j;
            if (r >= rows_.size()) break;
            present |= uint16_t(1u << j);
            const std::size_t at = gend - 6 - 2 * j;  // offset slot for row j
            index[at] = uint8_t(row_offsets[r]);
            index[at + 1] = uint8_t(row_offsets[r] >> 8);
        }
        const std::size_t pf = gend - 4;  // row_present_flags
        index[pf] = uint8_t(present);
        index[pf + 1] = uint8_t(present >> 8);
        // gend-2 .. gend : transaction_row_flags left 0.
    }
    buf.putBytes(index);

    return buf.bytes();  // exactly kPageSize
}

std::vector<uint8_t> buildExportPdb(const ExportModel& /*model*/) {
    // TODO(phase0): file header + table-pointer array + per-table page chains
    // built from PdbPage. Requires the row serializers (track_row + lookups +
    // playlist_tree/entries) and the observed magic constants from a reference
    // stick. Returning an empty buffer keeps the end-to-end pipeline compiling.
    return {};
}

}  // namespace openboxxx
