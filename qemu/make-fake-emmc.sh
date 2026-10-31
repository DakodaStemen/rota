#!/usr/bin/env bash
# Build a FAKE eMMC as a raw regular file: GPT only (sgdisk), partitions named as base618's extract-firmware.sh expects,
# raw synthetic payloads written with dd (no mkfs, no filesystem, no loop device, no sudo). Payloads are clearly fake
# (banner "ROTATE-OS-QEMU-SYNTHETIC-TEST-PAYLOAD ... NOT VENDOR FIRMWARE"), only the magic bytes match.
# usage: make-fake-emmc.sh OUT.raw   -> prints "SHA_AGDSP SHA_PMSYS SHA_WCN" (expected sha256 of the three extracted files)
# Layout exercises the a/b logic: l_agdsp_a BAD magic -> b is used; pm_sys_a good; wcnmodem_a BAD -> b is used.
set -euo pipefail
OUT=${1:?usage: make-fake-emmc.sh OUT.raw}
case "$OUT" in /dev/*) echo "refusing device path" >&2; exit 1;; esac
rm -f "$OUT"; truncate -s 40MiB "$OUT"
sgdisk -Z "$OUT" >/dev/null 2>&1 || true
sgdisk -o "$OUT" >/dev/null
n=1; start=2048; declare -A START
for spec in l_agdsp_a:6 l_agdsp_b:6 pm_sys_a:2 pm_sys_b:2 wcnmodem_a:1 wcnmodem_b:1; do
  name=${spec%%:*}; mib=${spec##*:}; sectors=$((mib*2048))
  sgdisk -n "$n:$start:+$sectors" -c "$n:$name" "$OUT" >/dev/null
  START[$name]=$start; start=$((start+sectors)); n=$((n+1))
done
sgdisk -p "$OUT" >&2
python3 -I - "$OUT" "${START[l_agdsp_a]}" "${START[l_agdsp_b]}" "${START[pm_sys_a]}" "${START[pm_sys_b]}" "${START[wcnmodem_a]}" "${START[wcnmodem_b]}" <<'PY'
import hashlib, sys
out = sys.argv[1]; st = dict(zip(["agdsp_a","agdsp_b","pm_a","pm_b","wcn_a","wcn_b"], map(int, sys.argv[2:8])))
BANNER = b" ROTATE-OS-QEMU-SYNTHETIC-TEST-PAYLOAD (NOT VENDOR FIRMWARE) "
def payload(magic, size, tag):
    d = bytearray(magic + BANNER + tag)
    i = 0
    while len(d) < size:
        d += hashlib.sha256(tag + i.to_bytes(4, "little")).digest(); i += 1
    return bytes(d[:size])
agdsp_b = payload(b"SharkL5_AUDCP_", 6*1024*1024, b"agdsp-slot-b")
pm_a    = payload(bytes.fromhex("dff80cd0"), 2*1024*1024, b"pm_sys-slot-a")
pm_b    = payload(bytes.fromhex("dff80cd0"), 2*1024*1024, b"pm_sys-slot-b")
wcn_b   = payload(bytes.fromhex("a00a1a00"), 1*1024*1024, b"wcnmodem-slot-b")
agdsp_a = payload(b"BADMAGIC_NOT_AUDCP", 6*1024*1024, b"agdsp-slot-a-bad")
wcn_a   = payload(b"\xde\xad\xbe\xef", 1*1024*1024, b"wcnmodem-slot-a-bad")
with open(out, "r+b") as f:
    for key, data in (("agdsp_a",agdsp_a),("agdsp_b",agdsp_b),("pm_a",pm_a),("pm_b",pm_b),("wcn_a",wcn_a),("wcn_b",wcn_b)):
        f.seek(st[key]*512); f.write(data)
# extract-firmware.sh copies only the first N bytes: 5 MiB / 1 MiB / 947200 B
print(hashlib.sha256(agdsp_b[:5242880]).hexdigest(), hashlib.sha256(pm_a[:1048576]).hexdigest(), hashlib.sha256(wcn_b[:947200]).hexdigest())
PY
