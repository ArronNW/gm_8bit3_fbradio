"""Replace one overlong DT_NEEDED string without moving the ELF string table.

Zig's Windows cross-link driver resolves a no-SONAME Valve import library to a
Windows-style relative path.  Garry's Mod expects the usual `libtier0.so` name.
The replacement must be shorter and must occur exactly once.
"""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: fix_elf_needed.py <elf> <old-name> <new-name>", file=sys.stderr)
        return 2

    target = Path(sys.argv[1])
    old = sys.argv[2].encode("utf-8") + b"\0"
    new_text = sys.argv[3].encode("utf-8")
    if len(new_text) + 1 > len(old):
        print("replacement is longer than the existing DT_NEEDED entry", file=sys.stderr)
        return 2

    data = bytearray(target.read_bytes())
    offsets = []
    start = 0
    while True:
        found = data.find(old, start)
        if found < 0:
            break
        offsets.append(found)
        start = found + len(old)

    if len(offsets) != 1:
        print(f"expected one exact entry, found {len(offsets)}", file=sys.stderr)
        return 1

    replacement = new_text + b"\0" * (len(old) - len(new_text))
    offset = offsets[0]
    data[offset:offset + len(old)] = replacement
    target.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
