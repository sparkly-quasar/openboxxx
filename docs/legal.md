# Legal position

The goal: build a genuinely open, useful tool **without picking the one fight most likely to get
the project killed** — hardware DRM circumvention and encryption bypass.

## The one line that actually matters: DMCA §1201 (anti-circumvention)

The legally radioactive part is **NOT** the USB format. It's the encrypted rekordbox 6/7
application database (`master.db`), locked with SQLCipher. Reading it requires a key that was
extracted from AlphaTheta's app. **Shipping or using that key = anti-circumvention risk.**

### The escape hatch

For the export use case, **we never touch the encrypted app database.**

- **The USB export format (PDB/ANLZ) is NOT encrypted.** Writing it circumvents no access control —
  there is nothing to decrypt. Safe ground.
- **`rekordbox.xml` is an official, sanctioned interchange format.** Reading and writing it is
  unambiguously fine (Serato, Traktor, etc. all use it).

### Rules we follow

1. **Never bundle AlphaTheta's extracted SQLCipher key.** This single rule keeps us out of the
   §1201 fight entirely.
2. **Import path:** read `rekordbox.xml` (official) and *unencrypted* USB exports. If a user wants
   their existing rekordbox 6/7 library in, ask *them* to export `rekordbox.xml` from their own
   rekordbox — the sanctioned path — rather than us decrypting their `master.db`.
3. **Export path:** write unencrypted PDB/ANLZ USBs. Add OneLibrary/Device Library Plus later.

## License compatibility (a real trap)

Mixxx is **GPLv2**. Anything we *link into it* must be GPLv2-compatible.

| Project | License | GPL-compatible? | Writes exports? | Language |
|---|---|---|---|---|
| Mixxx | GPLv2 | (the base) | — | C++ |
| pyrekordbox | MIT | yes (permissive) | yes — XML, ANLZ, Device Library Plus | Python |
| rekordcrate | MPL-2.0 | yes | no (read-only) | Rust |
| crate-digger | EPL-2.0 | **NO** — conflicts with GPLv2 | — | Java/Clojure |

- **crate-digger is the trap.** EPL-2.0 is not GPLv2-compatible. Use only as *documentation*,
  never as linked code.
- **None of these are C++ anyway** — so we don't link any of them. Their value is as reference
  implementations / format specs.
- **pyrekordbox (MIT)** is the goldmine: it already writes the formats, is permissively licensed,
  and its source is freely readable to understand the byte layout.

## Clean-room-ish implementation

A file format itself isn't copyrightable — only a specific code expression of it is. So:
**reimplement the PDB/ANLZ writer in fresh C++ inside Mixxx**, using public format documentation
(Deep Symmetry's dysentery/Kaitai specs, pyrekordbox's readable source) as reference for the byte
layout. We inherit the knowledge, not the licensing entanglement. This is how Mixxx's existing
rekordbox *importer* was built.

> This is engineering guidance, not legal advice. Before any public release, get a real IP lawyer
> to review the reverse-engineering and interop posture for the relevant jurisdictions.
