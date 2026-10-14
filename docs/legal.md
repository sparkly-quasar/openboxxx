# Legal position

> This is engineering guidance, not legal advice. Before any public release that includes OneLibrary
> support, get an IP lawyer to review the interoperability position for the relevant jurisdictions.

The goal: build a genuinely open, useful tool, and be deliberate about the one area with real legal
risk, which is encryption.

## What changed (2026-10-14)

This document used to say "never ship the OneLibrary key" and treated OneLibrary as permanently out of
scope. That has changed, for two reasons:

- **The newest players can't read anything else.** Per AlphaTheta's
  [USB notice](https://alphatheta.com/en/information/important-notice-for-customers-using-usb-devices-with-our-dj-equipment/),
  the CDJ-3000X, XDJ-AZ, OPUS-QUAD and OMNIS-DUO read OneLibrary, not the older Device Library.
  Leaving OneLibrary out means leaving out current club hardware. rekordbox 7.2.11+ writes both
  formats to every stick.
- **The passphrase is fixed and already public.** It's the same on every install, isn't tied to a user
  or licence, and is already distributed in open-source code, including libdjinterop's OneLibrary pull
  requests (xsco/libdjinterop#196, xsco/libdjinterop#204), which use it as the default.

**OneLibrary is now in scope for openboxxx.** For Mixxx itself, shipping it is the Mixxx maintainers'
decision (and possibly the Mixxx e.V.'s); the proposal (mixxxdj/proposals#20) sets out the options
neutrally rather than arguing for one.

## What's encrypted and what we do with it

| File | Encrypted? | openboxxx |
|---|---|---|
| Device Library: `export.pdb` + ANLZ files | No | Write it. There is nothing to circumvent. |
| OneLibrary: `exportLibrary.db` | SQLCipher, fixed passphrase | In scope, under the rules below |
| rekordbox application database: `master.db` | SQLCipher | Never read or decrypt it |
| `rekordbox.xml` | No (official interchange format) | Read and write freely |

## Rules we follow

1. **Never decrypt a user's rekordbox application database (`master.db`).** If someone wants their
   rekordbox library in Mixxx, they export `rekordbox.xml` from their own rekordbox.
2. **Use the OneLibrary passphrase only for interoperability:** writing the user's own library to their
   own stick, and reading sticks that rekordbox or openboxxx produced. Never to unlock anything else.
3. **Keep OneLibrary support behind a build option**, with the passphrase in one clearly marked place,
   so it can be compiled out or removed quickly (for example if a packager declines it, or AlphaTheta
   objects).
4. **Describe it as interoperability, not "cracking."** Docs and UI talk about writing OneLibrary, not
   bypassing encryption.
5. **Keep asking AlphaTheta for the official specification.** Comments in mixxxdj/mixxx#15556 report
   it has been shared with some other vendors. If it becomes available on acceptable terms, prefer it.

Technical note: the passphrase isn't stored in plain text in rekordbox. Public tools recover it by
base85-decoding, XOR-ing with `657f48f84c437cc1`, and zlib-inflating. Use the published value rather
than extracting it from rekordbox ourselves.

## Why we think this is defensible, and the risks

**In favour:**

- The purpose is interoperability: letting a user's own library work on hardware they own. The
  encrypted database holds the user's own metadata, not third-party content.
- US law has an interoperability provision in this area (17 U.S.C. §1201(f)). Whether it applies here
  is a legal question.
- The same code is landing in libdjinterop, which Mixxx already depends on.

**Risks:**

- US law also restricts distributing tools that get around encryption (§1201(a)(2)). A key being public
  isn't by itself a defence.
- Takedown or cease-and-desist requests are possible, aimed at us or at packagers. Linux distributions
  and app stores may have their own policies.
- AlphaTheta could change the passphrase or format in a firmware or rekordbox update.
- It could affect a future partnership with AlphaTheta.

## License compatibility

Mixxx is GPL-licensed (version 2; check the exact "or later" terms before relying on compatibility
with LGPL-3.0 or GPL-3.0 code). Anything linked into it must be compatible.

| Project | License | Notes |
|---|---|---|
| pyrekordbox | MIT | Reference for the ANLZ layout. Ported code must keep its MIT notice. |
| rekordcrate | MPL-2.0 | GPL-compatible. Device export from scratch in progress (Holzhaus/rekordcrate#259). |
| libdjinterop | LGPL-3.0 | Already a Mixxx dependency (Engine DJ export); OneLibrary support in progress. |
| crate-digger | EPL-2.0, with MPL-2.0 and LGPL-3.0 as secondary licences | Mixxx already links Kaitai code generated from its `.ksy` files (`lib/rekordbox-metadata/`). Check which licence applies before reusing anything beyond the format description. |
| fragmede/rekordbox-pdb | MIT | `FORMAT.md` documents the write-side `export.pdb` invariants. |

## Housekeeping (open)

- [ ] Add a LICENSE file. The SPDX headers say GPL-2.0-or-later, but the repo has no licence file, so
  GitHub reports none.
- [ ] Add pyrekordbox's MIT notice to `libopenboxxx-export/src/anlz/anlz_writer.cpp` and any other code
  ported from it.

## Clean-room-ish implementation

This applies to openboxxx's own writer. It may matter less if Mixxx adopts rekordcrate or libdjinterop
as the backend (see the proposal).

A file format itself isn't copyrightable; only a specific code expression of it is. So we implement
writers in our own code, using public format documentation (Deep Symmetry's specs, `FORMAT.md`,
pyrekordbox's source) as reference for the byte layout. We inherit the knowledge, not the code. This
is also how Mixxx's existing rekordbox importer was built.
