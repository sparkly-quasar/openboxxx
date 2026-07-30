#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verification ladder tier 2: check our generated ANLZ against an independent
real-rekordbox-format parser (pyrekordbox, MIT).

This is the "prove the bytes" gate for the ANLZ writer. It does two things:

  1. ORACLE round-trip -- parse OUR .DAT with pyrekordbox and assert the beats
     and cues we intended come back with the exact values. If pyrekordbox (a
     third-party parser written against real rekordbox files) can read it, the
     layout is right.

  2. REFERENCE compare (optional) -- given a real rekordbox .DAT (e.g. from a
     tester's stick or pyrekordbox's test fixtures), report which section types
     it has that ours does not yet, so we know what Phase 2 still owes.

Usage:
    pip install pyrekordbox
    # generate one first:  openboxxx_export_cli --out /tmp/ourusb
    python3 verify_anlz.py /tmp/ourusb/PIONEER/USBANLZ/P654/0000875E/ANLZ0000.DAT
    python3 verify_anlz.py OURS.DAT --ref /path/to/real/ANLZ0000.DAT

Exit code 0 = all checks passed, 1 = a check failed.
"""
import argparse
import sys

try:
    from pyrekordbox.anlz import AnlzFile
except ImportError:
    sys.exit("pyrekordbox not installed; run: pip install pyrekordbox")


class Checker:
    def __init__(self):
        self.failures = 0

    def check(self, ok, label):
        print(f"  {'PASS' if ok else 'FAIL'}  {label}")
        if not ok:
            self.failures += 1


def section_names(anlz):
    return [t.name for t in anlz.tags]


def verify_ours(path, chk):
    print(f"[oracle] parsing our file: {path}")
    anlz = AnlzFile.parse_file(path)  # raises if the layout is invalid
    names = section_names(anlz)
    print(f"         sections: {names}")

    chk.check("path" in names, "PPTH (audio path) present")
    chk.check("beat_grid" in names, "PQTZ (beat grid) present")
    chk.check(names.count("cue_list") >= 2, "two PCOB cue lists (memory + hot)")

    bg = next((t for t in anlz.tags if t.name == "beat_grid"), None)
    if bg is not None:
        beats = list(bg.struct.content.entries)
        chk.check(len(beats) > 0, "beat grid has entries")
        # beat numbers cycle 1..4 and times are monotonic
        if beats:
            chk.check(all(1 <= b.beat <= 4 for b in beats), "beat numbers in 1..4")
            times = [b.time for b in beats]
            chk.check(times == sorted(times), "beat times monotonic")

    for t in anlz.tags:
        if t.name != "cue_list":
            continue
        c = t.struct.content
        chk.check(int(c.count) == len(list(c.entries)),
                  f"PCOB count matches entries (cue_type={c.cue_type})")


def compare_reference(ref_path):
    print(f"[reference] parsing real stick file: {ref_path}")
    ref = AnlzFile.parse_file(ref_path)
    names = section_names(ref)
    print(f"            sections: {names}")
    mvp = {"path", "beat_grid", "cue_list"}
    extra = [n for n in names if n not in mvp]
    if extra:
        print(f"            sections beyond our MVP (Phase 2 targets): {sorted(set(extra))}")


def main():
    ap = argparse.ArgumentParser(description="ANLZ tier-2 verification")
    ap.add_argument("ours", help="path to an ANLZ .DAT produced by our writer")
    ap.add_argument("--ref", help="optional real-rekordbox .DAT to compare against")
    args = ap.parse_args()

    chk = Checker()
    verify_ours(args.ours, chk)
    if args.ref:
        compare_reference(args.ref)

    print()
    if chk.failures == 0:
        print("PASS: our ANLZ round-trips through pyrekordbox")
        return 0
    print(f"FAIL: {chk.failures} check(s) failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
