# State of the Mixxx rekordbox-export effort (researched 2026-07-29)

Bottom line: **rekordbox USB export is NOT implemented in Mixxx.** It's a long-standing,
"confirmed" wishlist item with no assignee, no branch, and no merged PR. But a lot of the
groundwork Mixxx would build on already exists — importer code, Kaitai format definitions, and a
working *Engine DJ* exporter that establishes the pattern. The gap is real but bounded.

## The relevant issues

| Issue | What it is | Status |
|---|---|---|
| [#9463](https://github.com/mixxxdj/mixxx/issues/9463) | Original "export crates/cues/beatgrids to rekordbox USB" request (2018, migrated from Launchpad) | Open, confirmed, wishlist. No PRs. |
| [#10321](https://github.com/mixxxdj/mixxx/issues/10321) | More specific "Rekordbox USB Export (PDB/ANLZ)" request (2021) | Open, confirmed. No PRs, no assignee. |
| [#12126](https://github.com/mixxxdj/mixxx/issues/12126) | "Abstract over library exporters" (2023) — **the architectural hook** | Open. No PRs. |
| [#15556](https://github.com/mixxxdj/mixxx/issues/15556) | "Add OneLibrary support" (Oct 2025) | Open, confirmed. No PRs. Blocked on spec. |
| [PR #13293](https://github.com/mixxxdj/mixxx/pull/13293) | Updated rekordbox Kaitai definitions (Swiftb0y) | Import-side maintenance. |

## What already EXISTS in Mixxx (build on this)

1. **A working Engine DJ / Engine Prime exporter.** This is the precedent: Mixxx already knows how
   to walk its library and write out a foreign DJ-software database (`Library > Export Library to
   Engine Prime`). A rekordbox exporter should follow the same shape.
2. **An exporter-abstraction plan (#12126).** The intent is to replace the Engine-specific menu
   item with a generic `Library > Export Library…` and a common interface each format implements.
   **This is where our rekordbox exporter plugs in** — implement that interface, don't bolt on a
   one-off.
3. **rekordbox format definitions already in-tree.** The importer uses Kaitai-generated
   `rekordbox_pdb` / `rekordbox_anlz` parsers (`lib/rekordbox-metadata/`, kept current by PR
   #13293). We already have the *read* side of PDB/ANLZ described.
4. **A mature importer (since v2.3, 2020).** The Mixxx→rekordbox field mapping (hotcues, memory
   cues, loops, colors) is already worked out in reverse; the exporter re-uses that mapping the
   other direction.

## The core technical blocker: Kaitai can only READ

Kaitai Struct's C++ target generates **parsers, not serializers.** Mixxx can *parse* PDB/ANLZ
today, but there is no generated code to *write* them. So the export work requires one of:

- **Hand-written serializers** for the PDB (DeviceSQL page/table layout) and ANLZ (analysis
  section) formats — the most likely path, using pyrekordbox (MIT) as the byte-layout reference.
- Kaitai's experimental/partial write support (limited, not really viable for C++ today).

This — a PDB/ANLZ **writer** — is the single biggest chunk of net-new code, and it's exactly the
piece our project should own and upstream.

## Strategy revision: OneLibrary is the LEGALLY RISKIER target, not the safer one

A key discovery from the reverse-engineering notes
([gist](https://gist.github.com/0xdevalias/b803476793b56f7c45e6361799168eb0)):

- **Classic `export.pdb` (legacy CDJ format): UNENCRYPTED.** Writing it circumvents nothing. Safe.
- **OneLibrary / Device Library Plus `exportLibrary.db`: SQLCipher-encrypted (256-bit AES).** The
  key is recovered via base85-decode → XOR (`657f48f84c437cc1`) → zlib-inflate. It's a *fixed,
  non-license* key shared across all Device Libraries — but it's still an **extracted encryption
  key**, so writing OneLibrary means circumventing SQLCipher. That is precisely the DMCA §1201
  exposure [docs/legal.md](legal.md) says to avoid.

**Consequence:** the "newest, sanctioned" format is actually the one we should NOT reverse-engineer
key-in-hand. Revised priority:

1. **Classic PDB/ANLZ** — widest hardware reach AND legally cleanest. This is the whole focus.
2. **OneLibrary** — pursue ONLY via an official AlphaTheta spec/partnership (#15556 notes they
   "welcome partnership discussions"). Do not ship the extracted SQLCipher key.

## What this means for openboxxx — concrete next steps

- [ ] Confirm in-repo: read `lib/rekordbox-metadata/` and the Engine DJ exporter
      (`src/library/export/` area) to see the exact interface a new exporter implements.
- [ ] Prototype a **standalone PDB/ANLZ writer** (can start as a small C++ or even Python spike
      using pyrekordbox to validate our output byte-for-byte) before touching Mixxx.
- [ ] Build the **round-trip verifier** early — parse our own output with the existing Kaitai
      parsers and diff against a reference rekordbox-generated USB.
- [ ] Engage issue #12126 / #10321 upstream so the work lands as a contribution, not a fork.
- [ ] Keep OneLibrary strictly behind "official spec only."

## Sources

- https://github.com/mixxxdj/mixxx/issues/9463
- https://github.com/mixxxdj/mixxx/issues/10321
- https://github.com/mixxxdj/mixxx/issues/12126
- https://github.com/mixxxdj/mixxx/issues/15556
- https://github.com/mixxxdj/mixxx/pull/13293
- https://gist.github.com/0xdevalias/b803476793b56f7c45e6361799168eb0
