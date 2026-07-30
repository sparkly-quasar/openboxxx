# third_party/crate-digger

`rekordbox_pdb.ksy` — the Kaitai Struct format description of the rekordbox
`export.pdb` DeviceSQL database, from [Deep-Symmetry/crate-digger](https://github.com/Deep-Symmetry/crate-digger).

- **License:** EPL-1.0 (declared in the file's `meta:` block). Redistributed here
  under those terms.
- **Use:** **test-time only.** It is compiled to a throwaway Python parser
  (`kaitai-struct-compiler -t python`) to read back our own `export.pdb` output
  in the `pdb_roundtrip` verification test (ladder tier 1). It is **not** compiled
  into, linked with, or distributed as part of the GPLv2 `openboxxx_export`
  library — it is a development/CI tool input, akin to a test fixture.

We track the byte layout from this spec but hand-write our own serializer
(`src/pdb/`); this file is the authoritative reference we validate against.
