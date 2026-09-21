"""Check the native addresses cited in runtime/ comments against the export.

AGENTS.md: address annotations taken from the reference project apply to the
reference game only; 《鬼作》 addresses must be verified. This script reads every
six-hex-digit token in runtime/*.inc and runtime/*.h and reports whether it is a
function start in the local Ghidra/r2 export (local/decompiled/<VA>.c). An address
that is not a function start is either a deliberate mid-function label or, more
often, a reference-project address that was never verified here.

Static only: the original EXE is read as data and never executed.

Usage: python3 tools/check_address_citations.py [--json reports/address-citations.json]
Exit status is 0 when every cited address resolves, 1 otherwise, so it can gate
a release step.
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def function_starts(export_dir):
    starts = {}
    for path in glob.glob(os.path.join(export_dir, "00*.c")):
        va = os.path.basename(path)[:-2]
        try:
            starts[int(va, 16)] = os.path.basename(path)
        except ValueError:
            continue
    return starts


def cited_addresses():
    cited = {}
    for pattern in ("runtime/*.inc", "runtime/*.h"):
        for path in glob.glob(os.path.join(ROOT, pattern)):
            text = open(path, encoding="utf-8", errors="replace").read()
            for match in re.finditer(r"\b([0-9a-f]{6})\b", text):
                cited.setdefault(int(match.group(1), 16), set()).add(os.path.basename(path))
    return cited


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--export", default=os.path.join(ROOT, "local", "decompiled"))
    parser.add_argument("--json")
    args = parser.parse_args()

    starts = function_starts(args.export)
    if not starts:
        print("no export found under %s" % args.export, file=sys.stderr)
        return 2

    cited = cited_addresses()
    resolved = sorted(a for a in cited if a in starts)
    unresolved = sorted(a for a in cited if a not in starts)

    print("export        : %s (%d function starts)" % (args.export, len(starts)))
    print("cited in runtime/: %d" % len(cited))
    print("resolved      : %d" % len(resolved))
    print("UNRESOLVED    : %d" % len(unresolved))
    for address in unresolved:
        print("  %#08x  %s" % (address, ", ".join(sorted(cited[address]))))

    if args.json:
        try:
            export_label = os.path.relpath(args.export, ROOT)
        except ValueError:
            export_label = args.export
        with open(os.path.join(ROOT, args.json), "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "export": export_label,
                    "function_starts": len(starts),
                    "cited": len(cited),
                    "resolved": ["%#08x" % a for a in resolved],
                    "unresolved": [
                        {"address": "%#08x" % a, "cited_in": sorted(cited[a])} for a in unresolved
                    ],
                },
                handle,
                ensure_ascii=False,
                indent=2,
            )
            handle.write("\n")
        print("wrote %s" % args.json)
    return 0 if not unresolved else 1


if __name__ == "__main__":
    sys.exit(main())
