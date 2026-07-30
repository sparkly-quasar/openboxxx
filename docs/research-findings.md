# openboxxx research findings (collected 2026-07-29)

Consolidated data-collection pass feeding the architecture phase. Two areas: (A) how Mixxx's
existing library-export code is structured and what an exporter can reuse, and (B) the byte-level
PDB/ANLZ format a serializer must produce. Everything here is source-cited; treat the cited files
as the copy-source of record.

---

## PART A — Mixxx library-export architecture

Repo `github.com/mixxxdj/mixxx` (GPLv2), inspected at `main`.

### A.1 There is no generic exporter interface yet
Mixxx has **exactly one** exporter: the **Engine DJ / Engine Prime** exporter in
`src/library/export/`, hardwired to the third-party `libdjinterop` library and compiled behind the
CMake `option(ENGINEPRIME ...)` / `__ENGINEPRIME__` define. The generic interface openboxxx hoped
to implement **does not exist** — issue #12126 ("Abstract over library exporters") is a *request*
for it with discussion but no interface, no draft code, and no linked PR.

Consequence: openboxxx builds a `RekordboxExportJob` mirroring the Engine-Prime classes, behind a
new CMake option, and proposes the shared abstraction as part of that PR.

### A.2 File / interface map (`src/library/export/`)
| Path | Role |
|---|---|
| `libraryexporter.{h,cpp}` | `mixxx::LibraryExporter : QWidget`. Entry object; slots `slotRequestExport()`, `…WithInitialCrate(CrateId)`, `…WithInitialPlaylist(int)`. Spawns the export job on a background thread; owns progress dialog + result message boxes. |
| `dlglibraryexport.{h,cpp}` | `mixxx::DlgLibraryExport : QDialog`. Gathers options (whole-library vs. selected crates/playlists, output dir, Engine schema version). Emits `startEnginePrimeExport(QSharedPointer<EnginePrimeExportRequest>)`. Header comment already anticipates other formats. |
| `engineprimeexportrequest.h` | `struct EnginePrimeExportRequest` — request DTO: dirs, schema version, `QSet<CrateId>`, `QSet<int>` playlists. Empty sets ⇒ whole library. |
| `engineprimeexportjob.{h,cpp}` | `EnginePrimeExportJob : QThread` (~840 lines) — **the template to mirror.** |
| `coverartcopyworker.{h,cpp}` | Cover-art copy helper. |
| `trackexport*.{h,cpp,ui}` | Separate older feature (export raw audio files to a folder); unrelated to DB export. |

Wiring (all `#ifdef __ENGINEPRIME__`): factory `Library::makeLibraryExporter()` in
`src/library/library.cpp`; menu/sidebar/crate/playlist actions in `mixxxmainwindow.cpp`,
`wmainmenubar.cpp`, `mixxxlibraryfeature.cpp`, `baseplaylistfeature.cpp`, crate feature. Build glue
in `CMakeLists.txt` (fetches/builds `libdjinterop`).

### A.3 The reusable job pattern (`EnginePrimeExportJob`)
Runs on its own `QThread::run()`; all Mixxx DB reads marshalled to the `TrackCollectionManager`
thread via `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)` in private slots
`loadIds / loadTrack / loadCrate / loadPlaylist`. Signals: `jobMaximum / jobProgress / completed /
failed`; `slotCancel()` sets an atomic flag polled in the loops.

`run()`: mkpath → `loadIds` (whole-library enumeration via `DirectoryDAO::loadAllDirectories` →
`TrackDAO::getAllTrackRefs`, crates via `collectCrateIdsOfTracks`, playlists via
`PlaylistDAO::getPlaylists(PLHT_NOT_HIDDEN)`; or resolve the explicit selection) → open target DB →
per track `loadTrack` + `exportTrack` → root crate/playlist → per crate/playlist export.

`exportMetadata()` is the field-mapping core (scalars, key table, rating×20, main cue, beatgrid,
hot cues/loops capped at 8, waveform). **A `RekordboxExportJob` keeps this scaffolding verbatim and
only swaps the `exportMetadata/exportCrate/exportPlaylist` bodies to write PDB/ANLZ.**

### A.4 The importer is the field-mapping goldmine (`src/library/rekordbox/`)
`RekordboxFeature : BaseExternalLibraryFeature` (~1600 lines) already reads a mounted USB into
Mixxx. It encodes the exact Mixxx↔rekordbox mapping an exporter runs **in reverse**:
- **Colors** — `colorFromID(int)` + `enum IDForColor {Pink=1,Red,Orange,Yellow,Green,Aqua,Blue,Purple}`
  with RGB constants (Pink `0xF870F8`, Red `0xF87090`, Orange `0xF8A030`, Yellow `0xF8E331`,
  Green `0x1EE000`, Aqua `0x16C0F8`, Blue `0x0150F8`, Purple `0x9808F8`). Invert to RGB→nearest ID.
- **Track scalars** — `insertTrack()` maps `track_row_t` → title/artist/album/year/genre/location/
  `tempo÷100→bpm`/bitrate/key/duration/rating/comment/track_number/analyze_path/color.
- **Beatgrids** — `readAnalyze()` converts `beat->time()` (ms) → frames via `sampleRateKhz*ms`
  (minus a `timingOffset`), then `Beats::fromBeatPositions(...)`. Reverse: Mixxx frame → ms.
- **Cues/loops** — hot cues (`hot_cue()-1` 0-based), memory cues, loops (`loop_time()` end);
  first non-loop memory cue becomes Mixxx `MainCue`. Extended tags carry RGB + UTF-16BE labels.
- **Time base** — rekordbox = **milliseconds**; Mixxx = `audio::FramePos`; `frame = (sr/1000)*ms`.

Reusable directly: the color table, key map, ms↔frame math, cue/loop/beatgrid semantics.

### A.5 In-tree Kaitai definitions are READ-ONLY (`lib/rekordbox-metadata/`)
`rekordbox_pdb.ksy` / `rekordbox_anlz.ksy` (from Deep-Symmetry crate-digger) + generated C++11
parsers `rekordbox_pdb_t` / `rekordbox_anlz_t`. Confirmed: grep for `_write|serialize|to_file`
returns **0 matches** — parsers only. Good as schema reference; **cannot write**. Serialization is
net-new code.

### A.6 Upstream issue state
- **#12126** "Abstract over library exporters" — OPEN, no assignee, no PR. Names Rekordbox (#10321)
  + iTunes as the formats to unify; wants `Library > Export Library…` and a per-format interface.
- **#10321** "Rekordbox USB Export (PDB/ANLZ)" — OPEN, confirmed, no assignee. **Dec 2025:** a
  commenter reports a working LLM-assisted writer at
  `AnnoyingTechnology/rhythmbox-to-pioneer-xdj-exporter`, tested on XDJ hardware, with a documented
  **ANLZ-path hash quirk** (`PIONEER.md#anlz-path-hash-algorithm`), and invites a Mixxx port.
- **#9463** older umbrella dupe — OPEN, no assignee. Mentions external tools `TheKantankerus/
  MixxxToRekordbox`, `FrankwaP/mixxx-utils`, `ambientsound/rex`, `arximboldi/mixxx-db-tools`.

No Mixxx PR is claimed on any of these — the work is unclaimed upstream.

---

## PART B — PDB/ANLZ serializer design reference

Two independent binary formats, **opposite endianness**. `export.pdb` = **little-endian**
DeviceSQL page DB. ANLZ (`.DAT/.EXT/.2EX`) = **big-endian** tagged-section container. Do not share
endian helpers.

### B.1 USB directory layout
```
/PIONEER/
  rekordbox/export.pdb        classic DeviceSQL DB (LE)  <-- primary target
  rekordbox/exportExt.pdb     optional My-Tag tables
  rekordbox/exportLibrary.db  OneLibrary only (SQLCipher) -- NOT targeted
  USBANLZ/P<xxx>/<8-hex>/ANLZ0000.DAT / .EXT / .2EX   per-track analysis
  MYSETTING*.DAT / DEVSETTING.DAT                     player settings
/<audio files anywhere>
```
**Track⇄ANLZ linkage is by stored string, not by algorithm.** `track_row.analyze_path` (string idx
14) holds e.g. `/PIONEER/USBANLZ/P016/0000875E/ANLZ0000.DAT`; the CDJ derives `.EXT`/`.2EX` by
extension swap. `file_path` (idx 20) holds the audio path. The writer chooses folder names freely
as long as the DB string matches the file actually written.

### B.2 PDB (`export.pdb`, little-endian, 4096-byte pages)
**File header (off 0):** `zero:u4` · `len_page:u4` (**use 4096**) · `num_tables:u4` ·
`next_unused_page:u4` · `unknown:u4` · `sequence:u4` (edit counter; fresh export → small const) ·
zero gap · `tables[num_tables]` (16 bytes each: `type:u4 · empty_candidate:u4 · first_page:u4 ·
last_page:u4`, page refs = indices, byte offset = index × len_page).

`page_type` enum: 0 tracks, 1 genres, 2 artists, 3 albums, 4 labels, 5 keys, 6 colors,
7 playlist_tree, 8 playlist_entries, 11 history_playlists, 12 history_entries, 13 artwork,
16 columns, 19 history. rekordbox emits **all ~20 tables even when empty**.

**Page (each len_page block):** ~0x28-byte header, heap grows **forward** from 0x28, row index
grows **backward** from page end. Header fields: `page_index:u4` (== own index) · `type:u4` ·
`next_page:u4` · `sequence:u4` · `num_row_offsets` (13 bits) + `num_rows` (11 bits) at 0x18 ·
`page_flags:u1` at 0x1b (**data page iff `flags & 0x40 == 0`**) · `free_size:u2` · `used_size:u2` ·
`transaction_row_count:u2` · `transaction_row_index:u2`.

**Writer quirk:** the *first* page of each table chain is conventionally an empty/garbage page;
real rows start on the second linked page. Simplest robust approach: emit one empty leading page
per table, then data pages.

**Row index (backward from page end):** rows grouped in blocks of **16**;
`num_row_groups = (num_row_offsets-1)/16 + 1`; each group = **0x24 (36) bytes** at
`base = len_page - group_index*0x24`. `row_present_flags:u2` at `base-4` (bit i ⇒ row i present).
16 row offsets `u2` at `base-(6+2*i)`, offset = bytes past the 0x28 header (`row_base = 0x28+ofs`).
Page is full when backward index would collide with forward heap → new page, link via `next_page`.
Keep `free_size/used_size/num_rows/num_row_offsets` consistent with what was packed.

**`device_sql_string` (every variable string):** leading `length_and_kind` byte:
- odd ⇒ **short ASCII**, `length_and_kind = (text_len+1)*2 + 1`, then text; empty = `0x03`
  (rekordbox pads unused slots with `0x03`).
- `0x40` ⇒ **long ASCII**: `u2 length` (incl. 4-byte header), `u1` pad, `length-4` ASCII bytes.
- `0x90` ⇒ **long UTF-16LE**: `u2 length`, `u1` pad, `length-4` bytes. Use for non-ASCII.

**Rows a writer needs:** `track_row` (large: subtype 0x24, bitmask, sample_rate, file_size, FK ids
`artwork/key/label/remixer/genre/album/artist/original_artist/composer`, bitrate, track_number,
`tempo`=BPM×100, disc, play_count, year, sample_depth, `duration` s, `color_id:u1`, `rating:u1`,
`id:u4`, then **21 × u2 `ofs_strings`**; playback-critical strings: 14 analyze_path, 17 title,
19 filename, 20 file_path — emit `0x03` for empties). Lookup rows `artist/album/label/genre/key/
color` (id+name; album also artist_id; color id is u2). `artwork_row` (id+path).
`playlist_tree_row` (parent_id, sort_order, id, raw_is_folder, name). `playlist_entry_row`
(entry_index, track_id, playlist_id). Full layout: `crate-digger .../rekordbox_pdb.ksy`.

### B.3 ANLZ (`.DAT/.EXT/.2EX`, big-endian)
**Container:** `"PMAI"` · `len_header:u4` (≈28) · `len_file:u4` · pad · then sections to EOF. Each
section: `fourcc:4` · `len_header:u4` · `len_tag:u4` (whole section) · body. Patch `PMAI.len_file`
after writing.

| tag | meaning | file | needed for basic prep |
|---|---|---|---|
| **PPTH** | audio path (UTF-16BE) | .DAT | yes (identity) |
| **PQTZ** | beat grid | .DAT | **yes** |
| **PCOB** | cue/loop list (legacy) | .DAT | **yes** |
| PCO2 | extended cues (names/colors, nxs2) | .EXT | recommended |
| PVBR | VBR seek index | .DAT | recommended for VBR/MP3 |
| PWAV/PWV2 | waveform preview | .DAT | cosmetic |
| PWV3/PWV4/PWV5 | detail/color waveforms | .EXT | cosmetic (nxs2) |
| PWV6/PWV7 | 3-band | .2EX | cosmetic (CDJ-3000) |
| PSSI | song structure (XOR-masked) | .EXT | optional |

**PQTZ beat grid** (`len_header=24`): `u4=0` · `u4=0x80000` · `num_beats:u4` · then num_beats ×
`{beat_number:u2 (1..4), tempo:u2 (BPM×100), time:u4 (ms)}`.

**PCOB cue list** (`len_header=24`): `cue_type:u4` (0=memory,1=hot) · `u2` · `num_cues:u2` ·
`memory_count:u4` · then num_cues × **PCPT** entries: `"PCPT" · len_header:u4 · len_entry:u4 ·
hot_cue:u4 (0=memory else #) · status:u4 · u4=0x10000 · order_first:u2 · order_last:u2 · type:u1
(1=cue,2=loop) · pad3 · time:u4 (ms) · loop_time:u4 (ms, -1 if not loop) · pad16`. Emit **two PCOB
sections** (memory + hot). Modern CDJs prefer PCO2/PCP2 in `.EXT` (adds UTF-16BE comment, color_id,
RGB) — worth writing both, but PCOB alone makes cues jump. Exact structs: `pyrekordbox anlz/
structs.py` (MIT) — has a working `AnlzFile.build()/save()`.

**Minimum ANLZ per track:** `.DAT` = PMAI + **PPTH + PQTZ + PCOB(memory) + PCOB(hot)** (+ PVBR for
VBR). Waveforms optional (track plays, grids/cues work, only on-screen waveform blank). Add `.EXT`
(PCO2 + PWV3/4/5) later for modern-player polish.

### B.4 Classic vs OneLibrary encryption
- **Classic `export.pdb`: UNENCRYPTED.** Plain page file; a writer circumvents nothing. **Target.**
- **OneLibrary `exportLibrary.db`: SQLCipher-encrypted** (rekordbox 6.8+). **Out of scope** —
  do not pursue the key. CDJs still read classic `export.pdb`, so targeting classic stays valid.

### B.5 Prior-art writers + licenses (the copy-source decision)
| Project | PDB | ANLZ | R/W | License | Use |
|---|---|---|---|---|---|
| **pyrekordbox** (dylanljones) | no `export.pdb` (its db is desktop `master.db` SQLite) | yes | ANLZ read **+ write** | **MIT** | **Primary ANLZ serializer reference — port the structs** |
| **crate-digger** (Deep-Symmetry) | spec+parse | parse | read-only | EPL-1.0 (`.ksy`) | **Authoritative PDB byte-layout spec** |
| **rekordcrate** (Holzhaus) | parse | parse | read-only | copyleft | Cross-check parser |
| **kimtore/rex** (Go) | **writes export.pdb** | no | write | **NONE (all-rights-reserved)** | Learn-only, **do not copy**; also lacks grids/cues/waveforms |
| **AnnoyingTechnology/rhythmbox-to-pioneer-xdj-exporter** | writes | writes | write | (verify) | LLM-assisted, XDJ-tested; documents ANLZ-path hash quirk; author invited Mixxx port |

**Net:** no permissively-licensed project writes a *complete* CDJ-playable export today. ANLZ
writing is effectively solved by pyrekordbox (MIT, portable). The **PDB writer is genuinely
net-new** — rex proves the page/table skeleton is feasible but omits the analysis data that
matters and its license bars copying; the byte layout comes from crate-digger's `.ksy` spec.

### B.6 Open questions / risks
- `track_row` "always X" constants (`19048`, `30967`, `41`, the `bitmask`, `2/3` alternator) are
  not fully understood — copy observed values from a real export + crate-digger notes, don't guess.
- Empty leading page / `first_page` vs `last_page` semantics — decide layout, validate on hardware.
- `num_row_offsets` vs `num_rows` + transaction fields — set cleanly for from-scratch (offsets ==
  rows, txn 0), confirm players don't reject.
- PCPT `order_first`/`order_last` (`0xffff` sentinels) must be consistent across the cue list.
- PSSI XOR masking — only if writing song structure; skip for MVP.
- **Firmware acceptance is the real test** — plan iteration on a real CDJ-2000NXS2 / CDJ-3000.
- **ANLZ-path hash quirk** flagged by the AnnoyingTechnology exporter — verify whether newer
  firmware requires a specific hash in the ANLZ folder path vs. the free-choice model above.

---

## Sources
- Mixxx: `src/library/export/`, `src/library/rekordbox/rekordboxfeature.cpp`,
  `lib/rekordbox-metadata/`; issues #12126, #10321, #9463.
- crate-digger (EPL-1.0) `rekordbox_pdb.ksy` / `rekordbox_anlz.ksy`; DJ Link Ecosystem Analysis
  (djl-analysis.deepsymmetry.org).
- pyrekordbox (MIT) `anlz/structs.py`, `anlz/file.py`.
- kimtore/rex (Go, unlicensed); rekordcrate (Rust); AnnoyingTechnology/rhythmbox-to-pioneer-xdj-exporter.
- 0xdevalias gist (OneLibrary/SQLCipher notes).
