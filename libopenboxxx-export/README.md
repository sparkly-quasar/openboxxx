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

### Tier-2 ANLZ oracle (optional)

Proves our ANLZ output is valid rekordbox format by round-tripping it through an
independent parser (`pyrekordbox`, MIT):

```sh
pip install -r tools/requirements.txt
# wire it into ctest by pointing at that interpreter:
cmake -S . -B build -DOPENBOXXX_ORACLE_PYTHON=$(which python3)
ctest --test-dir build            # now runs openboxxx_export_tests + anlz_oracle
# or run it directly against any .DAT, optionally comparing to a real stick:
python3 tools/verify_anlz.py OURS.DAT --ref /path/to/real/ANLZ0000.DAT
```

The oracle test is registered only if `pyrekordbox` imports, so environments
without it simply skip it.

## Status (Phase 0, in progress)

| Piece | State |
|---|---|
| `byteio` explicit LE/BE | ✅ implemented + tested |
| `device_sql_string` encoder | ✅ implemented + tested |
| `mapping` colour/rating | ✅ implemented + tested |
| ANLZ container + PPTH + PQTZ | ✅ implemented; validated via tier-2 oracle |
| ANLZ PCOB / PCPT cue entries | ✅ layout validated via tier-2 oracle (ordering TODO) |
| PDB page allocator (`PdbPage`) | ⚠️ framing implemented; row-group offsets need validation |
| PDB table/row serializers (`buildExportPdb`) | ⛔ TODO — the big remaining chunk |
| `verify` round-trip (tier 1, C++) | ⛔ TODO — needs Kaitai `rekordbox_pdb`/`_anlz` parsers |

Next: the PDB row serializers (the `⛔`), validated the same way once a PDB
parser is wired for read-back. ANLZ Phase-2 sections a real stick showed we still
owe: `vbr`, `wf_preview`, `wf_tiny_preview` (all cosmetic/optional).
