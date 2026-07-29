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

Early scoping. No code yet — this repo currently holds the project vision, the legal position,
and the architecture plan. Next up: architecture spec for the export module + a hardware test
matrix.

## Docs

- [docs/legal.md](docs/legal.md) — how we stay within legal bounds (the important one)
- [docs/architecture.md](docs/architecture.md) — the export module design & reuse map

## License

Because this builds on Mixxx (GPLv2), this project is **GPLv2** as well. That copyleft is a
feature, not a bug — it keeps the work open, which is the whole point.
