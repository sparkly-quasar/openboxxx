#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verification ladder tier 1 for the PDB writer: parse OUR export.pdb with the
authoritative crate-digger Kaitai spec and assert the tables/tracks/playlists we
wrote come back intact.

Needs a Python parser generated from third_party/crate-digger/rekordbox_pdb.ksy:

    kaitai-struct-compiler -t python --outdir <dir> rekordbox_pdb.ksy
    PYTHONPATH=<dir> python3 verify_pdb.py OURS/export.pdb

The CMake `pdb_roundtrip` test wires this up automatically when the compiler and
kaitaistruct runtime are available. Exit 0 = pass, 1 = fail.
"""
import sys

try:
    from kaitaistruct import KaitaiStream
    from rekordbox_pdb import RekordboxPdb
except ImportError as e:
    sys.exit(f"missing dependency ({e}); need kaitaistruct + a generated "
             f"rekordbox_pdb parser on PYTHONPATH")


class Checker:
    def __init__(self):
        self.failures = 0

    def check(self, ok, label):
        print(f"  {'PASS' if ok else 'FAIL'}  {label}")
        if not ok:
            self.failures += 1


def rows(db, type_name):
    for t in db.tables:
        if str(t.type) != type_name:
            continue
        ref = t.first_page
        last = t.last_page.index
        while True:
            pg = ref.body
            if pg.is_data_page:
                for rg in pg.row_groups:
                    for rr in rg.rows:
                        if rr.present:
                            yield rr.body
            if ref.index == last:
                break
            ref = pg.next_page


def text(s):
    return s.body.text if hasattr(s, "body") and hasattr(s.body, "text") else ""


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: verify_pdb.py path/to/export.pdb")
    path = sys.argv[1]
    print(f"[pdb] parsing our export.pdb: {path}")
    db = RekordboxPdb(False, KaitaiStream(open(path, "rb")))  # raises on bad layout

    chk = Checker()
    chk.check(db.len_page == 4096, "len_page == 4096")
    chk.check(db.num_tables > 0, "has tables")

    tracks = list(rows(db, "PageType.tracks"))
    chk.check(len(tracks) > 0, "tracks table has rows")
    for tr in tracks:
        chk.check(bool(text(tr.title)), f"track id={tr.id} has a title")
        chk.check(text(tr.analyze_path).startswith("/PIONEER/USBANLZ/"),
                  f"track id={tr.id} analyze_path points into USBANLZ")
        chk.check(bool(text(tr.file_path)), f"track id={tr.id} has a file_path")

    # Playlist entries must reference real track ids.
    track_ids = {tr.id for tr in tracks}
    entries = list(rows(db, "PageType.playlist_entries"))
    for e in entries:
        chk.check(e.track_id in track_ids,
                  f"playlist entry references existing track {e.track_id}")

    print()
    if chk.failures == 0:
        print(f"PASS: export.pdb round-trips ({len(tracks)} track(s), "
              f"{len(entries)} playlist entr(y/ies))")
        return 0
    print(f"FAIL: {chk.failures} check(s) failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
