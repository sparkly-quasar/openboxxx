// SPDX-License-Identifier: GPL-2.0-or-later
#include "openboxxx/device_sql_string.h"

#include <cstdint>
#include <vector>

namespace openboxxx {
namespace {

bool isPureAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

// Minimal UTF-8 -> UTF-16 code units (used only for the non-ASCII path). Emits
// surrogate pairs for astral code points. Invalid sequences are skipped rather
// than throwing; the writer's job is best-effort text, not validation.
std::vector<uint16_t> utf8ToUtf16(const std::string& s) {
    std::vector<uint16_t> out;
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        uint32_t cp;
        std::size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { ++i; continue; }  // invalid lead byte
        if (i + len > n) break;
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            unsigned char cc = s[i + k];
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

}  // namespace

void putDeviceSqlString(ByteBuffer& out, const std::string& s) {
    if (isPureAscii(s)) {
        // Short ASCII covers text_len up to 126 (so length_and_kind stays 1 byte
        // and odd). Beyond that, use the long-ASCII form.
        if (s.size() <= 126) {
            out.putU8(uint8_t((s.size() + 1) * 2 + 1));
            out.putAscii(s);
            return;
        }
        const uint16_t total = uint16_t(s.size() + 4);  // header (4) + payload
        out.putU8(0x40);
        out.putU16LE(total);
        out.putU8(0x00);  // pad
        out.putAscii(s);
        return;
    }

    // Non-ASCII -> long UTF-16LE.
    const std::vector<uint16_t> u16 = utf8ToUtf16(s);
    const uint16_t total = uint16_t(u16.size() * 2 + 4);
    out.putU8(0x90);
    out.putU16LE(total);
    out.putU8(0x00);  // pad
    for (uint16_t cu : u16) out.putU16LE(cu);
}

}  // namespace openboxxx
