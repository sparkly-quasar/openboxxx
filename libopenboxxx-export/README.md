# libopenboxxx-export

Standalone C++17 library that writes an unencrypted rekordbox/CDJ USB image
(`export.pdb` + per-track ANLZ files) from a plain intermediate model. **No Qt,
no Mixxx dependency** — that keeps the byte-fiddly serialization testable in
isolation before it's wired into Mixxx (see [../docs/export-design.md](../docs/export-design.md),
Phase 0).

## Layout

```
include/openboxxx/   public headers (model, byteio, writers, exporter)
src/
  pdb/     export.pdb writer (LITTLE-endian DeviceSQL pages) + device_sql_string
  anlz/    ANLZ .DAT writer (BIG-endian tagged sections)
  usb/     PIONEER/ path assignment
  mapping/ rekordbox colour/rating/key encodings (invert Mixxx importer)
  verify/  round-trip check (tier 1) — stub pending Kaitai parser wiring
  diag/    diagnostic manifest for the in-app bug reporter
tools/     openboxxx_export_cli — Phase 0 driver / smoke test
tests/     dependency-free unit tests
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
| `verify` round-trip (native C++) | ⛔ TODO — port the read-back into the lib for in-app use |

The MVP export (tracks, artists/albums/genres/keys, playlists; ANLZ beatgrid +
hot/memory cues) now round-trips through independent parsers. Remaining before a
hardware test: emit the full standard table set (incl. empty tables), refine the
observed track_row constants / `index_shift`, and add colours/artwork. ANLZ
Phase-2 (cosmetic): `vbr`, `wf_preview`, `wf_tiny_preview`.
