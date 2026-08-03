# libopenboxxx-export

Standalone C++17 library that writes an unencrypted rekordbox/CDJ USB image
(`export.pdb` + per-track ANLZ files) from a plain intermediate model. **No Qt,
no Mixxx dependency** — that keeps the byte-fiddly serialization testable in
isolation before it's wired into Mixxx (see [../docs/export-design.md](../docs/export-design.md),
Phase 0).

## Layout

```
include/openboxxx/   public headers (model, byteio, writers, exporter, reader)
src/
  pdb/     export.pdb writer (LITTLE-endian DeviceSQL pages) + device_sql_string
  anlz/    ANLZ .DAT writer (BIG-endian tagged sections)
  usb/     PIONEER/ path assignment
  mapping/ rekordbox colour/rating/key encodings (invert Mixxx importer)
  verify/  round-trip check (tier 1) — stub pending Kaitai parser wiring
  diag/    diagnostic manifest for the in-app bug reporter
  adapter/ mixxxdb.sqlite -> ExportModel reader + beats-blob protobuf parser (Phase 1)
tools/     openboxxx_export_cli   — Phase 0 driver / smoke test
           openboxxx_from_mixxx   — Phase 1 reader CLI (real library -> USB)
tests/     dependency-free unit tests (+ SQLite fixture tests for the adapter)
```

## Build & test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/openboxxx_export_cli               # smoke test on a built-in demo model
./build/openboxxx_export_cli --out /tmp/u  # also write the USB image to disk
```

### Verification harnesses (optional, read-back oracles)

Two independent parsers prove our bytes are real rekordbox format:

- **`anlz_oracle`** — parses our ANLZ with `pyrekordbox` (MIT).
- **`pdb_roundtrip`** — compiles `third_party/crate-digger/rekordbox_pdb.ksy`
  (EPL-1.0, test-only) to a throwaway parser via `kaitai-struct-compiler` and
  reads back our `export.pdb`.

```sh
pip install -r tools/requirements.txt          # pyrekordbox + kaitaistruct
brew install kaitai-struct-compiler            # (or your platform's package)
cmake -S . -B build -DOPENBOXXX_ORACLE_PYTHON=$(which python3)
ctest --test-dir build   # openboxxx_export_tests + anlz_oracle + pdb_roundtrip

# or run either directly:
python3 tools/verify_anlz.py OURS.DAT --ref /path/to/real/ANLZ0000.DAT
kaitai-struct-compiler -t python --outdir /tmp/p third_party/crate-digger/rekordbox_pdb.ksy
PYTHONPATH=/tmp/p python3 tools/verify_pdb.py OURS/PIONEER/rekordbox/export.pdb
```

Each oracle test registers only if its tools are present, so environments
without them simply skip it.

## Export from a real Mixxx library (Phase 1, reader path)

If SQLite is available at configure time, the build also produces
`openboxxx_from_mixxx` — a standalone CLI that reads a Mixxx `mixxxdb.sqlite`
directly (no Mixxx build required) and writes a CDJ USB image. The eventual
in-Mixxx export job fills the *same* `ExportModel`, so all mapping decisions are
shared.

```sh
# dry run — read the library and report counts, write nothing
./build/openboxxx_from_mixxx --db "$HOME/.mixxx/mixxxdb.sqlite"

# write the PIONEER/ tree (and optionally copy the audio for a playable stick)
./build/openboxxx_from_mixxx --db /path/to/mixxxdb.sqlite --out /Volumes/USB --copy-audio

#   --limit N          export only the first N tracks (quick tests)
#   --ids A,B,C        export only these Mixxx track ids (targeted test sets)
#   --no-intro-outro   don't map Mixxx Intro/Outro cues as memory cues
```

The DB is opened **read-only**; the tool never modifies the user's library.
Units decoded and validated against a real 2.x library: cue positions are
fractional *stereo samples*, beatgrid first-beat is in *frames*, colours are
`0x00RRGGBB`. macOS default DB path is
`~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/mixxxdb.sqlite`.

## Status (Phase 0)

| Piece | State |
|---|---|
| `byteio` explicit LE/BE | ✅ implemented + tested |
| `device_sql_string` encoder | ✅ implemented + tested |
| `mapping` colour/rating | ✅ implemented + tested |
| ANLZ container + PPTH + PQTZ | ✅ implemented; validated via `anlz_oracle` |
| ANLZ PCOB / PCPT cue entries | ✅ layout validated via `anlz_oracle` (cue ordering TODO) |
| PDB page allocator (`PdbPage`) | ✅ framing validated via `pdb_roundtrip` |
| PDB tracks + lookups + playlists (`buildExportPdb`) | ✅ round-trips via `pdb_roundtrip` |
| PDB full 20-table set + colours palette | ✅ emitted; round-trips (colour_id resolves) |
| `verify` round-trip (native C++) | ⛔ TODO — port the read-back into the lib for in-app use |

The MVP export (all 20 standard tables incl. the 8-colour palette; tracks with
colour_id, artists/albums/genres/keys; playlists; ANLZ beatgrid + hot/memory
cues) now round-trips through independent parsers. Remaining before a hardware
test: refine the observed track_row constants / `index_shift` across rekordbox
versions, and add artwork. ANLZ Phase-2 (cosmetic): `vbr`, `wf_preview`,
`wf_tiny_preview`.

## Status (Phase 1 — reader path)

| Piece | State |
|---|---|
| `beats` blob protobuf parser (BeatGrid-2.0 / BeatMap-1.0) | ✅ implemented + unit-tested |
| `mixxxdb.sqlite` reader → `ExportModel` | ✅ implemented + SQLite-fixture tested |
| `openboxxx_from_mixxx` CLI (real library → USB) | ✅ works; audio copy behind `--copy-audio` |
| Validated on a real ~2,900-track library | ✅ `export.pdb` round-trips; ANLZ cue/beat times match the DB exactly |
| Tier-2 diff vs genuine rekordbox ANLZ | ✅ shared sections (`PPTH`/`PQTZ`/`PCOB`×2) match field-for-field; only `vbr`+waveforms missing (Phase 2) |
| — found + fixed: negative-position hot cue dropped | ✅ now clamped to 0 (was silently losing a hot cue) |
| — found + fixed: duplicate memory cue (MainCue≡Intro) | ✅ now deduped by millisecond |
| In-Mixxx `RekordboxExportJob` + dialog + CMake option | ⛔ next — fills the same model from live objects |
| Import test in rekordbox desktop (tier 3) | ⛔ pending — needs a removable volume + GUI (manual smoke test) |

Known minor deviation (not yet changed): rekordbox writes the hot-cue `PCOB`
before the memory `PCOB`; we emit memory first. Readers key off each list's
`cue_type` field, not position, so this is cosmetic — left as-is until a
populated real reference confirms the convention.
