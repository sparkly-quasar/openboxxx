// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal protobuf reader for Mixxx's `beats` BLOB. We decode the wire format by
// hand (a few varints + one fixed64) rather than pull in the full protobuf
// runtime -- the schema is tiny and stable, and this keeps the adapter's
// dependency footprint to just SQLite.
//
// Wire format recap (protobuf): each field is a varint "key" = (field_number<<3)
// | wire_type. wire_type 0 = varint, 1 = 64-bit, 2 = length-delimited, 5 = 32-bit.
//
// Mixxx schemas (src/proto/beats.proto), confirmed against a real 2.x DB:
//   BeatGrid-2.0:  field 1 = Bpm{ field 1: double bpm },
//                  field 2 = FramePos{ field 1: first-beat frame }
//   BeatMap-1.0:   repeated field 1 = Beat{ field 1: beat frame }
// Frame positions appear as varints in this DB but older/newer builds may store
// them as fixed64 doubles, so the reader accepts either wire type for a position.
#include "openboxxx/mixxxdb_reader.h"

#include <cmath>
#include <cstring>

namespace openboxxx {
namespace {

// A forward cursor over the blob. All reads are bounds-checked; on overrun the
// cursor goes into a sticky error state and subsequent reads return 0/false.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool eof() const { return p >= end; }

    uint64_t varint() {
        uint64_t v = 0;
        int shift = 0;
        while (p < end && shift < 64) {
            const uint8_t b = *p++;
            v |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
        }
        ok = false;
        return 0;
    }

    uint64_t fixed64() {
        if (end - p < 8) { ok = false; return 0; }
        uint64_t v = 0;
        std::memcpy(&v, p, 8);  // stored little-endian on all targets we support
        p += 8;
        return v;
    }

    // Skip a field's payload given its wire type; returns false on malformed input.
    bool skip(uint32_t wire_type) {
        switch (wire_type) {
            case 0: varint(); return ok;
            case 1: fixed64(); return ok;
            case 5: if (end - p < 4) { ok = false; } else { p += 4; } return ok;
            case 2: {
                const uint64_t n = varint();
                if (!ok || uint64_t(end - p) < n) { ok = false; return false; }
                p += n;
                return true;
            }
            default: ok = false; return false;
        }
    }
};

double asDouble(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

// Read one position value: a varint frame count, or a fixed64 double rounded to
// the nearest frame. `wire_type` selects which.
double readPosition(Reader& r, uint32_t wire_type) {
    if (wire_type == 1) return asDouble(r.fixed64());
    return double(r.varint());
}

// Pull {bpm, first_beat_frame} out of a BeatGrid-2.0 blob. Missing fields stay 0.
bool parseBeatGrid(Reader& r, double& bpm_out, double& first_frame_out) {
    bpm_out = 0.0;
    first_frame_out = 0.0;
    while (!r.eof() && r.ok) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3);
        const uint32_t wt = uint32_t(key & 0x7);
        if (field == 1 && wt == 2) {  // Bpm submessage
            const uint64_t len = r.varint();
            if (!r.ok || uint64_t(r.end - r.p) < len) { r.ok = false; break; }
            Reader sub{r.p, r.p + len, true};
            r.p += len;
            while (!sub.eof() && sub.ok) {
                const uint64_t k = sub.varint();
                const uint32_t f = uint32_t(k >> 3), w = uint32_t(k & 0x7);
                if (f == 1) bpm_out = (w == 1) ? asDouble(sub.fixed64())
                                               : double(sub.varint());
                else sub.skip(w);
            }
        } else if (field == 2 && wt == 2) {  // FramePos submessage
            const uint64_t len = r.varint();
            if (!r.ok || uint64_t(r.end - r.p) < len) { r.ok = false; break; }
            Reader sub{r.p, r.p + len, true};
            r.p += len;
            while (!sub.eof() && sub.ok) {
                const uint64_t k = sub.varint();
                const uint32_t f = uint32_t(k >> 3), w = uint32_t(k & 0x7);
                if (f == 1) first_frame_out = readPosition(sub, w);
                else sub.skip(w);
            }
        } else {
            r.skip(wt);
        }
    }
    return r.ok && bpm_out > 0.0;
}

// Collect explicit beat frames from a BeatMap-1.0 blob (repeated Beat, field 1).
bool parseBeatMap(Reader& r, std::vector<double>& frames_out) {
    while (!r.eof() && r.ok) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3);
        const uint32_t wt = uint32_t(key & 0x7);
        if (field == 1 && wt == 2) {  // Beat submessage
            const uint64_t len = r.varint();
            if (!r.ok || uint64_t(r.end - r.p) < len) { r.ok = false; break; }
            Reader sub{r.p, r.p + len, true};
            r.p += len;
            double frame = 0.0;
            bool got = false;
            while (!sub.eof() && sub.ok) {
                const uint64_t k = sub.varint();
                const uint32_t f = uint32_t(k >> 3), w = uint32_t(k & 0x7);
                if (f == 1) { frame = readPosition(sub, w); got = true; }
                else sub.skip(w);
            }
            if (got) frames_out.push_back(frame);
        } else {
            r.skip(wt);
        }
    }
    return r.ok;
}

// Emit a per-beat marker list from explicit frame positions. Tempo at each beat
// is derived from the gap to the next beat (last beat reuses the previous tempo).
std::vector<Beat> beatsFromFrames(const std::vector<double>& frames,
                                  int sample_rate) {
    std::vector<Beat> out;
    if (frames.size() < 2 || sample_rate <= 0) return out;
    out.reserve(frames.size());
    uint16_t last_tempo = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        Beat b;
        b.beat_number = uint16_t((i % 4) + 1);
        if (i + 1 < frames.size()) {
            const double dframes = frames[i + 1] - frames[i];
            if (dframes > 0) {
                const double bpm = 60.0 * sample_rate / dframes;
                last_tempo = uint16_t(std::lround(bpm * 100.0));
            }
        }
        b.tempo_x100 = last_tempo;
        b.time_ms = uint32_t(std::lround(frames[i] / sample_rate * 1000.0));
        out.push_back(b);
    }
    return out;
}

}  // namespace

std::vector<Beat> parseMixxxBeats(const uint8_t* data, std::size_t len,
                                  const std::string& beats_version,
                                  int sample_rate, double duration_s) {
    std::vector<Beat> out;
    if (!data || len == 0 || sample_rate <= 0) return out;

    if (beats_version == "BeatGrid-2.0") {
        Reader r{data, data + len, true};
        double bpm = 0.0, first_frame = 0.0;
        if (!parseBeatGrid(r, bpm, first_frame)) return out;
        if (bpm <= 0.0 || bpm > 500.0) return out;  // sanity guard
        const double beat_len = 60.0 * sample_rate / bpm;  // frames per beat
        if (beat_len <= 0.0) return out;
        const double end_frame = duration_s > 0.0 ? duration_s * sample_rate
                                                  : first_frame + beat_len;
        const uint16_t tempo_x100 = uint16_t(std::lround(bpm * 100.0));
        // Phase the grid back to the first beat in [0, beat_len) so it starts at
        // the top of the track. Computed directly (no loop) -- a stray large
        // first_frame must never spin. std::fmod keeps the downbeat phase.
        double f = std::fmod(first_frame, beat_len);
        if (f < 0.0) f += beat_len;
        // Safety cap: a normal track has a few thousand beats; anything past this
        // means garbage tempo, so stop rather than allocate unboundedly.
        constexpr int kMaxBeats = 20000;
        int bar_pos = 0;  // phase relative to the stored downbeat is unknown; MVP
                          // assumes the grid's first emitted beat is a downbeat.
        for (int n = 0; f < end_frame && n < kMaxBeats; ++n, f += beat_len) {
            Beat b;
            b.beat_number = uint16_t((bar_pos++ % 4) + 1);
            b.tempo_x100 = tempo_x100;
            b.time_ms = uint32_t(std::lround(f / sample_rate * 1000.0));
            out.push_back(b);
        }
        return out;
    }

    if (beats_version == "BeatMap-1.0") {
        Reader r{data, data + len, true};
        std::vector<double> frames;
        if (!parseBeatMap(r, frames)) return out;
        return beatsFromFrames(frames, sample_rate);
    }

    return out;  // unknown version
}

}  // namespace openboxxx
