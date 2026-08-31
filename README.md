# openboxxx

An open-source DJ software effort focused on **making Mixxx a first-class citizen in the
Pioneer/AlphaTheta CDJ world** — so a DJ can prepare a set in free, open software and walk up
to any club CDJ with confidence that their playlists, cues, and beatgrids come with them.

---

## 🎧 Got a CDJ? We need you

openboxxx can already write a rekordbox USB stick from a Mixxx library. Every byte it
writes is checked against two independent real-rekordbox-format parsers. But **nobody
has confirmed it on physical hardware yet** — and that is the only test that counts.

### ➡️ **[Beta testing guide](docs/beta-testing.md)** — step by step, about 20 minutes, no experience needed

The short version:

1. Format a **spare** USB stick as FAT32.
2. Install the [beta Mixxx build](https://github.com/sparkly-quasar/openboxxx/releases/tag/v0.1.0-alpha)
   (macOS / Windows / Linux) — or build the
   [command-line exporter](docs/beta-testing.md#path-b--export-from-the-command-line)
   if you'd rather not install a fork.
3. **Library → Export Library to rekordbox USB**, and say **Yes** to copying audio.
4. Try it on the player and
   [tell us what happened](https://github.com/sparkly-quasar/openboxxx/issues/new?template=beta-report.md&labels=beta-report)
   — **including if it all worked.** We have no hardware results at all yet, so a
   "CDJ-3000, 20 tracks, everything correct" report is worth as much as a bug.

**Bring your normal USB too, and don't try this first at a paying gig.** Waveforms will
be blank (not implemented yet); the music still plays.

---

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

**Phase 0 (standalone writer) — proven.** [`libopenboxxx-export/`](libopenboxxx-export/) turns a
plain track/playlist model into a real `export.pdb` (all 20 standard tables + the 8-colour palette)
plus per-track ANLZ analysis files (beat grids + hot/memory cues). Every output is checked against
**two independent real-rekordbox-format parsers** (pyrekordbox for ANLZ, a crate-digger-spec Kaitai
parser for PDB), and diffed field-for-field against genuine rekordbox ANLZ files.

**Phase 1 (Mixxx integration) — working.** Both the reader path and the in-Mixxx button exist:

- `openboxxx_from_mixxx` reads a `mixxxdb.sqlite` directly into the export model (no Mixxx build
  needed), validated on a real ~2,900-track library.
- An in-Mixxx **"Export Library to rekordbox USB"** button — a `RekordboxExportJob` + dialog
  mirroring the Engine DJ exporter, behind a `REKORDBOX_EXPORT` CMake option — plus a **"Cue Sheet
  to Tracklist"** tool. This lives in a Mixxx fork and **compiles + links on macOS (Intel + Apple
  Silicon), Windows, and Linux** via CI. Downloadable builds are on the
  [**Releases**](https://github.com/sparkly-quasar/openboxxx/releases) page; source is
  [sparkly-quasar/mixxx `feat/rekordbox-usb-export`](https://github.com/sparkly-quasar/mixxx/tree/feat/rekordbox-usb-export).

**Still to confirm — desktop import & hardware.** Exports complete without errors and round-trip
through the parsers, but haven't yet been confirmed by importing into rekordbox **desktop**
(verification tier 3) or playing on a physical **CDJ/XDJ** (tier 4). Those are the real proof —
**CDJ hardware testers are very welcome:** see the [beta testing guide](docs/beta-testing.md).

See [`libopenboxxx-export/README.md`](libopenboxxx-export/README.md) for the component-by-component
status table and how to build + run the verification tests.

## Docs

- [docs/beta-testing.md](docs/beta-testing.md) — **start here if you're testing** — step-by-step export + hardware checklist
- [docs/legal.md](docs/legal.md) — how we stay within legal bounds (the important one)
- [docs/architecture.md](docs/architecture.md) — high-level framing & reuse map
- [docs/export-design.md](docs/export-design.md) — implementation design: phases, MVP scope, verification ladder, in-app bug reporting
- [docs/research-findings.md](docs/research-findings.md) — source-cited research: Mixxx internals + PDB/ANLZ byte formats
- [docs/mixxx-export-status.md](docs/mixxx-export-status.md) — state of Mixxx's export effort (what exists vs. what's left)

## Credits & prior art

openboxxx stands on a lot of other people's work — both the format reverse-engineering
that makes writing these files possible and earlier Mixxx→rekordbox efforts.

**Format research:**
- [crate-digger](https://github.com/Deep-Symmetry/crate-digger) &
  [dysentery](https://github.com/Deep-Symmetry/dysentery) — Deep Symmetry (James Elliott):
  the PDB/ANLZ reverse-engineering and the `.ksy` spec this project round-trips its output
  against (EPL-1.0, used as a format description, not copied code).
- [pyrekordbox](https://github.com/dylanljones/pyrekordbox) — Dylan Jones (MIT): ANLZ layouts
  referenced here, and an independent parser the tests use as an oracle.

**Prior rekordbox/PDB libraries & exporters:**
- [rekordcrate](https://github.com/Holzhaus/rekordcrate) — Jan Holthuis (**@Holzhaus**): a Rust
  PDB parser/serializer (PDB serialization is merged) and the Mixxx maintainer-suggested path
  for PDB export.
- [libdjinterop](https://github.com/xsco/libdjinterop) — **@mr-smidge**: the library behind
  Mixxx's existing Engine Prime export; the Mixxx adapter here mirrors its export-job/dialog
  architecture.
- [rex](https://github.com/ambientsound/rex) (**@kimtore**),
  [mixxx-db-tools](https://github.com/arximboldi/mixxx-db-tools) (**@arximboldi**),
  `TheKantankerus/MixxxToRekordbox`, and `FrankwaP/mixxx-utils`: earlier community approaches to
  getting Mixxx libraries onto CDJs.

**Context:** this addresses the long-standing feature request
[mixxxdj/mixxx#9463](https://github.com/mixxxdj/mixxx/issues/9463). openboxxx's writer is a
standalone **C++** library — a third path alongside rekordcrate (Rust) and libdjinterop (C++).
Which architecture Mixxx should ultimately adopt is an open discussion, not settled here.

## License

Because this builds on Mixxx (GPLv2), this project is **GPLv2** as well. That copyleft is a
feature, not a bug — it keeps the work open, which is the whole point.
