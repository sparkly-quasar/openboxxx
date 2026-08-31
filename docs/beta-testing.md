# Beta testing openboxxx

**Thank you.** openboxxx writes a rekordbox/CDJ USB stick from a Mixxx library. The
code says the bytes are correct. Only you can tell us whether a **real CDJ** agrees.

This guide takes about 20 minutes end to end. No prior experience needed — if you can
plug a USB stick into a CDJ, you can run this test.

---

## Read this first

openboxxx has never been confirmed on physical hardware. That is exactly what we're
asking you to find out, so please plan for it not to work.

| | |
|---|---|
| ⚠️ **Do not use this at a paying gig.** | Bring your normal USB as well. Always. |
| 💾 **Use a spare USB stick.** | The export writes to the stick. Don't reuse your working one. |
| 🔒 **Your Mixxx library is safe.** | Both tools open `mixxxdb.sqlite` **read-only** and never write to it. Back it up anyway. |
| 🎛️ **Keep your normal Mixxx installed.** | The beta build is a separate, unofficial fork build. |
| 🌊 **Waveforms will be blank.** | Not a bug — not implemented yet (Phase 2). The track still plays. |

**A malformed database cannot silence your music.** Worst case a CDJ ignores the
database and you browse the stick by folder — you lose the prepared metadata, not the
audio.

---

## Step 1 — Prepare the USB stick

CDJs are much fussier about the stick than about the files on it. Get this wrong and
you'll report a bug that isn't ours.

1. Use a stick **8 GB or larger**, ideally USB 3.0, from a brand you recognise.
2. Format it **FAT32** — the safest choice, readable by every CDJ ever made.
   - **exFAT** also works on CDJ-2000NXS2 and newer, and lifts FAT32's 4 GB
     per-file limit. Older players will not see an exFAT stick at all.
   - **NTFS, APFS and HFS+ do not work.** A CDJ will not mount them.
3. Use a **single partition**, MBR partition table.
4. Start from an **empty** stick — no leftover `PIONEER/` folder from rekordbox.

> Formatting guidance above is standard CDJ practice, not something openboxxx itself
> verified. If your player is unusual, check its manual.

<details>
<summary>How to format — macOS / Windows / Linux</summary>

- **macOS:** Disk Utility → select the *device* (not the volume) → Erase →
  Format **MS-DOS (FAT)**, Scheme **Master Boot Record**.
- **Windows:** File Explorer → right-click the drive → Format → File system **FAT32**
  (use [rufus](https://rufus.ie) if Windows won't offer FAT32 on a large stick).
- **Linux:** `sudo mkfs.vfat -F 32 /dev/sdX1` — check `lsblk` **twice** before running,
  this erases the target.

</details>

---

## Step 2 — Export your library

Two ways to do it. They run the **same** exporter and produce the same stick —
follow **Path A** or **Path B** below, not both.

| | Path A — Mixxx app | Path B — command line |
|---|---|---|
| **Best for** | Most testers, especially DJs | Developers, or anyone who'd rather not install a fork |
| **You need** | To install an unofficial Mixxx build | A terminal and a compiler |
| **You get** | A button inside Mixxx | A standalone tool; your normal Mixxx is untouched |
| **Test subset?** | No — whole library only | Yes — `--limit` / `--ids` |

If you have the choice, **Path B is the better first test** — it can export 20 tracks
instead of 3,000, which is exactly what you want on a first trip to a CDJ.

---

## Path A — Export from the Mixxx app

### A1. Download the beta build

Go to the [**v0.1.0-alpha release**](https://github.com/sparkly-quasar/openboxxx/releases/tag/v0.1.0-alpha)
and download the file for your system:

| Your system | File ending in |
|---|---|
| macOS, Apple Silicon (M1–M4) | `-arm64.dmg` |
| macOS, Intel | `-x86_64.dmg` |
| Windows 10/11, 64-bit | `-amd64.msi` |
| Linux, Debian/Ubuntu | `-x86_64.deb` |

Not sure which Mac you have?  → **Apple menu → About This Mac.** "Apple M…" means
Apple Silicon; "Intel" means Intel.

### A2. Open it despite the security warning

These builds are **not code-signed**, so your operating system will object. That
warning is expected — it means "unsigned", not "malicious".

- **macOS:** right-click the app → **Open** → **Open**. If that's refused, go to
  System Settings → Privacy & Security → **Open Anyway**.
- **Windows:** SmartScreen appears → **More info** → **Run anyway**.
- **Linux:** `sudo dpkg -i mixxx-*.deb`, then `sudo apt -f install` if it asks for
  dependencies.

### A3. Check your library is analysed

The export can only write what Mixxx knows. In Mixxx, confirm your test tracks have:

- a **BPM / beatgrid** (analyse them if not: select tracks → right-click → *Analyze*)
- at least a few **hot cues** and **memory cues**, so there's something to verify

Tracks with no beatgrid still export — they just arrive on the CDJ without one.

### A4. Run the export

Plug in your prepared stick, then in Mixxx:

**Library → Export Library to rekordbox USB**

> 📷 *Screenshot slot — `images/01-library-menu.png`*
> The Mixxx **Library** menu open, with **Export Library to rekordbox USB** and
> **Cue Sheet to Tracklist...** visible at the bottom.
> <!-- Replace this block with: ![Mixxx Library menu](images/01-library-menu.png) -->

Three prompts follow, in this order:

1. **"Select USB device or folder to export to"** — choose the **root** of your USB
   stick (e.g. `/Volumes/MYUSB`, `E:\`, `/media/you/MYUSB`). Not a subfolder.

   > 📷 *Screenshot slot — `images/02-choose-destination.png`*
   > The folder chooser with the USB stick's root selected.
   > <!-- ![Choose destination](images/02-choose-destination.png) -->

2. **"Copy audio files?"** — *"Also copy the audio files onto the device so the tracks
   are playable?"*

   - **Yes** → the tracks come with it. **Choose this** for a hardware test.
   - **No** → database only. The CDJ will browse but **cannot play**. Useful only for
     inspecting the database quickly.

   > 📷 *Screenshot slot — `images/03-copy-audio-prompt.png`*
   > The Yes / No / Cancel question dialog.
   > <!-- ![Copy audio prompt](images/03-copy-audio-prompt.png) -->

3. **"Exporting to rekordbox USB..."** — a progress bar. Copying a large library takes
   a while; the bar counts tracks, then crates, then playlists.

   > 📷 *Screenshot slot — `images/04-export-progress.png`*
   > The progress dialog partway through.
   > <!-- ![Export progress](images/04-export-progress.png) -->

You should finish on **"Export Completed"**, reporting how many tracks, crates and
playlists were written.

> 📷 *Screenshot slot — `images/05-export-completed.png`*
> The completion dialog with its counts.
> <!-- ![Export completed](images/05-export-completed.png) -->

If you get **"Export Failed"** instead, that message is the single most useful thing
you can send us — screenshot it and skip to [Reporting](#reporting-what-you-found).

> **Note:** the menu item always exports your **whole library**. Right-clicking a crate
> gives you *Engine DJ* export, not rekordbox — that's a known gap. For a small test
> stick, use Path B.

Now skip to [Step 3 — check the stick](#step-3--check-the-stick-before-you-leave).

---

## Path B — Export from the command line

No Mixxx build required. Reads your existing `mixxxdb.sqlite` directly.

### B1. Install the build tools

```sh
# Debian / Ubuntu
sudo apt install git cmake g++ libsqlite3-dev

# macOS (Xcode command line tools + Homebrew)
xcode-select --install && brew install cmake sqlite
```

### B2. Build

```sh
git clone https://github.com/sparkly-quasar/openboxxx.git
cd openboxxx/libopenboxxx-export
cmake -S . -B build
cmake --build build
```

Confirm it works before pointing it at your library:

```sh
ctest --test-dir build --output-on-failure
```

All tests should pass. If they don't, stop and
[tell us](https://github.com/sparkly-quasar/openboxxx/issues) — that's a bug worth
knowing about on its own.

### B3. Find your Mixxx library file

```sh
./build/openboxxx_from_mixxx --help
```

prints the flags and the usual locations:

| Your system | `mixxxdb.sqlite` lives at |
|---|---|
| Linux | `~/.mixxx/mixxxdb.sqlite` |
| Windows | `%LOCALAPPDATA%\Mixxx\mixxxdb.sqlite` |
| macOS | `~/Library/Application Support/Mixxx/mixxxdb.sqlite` |
| macOS (sandboxed) | `~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/mixxxdb.sqlite` |

**Quit Mixxx before exporting** so nothing is mid-write.

### B4. Dry run — read the library, write nothing

Always do this first. It touches no disk and tells you what would be exported:

```sh
./build/openboxxx_from_mixxx --db ~/.mixxx/mixxxdb.sqlite
```

```
Read /home/you/.mixxx/mixxxdb.sqlite
  tracks read     : 1
  tracks skipped  : 0 (missing files)
  with beatgrid   : 1
  cues mapped     : 2
  playlists       : 1
Built USB image: 2 generated file(s)
(dry run -- pass --out DIR to write the stick)
```

Sanity-check those numbers against what you see in Mixxx. A large **tracks skipped**
count means files have moved since Mixxx last saw them — fix that first, or those
tracks simply won't be on the stick.

### B5. Write the stick

Start small. Twenty tracks is a far better first hardware test than three thousand:

```sh
./build/openboxxx_from_mixxx \
    --db ~/.mixxx/mixxxdb.sqlite \
    --out /media/you/MYUSB \
    --copy-audio \
    --limit 20
```

```
Read /home/you/.mixxx/mixxxdb.sqlite
  tracks read     : 20
  ...
Wrote 21 generated file(s) (86448 bytes) under /media/you/MYUSB
Copied 20 audio file(s); 0 could not be copied
```

Useful flags:

| Flag | What it does |
|---|---|
| `--limit N` | First N tracks only. Playlists are trimmed to match, so the stick stays consistent. |
| `--ids 12,34,56` | Only these Mixxx track ids — for chasing one specific misbehaving track. |
| `--copy-audio` | **Required** for a playable stick. Without it the CDJ browses but won't play. |
| `--no-intro-outro` | Don't turn Mixxx Intro/Outro cues into memory cues. |

**"could not be copied" is not zero?** Those tracks are in your library but missing
from disk. Everything else still exported.

---

## Step 3 — Check the stick before you leave

Thirty seconds here saves a wasted trip. The stick should look like this:

```
MYUSB/
├── Contents/                         ← your audio (only with --copy-audio / "Yes")
│   └── 1/
│       └── song.flac
└── PIONEER/
    ├── rekordbox/
    │   └── export.pdb                ← the database
    └── USBANLZ/
        └── P001/00000001/
            └── ANLZ0000.DAT          ← beatgrid + cues, one per track
```

Check that:

- [ ] `PIONEER/rekordbox/export.pdb` exists and is **not 0 bytes**
- [ ] `PIONEER/USBANLZ/` has roughly one folder per exported track
- [ ] `Contents/` holds your audio (if you asked for it)
- [ ] You **ejected the stick properly** — pulling it early truncates files and looks
      exactly like a corrupt export

---

## Step 4 — The actual test

Plug into the CDJ. Work down this list and note where it stops.

| # | Check | Looks like |
|---|---|---|
| 1 | The player reads the stick at all | No "NO TRACK" / "E-8305" / unreadable-device error |
| 2 | Tracks are browsable by title/artist/album/genre | The browse menu is populated, not empty |
| 3 | Your **playlists** are there, with the right tracks in the right order | |
| 4 | A track **loads and plays** | |
| 5 | The **beatgrid** is correct | Beat markers land on the beat; sync/quantize behave |
| 6 | **Hot cues** are on the right slots, at the right times | |
| 7 | **Memory cues** are at the right times | |
| 8 | Track **colour** and **rating** survived | |
| 9 | It survives normal use | Loading, cueing, looping, ~10 minutes of play |

Expected and **not** worth reporting: **blank waveforms** (Phase 2), and no track
artwork.

> 📷 *Screenshot slot — `images/06-cdj-browse.png`*
> A photo of the CDJ screen browsing the exported library or a playlist.
> <!-- ![CDJ browsing](images/06-cdj-browse.png) -->

> 📷 *Screenshot slot — `images/07-cdj-playing.png`*
> A photo of a track loaded and playing, showing the beatgrid and cue markers.
> <!-- ![CDJ playing](images/07-cdj-playing.png) -->

**Phone photos of the CDJ screen are perfect.** Please take them even when it works —
a photo of a correct beatgrid is proof, and we currently have none.

<details>
<summary>Optional: test in rekordbox desktop first (no CDJ needed)</summary>

A cheaper gate that catches gross errors. **Use rekordbox 5.** Version 6/7 keep their
real library in an encrypted database and may ignore the `export.pdb` we write, so a
failure there tells you very little.

Mount the stick as a device in rekordbox and see whether the tracks, playlists and cues
appear. Passing this is *not* the same as passing on hardware — CDJ firmware is far
stricter than the desktop app.

</details>

---

## Reporting what you found

**Please report success too.** "CDJ-3000, 20 tracks, everything correct" is the single
most valuable message this project can receive right now.

👉 **[Open a beta report](https://github.com/sparkly-quasar/openboxxx/issues/new?template=beta-report.md&labels=beta-report)**

That link pre-fills the form below. If you'd rather write it freehand, include:

```
Player:        CDJ-3000  (exact model, and firmware version if you know it)
USB stick:     SanDisk 32 GB, FAT32
Export path:   Path A (Mixxx app v0.1.0-alpha)  /  Path B (CLI, commit abc1234)
Your OS:       macOS 15.3 Apple Silicon
Tracks:        20 exported, all FLAC/MP3/…

Got to step __ of the checklist.

What happened:

What you expected instead:
```

Attach if you have them:

- **Photos of the CDJ screen** — the most useful evidence by far
- The **terminal output** (Path B) or a screenshot of the error dialog (Path A)
- Anything odd about the tracks that failed (unusual characters in tags, very long
  files, variable bit rate, unusual sample rate)

Please **don't** attach copyrighted audio, and remember issues are public — crop
anything personal out of screenshots.

---

## Troubleshooting

**The CDJ doesn't see the stick at all.**
Almost always the stick, not the export. Re-check Step 1: FAT32, single partition, MBR.
Confirm the same stick works with a rekordbox-made or plain-audio USB.

**It browses, but every track fails to play.**
You exported without the audio. Re-run with **"Copy audio files?" → Yes** (Path A) or
`--copy-audio` (Path B).

**Tracks are missing from the stick.**
Check `tracks skipped` in the dry run. Those files have moved or been deleted since
Mixxx last saw them.

**"cmake: SQLite3 not usable; skipping mixxxdb reader adapter + CLI"** during the build.
The SQLite headers aren't installed — `libsqlite3-dev` on Debian/Ubuntu, `brew install
sqlite` on macOS. Without them the reader CLI is never built.

**Windows/macOS refuses to open the app.**
Expected — the build isn't code-signed. See [A2](#a2-open-it-despite-the-security-warning).

**The export said it worked but the stick is empty.**
Did you point it at the stick's **root**, and eject properly afterwards?

---

## What we already know is missing

Please don't file these — they're on the roadmap, tracked in
[`export-design.md`](export-design.md):

| Not done yet | Effect on the CDJ |
|---|---|
| Waveforms (Phase 2) | Waveform display is blank; playback is unaffected |
| Track artwork | No cover art in the browser |
| Extended cues (`.EXT` / `PCO2`) | Cue colours and labels don't carry over |
| Four PDB tables left empty (`columns`, `unknown_17`, `unknown_18`, `history`) | Unknown — a genuine stick populates them, and this is a plausible cause if browsing misbehaves. **Do tell us if browsing is odd.** |
| Per-crate export in the Mixxx GUI | Menu exports the whole library; use the CLI's `--limit` / `--ids` |
| OneLibrary (encrypted, newest players) | Out of scope permanently — see [legal.md](legal.md) |

---

## Questions

Open a [discussion or issue](https://github.com/sparkly-quasar/openboxxx/issues) —
"I don't understand step 4" is a perfectly good issue to file, and means this guide
needs fixing.
