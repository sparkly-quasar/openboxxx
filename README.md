# openboxxx

An open-source DJ software effort focused on **making Mixxx a first-class citizen in the
Pioneer/AlphaTheta CDJ world** — so a DJ can prepare a set in free, open software and walk up
to any club CDJ with confidence that their playlists, cues, and beatgrids come with them.

## The problem we're solving

The single fear that keeps DJs on proprietary rekordbox:

> "I'm scared that if I use something else, my USB won't be read by the newer CDJs."

The reassurance we're building toward:

- **Your music always plays.** Even if a database is malformed, a CDJ can browse the USB by
  folder and play the raw audio. The catastrophic "nothing works" scenario isn't on the table —
  worst case you lose *prepared metadata*, not the music.
- **On legacy CDJs, your full prep comes with it** — and it's tested on real hardware.
- **On the newest CDJs**, we target both the reverse-engineered path and AlphaTheta's own
  OneLibrary format.
- **You can verify the stick before you leave the house** (round-trip verifier).

## Approach

Rather than build a DJ app from scratch, we build on **Mixxx** (GPLv2) — a mature, cross-platform
open-source DJ application. The goal is to add/extend **CDJ USB export** (writing the unencrypted
PDB/ANLZ format that CDJs read directly), ideally **upstreamed into Mixxx** rather than kept as a
hard fork. See [docs/architecture.md](docs/architecture.md).

## Status

**Phase 0 (standalone writer) — core proven.** The byte-level CDJ USB writer exists and is
validated: [`libopenboxxx-export/`](libopenboxxx-export/) turns a plain track/playlist model into
a real `export.pdb` (all 20 standard tables + the 8-colour palette) plus per-track ANLZ analysis
files (beat grids + hot/memory cues). Every output is checked against **two independent
real-rekordbox-format parsers** (pyrekordbox for ANLZ, a crate-digger-spec Kaitai parser for PDB),
so we can prove the bytes are right, not just hope.

**Not yet done — Phase 1 (Mixxx integration).** There is no in-Mixxx export button yet. Mixxx has
no plugin API for this, so integration means adding a `RekordboxExportJob` (mirroring the existing
Engine DJ exporter) compiled into Mixxx, plus an adapter that fills the export model from Mixxx's
live library — to be upstreamed, not shipped as a loadable plugin. See the roadmap in
[docs/export-design.md](docs/export-design.md).

**Not yet done — hardware validation.** The output round-trips through parsers but has not been
tested on a physical CDJ/XDJ (verification-ladder tier 4). That is the real proof and comes after
a `--write-to-USB` path + a borrowed player.

See [`libopenboxxx-export/README.md`](libopenboxxx-export/README.md) for the component-by-component
status table and how to build + run the verification tests.

## Docs

- [docs/legal.md](docs/legal.md) — how we stay within legal bounds (the important one)
- [docs/architecture.md](docs/architecture.md) — high-level framing & reuse map
- [docs/export-design.md](docs/export-design.md) — implementation design: phases, MVP scope, verification ladder, in-app bug reporting
- [docs/research-findings.md](docs/research-findings.md) — source-cited research: Mixxx internals + PDB/ANLZ byte formats
- [docs/mixxx-export-status.md](docs/mixxx-export-status.md) — state of Mixxx's export effort (what exists vs. what's left)

## License

Because this builds on Mixxx (GPLv2), this project is **GPLv2** as well. That copyleft is a
feature, not a bug — it keeps the work open, which is the whole point.
