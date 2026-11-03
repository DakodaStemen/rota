#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# pgo-train-qemu.sh [-j STREAMS] [-r ROMDIR] [-o OUTDIR] [-c CORE.pgo.so]
# Host-side PGO training of the -fprofile-generate melonDS core UNDER QEMU-USER (aarch64) inside the melonds build
# chroot (work/melonds-chroot, made by `build.sh melonds`). The .gcda counters record edge/branch execution counts,
# which depend on the code path, not on CPU timing, so a profile gathered under emulation with the same GCC (16.1.1,
# the chroot's) is valid -fprofile-use input. It is NOT the same workload mix as a real game (homebrew test ROMs only):
# see build/README.md "melonDS PGO under qemu-user".
#
# Each ROM runs through tools/dsbench.c (compiled natively in the chroot) three ways: attract (no input), scripted
# input, and for CPU-test ROMs (armwrestler/rockwrestler) a long input run. One ROM also does serialize/round-trip/
# reload, and one pass runs with the JIT off so the interpreter paths get counts too. STREAMS parallel chroot
# processes each write their own GCOV_PREFIX dir; gcov-tool merges them into OUTDIR/merged/ (build/out/build/**.gcda).
# Output: OUTDIR/merged  (tar it: tar czf melonds-profile.tgz -C OUTDIR/merged .)   OUTDIR/train.log
# Needs sudo (systemd-nspawn). Exit: 0 ok, 1 usage, 2 missing input, 3 no .gcda / merge failed.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); B=$(cd "$HERE/.." && pwd)
J=4; ROMS=$B/work/pgo-roms/nds; OUT=$B/work/pgo-roms/gcda; CORE=$B/out/melonds/melondsds_libretro.pgo.so
CH=$B/work/melonds-chroot
while getopts j:r:o:c:h o; do case $o in j) J=$OPTARG;; r) ROMS=$OPTARG;; o) OUT=$OPTARG;; c) CORE=$OPTARG;;
  *) sed -n 2,3p "$0" >&2; exit 1;; esac; done
[[ -f $CH/.ready && -f $CORE && -d $ROMS ]] || { echo "need $CH/.ready, $CORE, $ROMS" >&2; exit 2; }
sudo cp "$HERE/dsbench.c" "$CH/root/dsbench.c"
sudo systemd-nspawn -q --register=no -D "$CH" gcc -O2 -Wall -Wno-unused-result /root/dsbench.c -ldl -o /root/dsbench
sudo rm -rf "$OUT"; mkdir -p "$OUT"   # earlier runs leave root-owned .gcda files
# list: one ROM per line; stream i takes every J-th (CPU-test ROMs first so they land on different streams)
ls "$ROMS"/*.nds | sort | sed "s|.*/|/roms/|" > "$OUT/roms.txt"
sudo tee "$CH/root/stream.sh" >/dev/null <<'EOS'
#!/bin/bash
# stream.sh N J : train ROMs N, N+J, ... into /gcda/sN
N=$1; J=$2; export GCOV_PREFIX=/gcda/s$N BENCH_EXIT=1; mkdir -p $GCOV_PREFIX
OPTS="melonds_sysfile_mode=builtin melonds_boot_mode=direct melonds_render_mode=software melonds_threaded_renderer=enabled"
CORE=/pgocore/melondsds_libretro.pgo.so
run() { local t0=$(date +%s); "$@" 2>&1 | grep -E '^(core|ms|roundtrip|serialize|load failed)' | sed "s|^|[$N] $(basename $ROM) |"; echo "[$N] $(basename $ROM) pass-wall $(( $(date +%s)-t0 )) s"; }
i=0
while read -r ROM; do
  if (( i++ % J != N )); then continue; fi
  case $ROM in
    *wrestler*) FR=1500;;
    *) FR=200;;
  esac
  BENCH_NOINPUT=1 run /root/dsbench $CORE $ROM 30 250 /tmp/s$N.ppm $OPTS melonds_jit_enable=enabled
  run /root/dsbench $CORE $ROM 30 $FR /tmp/s$N.ppm $OPTS melonds_jit_enable=enabled
  case $ROM in
    *basic_cube*|*rockwrestler*|*basic_texture*) BENCH_RT=1 BENCH_SER=30 BENCH_RELOAD=1 BENCH_NOINPUT=1 run /root/dsbench $CORE $ROM 30 60 /tmp/s$N.ppm $OPTS melonds_jit_enable=enabled;; esac
  case $ROM in
    *armwrestler*|*rockwrestler*|*toon_shading*|*photo_slideshow*) run /root/dsbench $CORE $ROM 30 150 /tmp/s$N.ppm $OPTS melonds_jit_enable=disabled;; esac
done < /gcda/roms.txt
echo "[$N] DONE"
EOS
sudo chmod +x "$CH/root/stream.sh"
T0=$(date +%s)
# ONE container, J parallel streams inside it (a second nspawn on the same root dir fails: "Directory tree is busy")
sudo tee "$CH/root/streams.sh" >/dev/null <<'EOS'
#!/bin/bash
J=$1; for ((n=0; n<J; n++)); do /root/stream.sh "$n" "$J" > /gcda/stream$n.log 2>&1 & done; wait
EOS
sudo chmod +x "$CH/root/streams.sh"
rc=0
sudo systemd-nspawn -q --register=no -D "$CH" --bind-ro="$ROMS:/roms" --bind="$OUT:/gcda" --bind-ro="$(dirname "$CORE"):/pgocore" /root/streams.sh "$J" || rc=1
cat "$OUT"/stream*.log > "$OUT/train.log"
echo "training wall time $(( $(date +%s)-T0 )) s, streams rc=$rc" | tee -a "$OUT/train.log"
ndir=$(find "$OUT"/s* -name '*.gcda' 2>/dev/null | wc -l); echo "per-stream .gcda files: $ndir" | tee -a "$OUT/train.log"
(( ndir > 0 )) || { echo "no .gcda written" >&2; exit 3; }
# merge: gcov-tool (GCC 16.1.1, chroot) merges profile dirs
sudo systemd-nspawn -q --register=no -D "$CH" --bind="$OUT:/gcda" bash -c '
  set -e; cd /gcda; rm -rf merged; first=1
  for d in s*/ ; do d=${d%/}; [ -d $d/build ] || continue
    if [ $first = 1 ]; then cp -a $d/ merged; first=0; else gcov-tool merge -o merged.new merged $d/ && rm -rf merged && mv merged.new merged; fi
  done
  find merged -name "*.gcda" | wc -l' | tee -a "$OUT/train.log"
sudo chown -R "$(id -u):$(id -g)" "$OUT"
