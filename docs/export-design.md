# Export module design

Detailed implementation architecture for the CDJ USB exporter. Builds on
[architecture.md](architecture.md) (high-level framing) and
[research-findings.md](research-findings.md) (source-cited format + Mixxx internals research).

## Decisions locked in

| # | Decision | Rationale |
|---|---|---|
| 1 | **Build the writer as a standalone C++ library first**, then wire into Mixxx. | De-risk the hardest, byte-fiddly part in isolation where testing is fast and unambiguous, before fighting Mixxx's build + plumbing. |
| 2 | **First milestone = a *playable* USB MVP**, not full fidelity. | Fastest route to the only question that matters — "does a stick we made work in a real CDJ?" Waveforms + extended cues are a low-risk follow-up (they're additive ANLZ sections, don't touch the PDB). |
| 3 | **Cross-platform (macOS, Linux, Windows) from day one.** | Mixxx is tri-platform; matching it maximizes the beta-tester pool, which is our main source of hardware-compatibility data. |
| 4 | **Ship an in-app "Report a bug / send export diagnostics" flow.** | Every tester becomes a data source; turns scattered "it didn't work" into structured, reproducible reports. |

## Component architecture

The standalone library (working name `libopenboxxx-export`) has no Qt/Mixxx dependency in its core —
it takes a plain intermediate model and emits bytes. Mixxx integration is a thin adapter on top.

```
                         ┌─────────────────────────────────────────┐
   Mixxx (adapter layer) │  RekordboxExportJob  (mirrors            │
   — Phase 1 —           │  EnginePrimeExportJob, QThread)          │
                         │  walks library → fills ExportModel       │
                         └───────────────────┬─────────────────────┘
                                             │ ExportModel (plain structs)
   ┌─────────────────────────────────────────▼─────────────────────────────────────────┐
   │  libopenboxxx-export  (standalone, no Qt — Phase 0)                                 │
   │                                                                                    │
   │   mapping/      Mixxx-model → rekordbox-model (colors, keys, ms↔frame, cue types)  │
   │   pdb/          export.pdb writer   — LITTLE-endian DeviceSQL pages                 │
   │   anlz/         ANLZ .DAT/.EXT writer — BIG-endian tagged sections                  │
   │   usb/          PIONEER/ dir layout, ANLZ path assignment, audio-file copy          │
   │   verify/       round-trip: parse our output with Kaitai parsers + diff             │
   │   diag/         diagnostic-bundle builder (feeds the in-app bug report)             │
   └────────────────────────────────────────────────────────────────────────────────────┘
```

### Core rule: byte layout is explicit, never host-dependent
This is what makes cross-platform safe. The writers never rely on the host CPU's byte order,
struct padding, or `wchar_t` size:
- PDB integers written explicitly little-endian; ANLZ integers explicitly big-endian (helper
  functions per format — do **not** share them). All target platforms happen to be little-endian
  today, but we never assume it.
- Strings encoded by hand per the `device_sql_string` rules (short-ASCII mangling, or `0x90`
  UTF-16**LE** for PDB) and UTF-16**BE** for ANLZ `PPTH`/cue labels. No `std::wstring`/`wchar_t`.
- USB-internal paths always use forward slashes (`/PIONEER/USBANLZ/...`) regardless of host OS;
  only filesystem *access* to the stick goes through the platform layer (Qt `QDir`/`QFile` in the
  Mixxx adapter; `std::filesystem` in standalone tests).
- No FAT32/exFAT-specific code — we write ordinary files; the DJ formats the stick.

### Module responsibilities
- **mapping/** — inverts the mappings the Mixxx *importer* already encodes (see research-findings
  §A.4): color RGB→nearest rekordbox ID, `ChromaticKey`→rekordbox key-id, Mixxx frame→ms, Mixxx
  `MainCue`→first memory cue, `HotCue`/`Loop`→hot-cue + loop lists.
- **pdb/** — the hard part; genuinely net-new code. Page allocator (4096-byte pages, forward heap
  + backward 16-row-group index), all ~20 tables (empty ones get an empty page pair), `track_row`
  + lookup rows, `device_sql_string` encoder. Byte layout copied from crate-digger's `.ksy` spec
  (EPL-1.0, spec not code) — see research-findings §B.2.
- **anlz/** — tagged-section container (`PMAI` + `PXXX`). MVP tags: `PPTH`, `PQTZ` (beatgrid),
  `PCOB` memory + `PCOB` hot. Port the struct layouts from **pyrekordbox** (MIT, already writes
  ANLZ) — see research-findings §B.3, §B.5.
- **usb/** — assigns each track its `PIONEER/USBANLZ/P<xxx>/<hex>/ANLZ0000.*` path and guarantees
  the `track_row.analyze_path` string matches the file actually written (linkage is by stored
  string, not algorithm — research-findings §B.1). ⚠️ verify the **ANLZ-path hash quirk** flagged
  by the AnnoyingTechnology exporter before assuming free choice of folder names.
- **verify/** — reuses the in-tree read-only Kaitai parsers (`rekordbox_pdb_t`/`rekordbox_anlz_t`)
  to parse our own output back and diff against both (a) the model we intended and (b) a reference
  real-rekordbox stick. This is verification tiers 1–2 (below) as a library API.
- **diag/** — builds a shareable diagnostic bundle: export log, versions, a *structural* manifest
  (track/playlist counts, which sections written, verifier results) and hashes — **never the music
  files or full library metadata**. Powers the in-app bug report.

## Phasing

- **Phase 0 — standalone writer + MVP (the risky core).** `libopenboxxx-export` with pdb/anlz/usb/
  mapping/verify. Driven by a tiny CLI or test harness (feed a fixture library → write a stick).
  Exit criteria: verifier tiers 1–2 green, and a stick plays on a real CDJ (tier 4) with correct
  beatgrids + hot/memory cues.
- **Phase 1 — Mixxx integration.** Started with a **reader-path sub-step** (done):
  `openboxxx_from_mixxx` reads a Mixxx `mixxxdb.sqlite` directly into `ExportModel`
  (no Mixxx build required), so real libraries flow through the Phase-0 writer today
  and beta testers can export without compiling Mixxx. Units were decoded and
  validated against a real ~2,900-track library (cue positions = fractional stereo
  samples; beatgrid first-beat = frames; colours = `0x00RRGGBB`; `beats` BLOB is a
  small protobuf parsed by hand). The in-Mixxx path then reuses the identical model:
  add `RekordboxExportJob` (mirrors `EnginePrimeExportJob`:
  `QThread` + `loadIds/loadTrack/loadCrate/loadPlaylist` marshalling + `jobMaximum/jobProgress/
  completed/failed` signals — research-findings §A.3), a generalized export dialog, and a new CMake
  option mirroring `ENGINEPRIME`/`__ENGINEPRIME__`. Wire in the in-app bug report (diag/). Propose
  the shared exporter interface as part of this PR (engages upstream #12126/#10321).
- **Phase 2 — full fidelity.** Waveforms (`PWAV`/`PWV3`/`PWV4`/`PWV5`), extended cues with
  colors + labels (`PCO2`/`.EXT`), `PVBR`, `.2EX` for CDJ-3000. Additive ANLZ sections — Mixxx
  already has the waveform data + downsampling code in the Engine exporter to borrow.

## MVP scope (Phase 0 exit target)

**In:** PDB with `tracks` + referenced lookups (`artists`/`albums`/`genres`/`keys`/`colors`/
`artwork`) + `playlist_tree`/`playlist_entries`; per-track ANLZ `.DAT` = `PPTH` + `PQTZ` +
`PCOB`(memory) + `PCOB`(hot); audio files copied; correct `PIONEER/` layout.

**Out (deferred to Phase 2):** waveforms, `.EXT`/`PCO2` extended cues, `.2EX`, My-Tags
(`exportExt.pdb`), history playlists, OneLibrary (permanently out — encrypted; see legal.md).

Result: a CDJ browses the library + playlists, and every track plays with a correct beatgrid and
working hot/memory cues. The on-screen waveform is blank until Phase 2 — the track still plays.

## Verification ladder

Cheapest/fastest at top; run top tiers constantly, bottom tiers at milestones.

1. **Round-trip self-check** — write a stick, parse it back with the in-tree Kaitai parsers,
   confirm it reproduces the intended model. Free; runs on every build in CI.
2. **Diff vs a real rekordbox stick** — structural byte/field comparison against a stick that
   genuine rekordbox produced. Catches subtle layout mistakes the parser tolerates.
3. **Import into rekordbox desktop** — behavioral smoke test. Cheap gate; catches gross errors.
   ⚠️ **Not equivalent to hardware** — the desktop app is more forgiving (and may re-analyze or
   route through its own DB) than CDJ firmware. "Opens in rekordbox" ≠ "gig-ready." Note the
   rekordbox version used (5 vs 6/7 read exported PDBs differently).
4. **Real CDJ/XDJ hardware** — the only true proof. Done at milestones (first playable stick, then
   after waveforms), not daily. Target matrix per architecture.md: CDJ-2000NXS2, CDJ-3000/3000X,
   XDJ-AZ. Hardware access: a beta tester's / friend's unit initially — hence the emphasis on
   tiers 1–3 to arrive at hardware with high confidence.

## In-app bug reporting

Goal: one click in Mixxx turns a failed/odd export into a structured, reproducible report.

- **What it collects (via diag/):** Mixxx + module + format-target versions, host OS, the export
  log, a structural manifest (counts of tracks/playlists/crates, which PDB tables + ANLZ sections
  were written, per-track cue/beatgrid presence), verifier tier-1/2 results, and content **hashes**
  — enough to reproduce a structural bug without shipping anyone's music.
- **What it never collects:** audio files, full library metadata, or anything the user hasn't seen.
  The bundle is shown for review before it leaves the machine (privacy + it's GPLv2/open).
- **How it's sent:** write the bundle to a file the user can attach, and open a **prefilled GitHub
  issue** (title/labels/body templated from the manifest) — no telemetry server to run, no silent
  upload. A tester can redact and attach. (Auto-upload can come later if wanted, opt-in only.)
- **Tester-friendliness:** the manifest + verifier output often pinpoints the failing structure
  (e.g. "PQTZ present, PCOB hot missing on 3 tracks") before hardware is even involved, which is
  exactly what tiers 1–3 are for.

## Integration + licensing notes

- **License:** the module is **GPLv2** (Mixxx-compatible). ANLZ layout is **ported** from
  pyrekordbox (**MIT** → GPLv2-compatible; attribute it). PDB layout comes from crate-digger's
  `.ksy` **spec** (EPL-1.0) as a format description, not copied code. `kimtore/rex` is
  **unlicensed** → learn-from-only, never copy. Confirm the license of
  `AnnoyingTechnology/rhythmbox-to-pioneer-xdj-exporter` before reusing anything from it.
- **Upstreamability:** keeping the byte-writers in a clean standalone lib with its own tests makes
  the eventual Mixxx PR far smaller and easier to review — the PR is mostly the adapter + dialog +
  CMake option, with the scary serialization already tested independently.

## Open questions carried into Phase 0

- The `track_row` "always X" magic constants + `bitmask` semantics (research-findings §B.6) — pin
  down from a real export before guessing.
- PDB empty-leading-page convention + `first_page`/`last_page` semantics — validate on hardware.
- ANLZ-path hash quirk — confirm whether newer firmware requires a specific folder-path hash.
- Which rekordbox desktop version(s) to standardize on for verification tier 3.
