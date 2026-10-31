#!/usr/bin/env python3
"""Verify a Unisoc DHTB image (layout: research 01 section 3 / Armbian flash README).
usage: check-dhtb.py FILE [EXACT_TOTAL_SIZE|-] [MAX_PAYLOAD]   exit 0 ok, 1 bad."""
import hashlib, struct, sys
d = open(sys.argv[1], "rb").read()
total = int(sys.argv[2], 0) if len(sys.argv) > 2 and sys.argv[2] != "-" else None
maxp = int(sys.argv[3], 0) if len(sys.argv) > 3 else None
def bad(m): print("DHTB FAIL:", m, file=sys.stderr); sys.exit(1)
if d[:4] != b"DHTB": bad("magic")
size = struct.unpack("<I", d[0x30:0x34])[0]
if total is not None and len(d) != total: bad(f"file size {len(d)} != {total}")
if 0x200 + size > len(d): bad("payload size exceeds file")
if hashlib.sha256(d[0x200:0x200 + size]).digest() != d[8:0x28]: bad("sha256 mismatch")
if maxp is not None and size > maxp: bad(f"payload {size} > max {maxp}")
print(f"DHTB OK: file={len(d)} payload={size} sha256={d[8:0x28].hex()[:16]}...")
