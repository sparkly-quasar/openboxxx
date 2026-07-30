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
./build/openboxxx_export_cli          # smoke test on a built-in demo model
```

## Status (Phase 0, in progress)

| Piece | State |
|---|---|
| `byteio` explicit LE/BE | ✅ implemented + tested |
| `device_sql_string` encoder | ✅ implemented + tested |
| `mapping` colour/rating | ✅ implemented + tested |
| ANLZ container + PPTH + PQTZ | ✅ implemented + framing-tested |
| ANLZ PCOB / PCPT cue entries | ⚠️ best-effort layout, needs reference-stick validation |
| PDB page allocator (`PdbPage`) | ⚠️ framing implemented; row-group offsets need validation |
| PDB table/row serializers (`buildExportPdb`) | ⛔ TODO — the big remaining chunk |
| `verify` round-trip (tier 1) | ⛔ TODO — needs Kaitai `rekordbox_pdb`/`_anlz` parsers |

The `⚠️`/`⛔` items are exactly what verification tiers 1–2 (round-trip parse +
diff against a real rekordbox stick) will pin down; see the design doc.
