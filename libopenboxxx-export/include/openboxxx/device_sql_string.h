// SPDX-License-Identifier: GPL-2.0-or-later
//
// DeviceSQL string encoding used by every variable-length string in export.pdb.
// Spec: docs/research-findings.md §B.2 ("device_sql_string encoding").
//
//   leading byte "length_and_kind":
//     * odd            -> short ASCII: length_and_kind = (text_len + 1) * 2 + 1,
//                         then text_len ASCII bytes. Empty string = 0x03.
//     * 0x40           -> long ASCII : u2 length (incl. 4-byte header), u1 pad,
//                         then (length - 4) ASCII bytes.
//     * 0x90           -> long UTF-16LE: u2 length, u1 pad, then (length - 4)
//                         bytes UTF-16LE. Used for any non-ASCII text.
#ifndef OPENBOXXX_DEVICE_SQL_STRING_H
#define OPENBOXXX_DEVICE_SQL_STRING_H

#include <string>

#include "openboxxx/byteio.h"

namespace openboxxx {

// Append `s` to `out` as a DeviceSQL string, choosing the encoding automatically:
// short-ASCII when it fits (len <= 126 and pure ASCII), long-ASCII for longer
// ASCII, and UTF-16LE (0x90) when any byte is non-ASCII. `s` is treated as UTF-8.
void putDeviceSqlString(ByteBuffer& out, const std::string& s);

}  // namespace openboxxx

#endif  // OPENBOXXX_DEVICE_SQL_STRING_H
