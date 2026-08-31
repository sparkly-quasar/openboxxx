# Screenshots for the beta-testing guide

[`../beta-testing.md`](../beta-testing.md) has a numbered slot for each image below.
None of them exist yet — the guide reads fine without them, but it reads far better
with them, and testers are much likelier to get through a GUI walkthrough that shows
them what to look for.

Each slot in the guide looks like this:

```markdown
> 📷 *Screenshot slot — `images/01-library-menu.png`*
> The Mixxx **Library** menu open, with ...
> <!-- Replace this block with: ![Mixxx Library menu](images/01-library-menu.png) -->
```

To fill one in: drop the file here with the exact name, then replace the whole
blockquote with the `![...](...)` line from the comment on its last line.

## What each one needs

| File | Shows | Where to get it |
|---|---|---|
| `01-library-menu.png` | Mixxx **Library** menu open, showing *Export Library to rekordbox USB* and *Cue Sheet to Tracklist...* | Beta build, menu bar |
| `02-choose-destination.png` | The *"Select USB device or folder to export to"* folder chooser, USB root selected | During export |
| `03-copy-audio-prompt.png` | The *"Copy audio files?"* Yes/No/Cancel dialog | During export |
| `04-export-progress.png` | *"Exporting to rekordbox USB..."* progress dialog, partway through | During export |
| `05-export-completed.png` | The *"Export Completed"* dialog with its track/crate/playlist counts | End of export |
| `06-cdj-browse.png` | CDJ screen browsing the exported library or a playlist | **Phone photo of the player** |
| `07-cdj-playing.png` | CDJ screen with a track playing, beatgrid and cue markers visible | **Phone photo of the player** |

`06` and `07` are the ones that matter most. They're the first visual evidence that
openboxxx works on real hardware, and no one has them yet.

## Guidelines

- **PNG** for UI, **JPEG** for photos of a CDJ screen.
- Keep each file **under ~500 KB** and no wider than **1600 px**. These live in git
  forever; a 5 MB screenshot is a permanent tax on every clone.
- Crop to the dialog plus a little context — not the whole desktop.
- **Check for personal information** before committing: track paths, folder names,
  your username in a title bar, anything in a neighbouring window. Screenshots are
  public and permanent once pushed.
- Light or dark theme is fine; be consistent within a set if you can.
- If you reshoot after a UI change, keep the same filename so the guide doesn't need
  editing.
