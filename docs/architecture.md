# Architecture

## Framing: fork vs upstream

Mixxx is GPLv2, so any fork *stays* open. But the preferred path is a **feature branch upstreamed
into Mixxx proper**, not a hard fork:

- CDJ export is already a wanted, open issue upstream (mixxxdj/mixxx#9463).
- Upstreaming inherits their test base, credibility, and maintenance help instead of splitting the
  community.
- Fork only if we need to move faster than upstream will accept, or take the UX somewhere they'd
  reject.

## The export pipeline (the core deliverable)

```
Mixxx library (tracks + cues + beatgrids + waveforms + crates/playlists)
        │
        ▼
  [ mapping layer ]  ── translate Mixxx's data model to rekordbox's
        │
        ▼
  [ PDB writer ]     ── unencrypted export.pdb (DeviceSQL) : tracks, playlists, artwork refs
  [ ANLZ writer ]    ── .DAT/.EXT analysis files : waveforms, beatgrids, cues/hotcues, loops
        │
        ▼
  USB layout (PIONEER/ dir structure + audio files copied)
        │
        ▼
  [ verifier ]       ── round-trip parse the written USB, report fidelity BEFORE the DJ leaves
```

### Component notes

- **Mapping layer** — the fiddly part. Mixxx hotcues → rekordbox hotcues; Mixxx main cue →
  rekordbox first memory cue; loops; cue colors; beatgrid representation differences.
- **PDB writer** — DeviceSQL page/table format. Reference: pyrekordbox source + dysentery specs.
- **ANLZ writer** — the fidelity-critical part. Waveform blobs and beatgrid structures have
  version variants; newer CDJs (3000/3000X) are STRICTER about versions than older ones.
- **Verifier** — the trust feature. Parse our own output back and report
  "playlists 12/12, hotcues OK, beatgrids OK". Turns "I hope" into "I checked."

## Compatibility targets (priority order)

1. **Legacy PDB/ANLZ** — baseline. Read by essentially every export-capable CDJ from ~2010 on
   (CDJ-2000NXS2 and the huge installed base). Widest reach, best-understood format.
2. **OneLibrary / Device Library Plus** — AlphaTheta's own cross-software format (Oct 2025), only
   newest firmware/models: CDJ-3000X, CDJ-3000 (fw 3.30+), XDJ-AZ, OPUS-QUAD, OMNIS-DUO.
   ⚠️ **Legally the harder target, not the easier one:** its `exportLibrary.db` is
   SQLCipher-encrypted, so writing it means circumventing encryption. Pursue ONLY via an official
   spec/partnership. See [legal.md](legal.md) and [mixxx-export-status.md](mixxx-export-status.md).

## Hardware test matrix (to define next)

Trust is earned by a published compatibility table. Minimum credible launch set:

- CDJ-2000NXS2 (legacy baseline)
- CDJ-3000 / 3000X (strict, modern)
- XDJ-AZ (all-in-one, modern)

For each: playlists load? hotcues? memory cues? beatgrids? waveforms? artwork?

## Known moving-target risk

AlphaTheta ships new firmware/format versions on their schedule. A 3000X firmware update could
tighten validation and break an edge case. Mitigation: the test matrix + verifier + a fast-moving
open community so breaks get caught and patched in days, not never.

## Detailed design

The implementation architecture (standalone writer library, MVP scope, cross-platform rules,
verification ladder, in-app bug reporting) lives in [export-design.md](export-design.md).

## Open research tasks

- [x] Survey the state of the Mixxx export effort → see [mixxx-export-status.md](mixxx-export-status.md).
- [x] Confirm in-repo: the Engine DJ exporter interface (#12126) + `lib/rekordbox-metadata/` → [research-findings.md](research-findings.md) §A.
- [x] Deep-read pyrekordbox's ANLZ/PDB writers to map the byte layout → [research-findings.md](research-findings.md) §B.
- [ ] Prototype a standalone PDB/ANLZ writer + round-trip verifier (spike, before touching Mixxx) → Phase 0, see [export-design.md](export-design.md).
- [x] Define the exact Mixxx→rekordbox cue/beatgrid mapping → mapping inverts importer, [research-findings.md](research-findings.md) §A.4.
