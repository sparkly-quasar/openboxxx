// SPDX-License-Identifier: GPL-2.0-or-later
//
// Explicit-endianness byte buffer. This is the cross-platform foundation of the
// whole writer (see docs/export-design.md "Core rule: byte layout is explicit,
// never host-dependent"): every integer is serialized byte-by-byte in a chosen
// order, so output is identical on macOS/Linux/Windows and on any CPU endianness.
//
// PDB is little-endian; ANLZ is big-endian. The two writers pick the matching
// helpers here and never share them.
#ifndef OPENBOXXX_BYTEIO_H
#define OPENBOXXX_BYTEIO_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openboxxx {

// Append-oriented byte buffer with offset-based patching. Patching exists so a
// container can reserve a length field, keep writing, then backfill the real
// length once known (e.g. ANLZ's PMAI.len_file, PDB page sizes).
class ByteBuffer {
public:
    std::size_t size() const { return data_.size(); }
    const std::vector<uint8_t>& bytes() const { return data_; }

    // Raw appends.
    void putU8(uint8_t v) { data_.push_back(v); }
    void putBytes(const uint8_t* p, std::size_t n) { data_.insert(data_.end(), p, p + n); }
    void putBytes(const std::vector<uint8_t>& v) { data_.insert(data_.end(), v.begin(), v.end()); }

    // ASCII text, no terminator, no length prefix (the caller owns framing).
    void putAscii(const std::string& s) {
        data_.insert(data_.end(), s.begin(), s.end());
    }

    // Zero padding.
    void putZeros(std::size_t n) { data_.insert(data_.end(), n, uint8_t{0}); }

    // Little-endian (PDB / DeviceSQL, and UTF-16LE payloads).
    void putU16LE(uint16_t v) {
        putU8(uint8_t(v)); putU8(uint8_t(v >> 8));
    }
    void putU32LE(uint32_t v) {
        putU8(uint8_t(v)); putU8(uint8_t(v >> 8));
        putU8(uint8_t(v >> 16)); putU8(uint8_t(v >> 24));
    }

    // Big-endian (ANLZ container + tags, and UTF-16BE payloads like PPTH).
    void putU16BE(uint16_t v) {
        putU8(uint8_t(v >> 8)); putU8(uint8_t(v));
    }
    void putU32BE(uint32_t v) {
        putU8(uint8_t(v >> 24)); putU8(uint8_t(v >> 16));
        putU8(uint8_t(v >> 8)); putU8(uint8_t(v));
    }

    // Backfill a value at an already-written offset (bounds-checked).
    void patchU16LE(std::size_t off, uint16_t v) {
        need(off, 2); data_[off] = uint8_t(v); data_[off + 1] = uint8_t(v >> 8);
    }
    void patchU32LE(std::size_t off, uint32_t v) {
        need(off, 4);
        data_[off] = uint8_t(v); data_[off + 1] = uint8_t(v >> 8);
        data_[off + 2] = uint8_t(v >> 16); data_[off + 3] = uint8_t(v >> 24);
    }
    void patchU32BE(std::size_t off, uint32_t v) {
        need(off, 4);
        data_[off] = uint8_t(v >> 24); data_[off + 1] = uint8_t(v >> 16);
        data_[off + 2] = uint8_t(v >> 8); data_[off + 3] = uint8_t(v);
    }

private:
    void need(std::size_t off, std::size_t n) const {
        if (off + n > data_.size()) {
            throw std::out_of_range("ByteBuffer patch past end of buffer");
        }
    }
    std::vector<uint8_t> data_;
};

}  // namespace openboxxx

#endif  // OPENBOXXX_BYTEIO_H
