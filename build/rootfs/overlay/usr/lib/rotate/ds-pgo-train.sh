#!/bin/sh
# ds-pgo-train.sh [-o OUTDIR] [-w WARMUP] [-f FRAMES] ROM.nds [ROM.nds ...]
# Runs the -fprofile-generate melonDS DS core (/opt/rotate/pgo/melondsds_libretro.pgo.so) headlessly through dsbench on
# THIS device and leaves the profile in OUTDIR (default /var/lib/rotate/pgo-profile), ready for the host:
#     tar czf melonds-profile.tgz -C OUTDIR .      then on the build host:
#     MELONDS_PROFILE=melonds-profile.tgz build/build.sh melonds-pgo
# Protocol adapted from thwonp/plorpOS tools/ds-pgo-train.sh (MIT): unthrottled on the two A75s (cpus 6,7), each ROM
# once as an attract/no-input pass and once with scripted input, plus one serialize/round-trip/reload pass on the first ROM.
# plorpOS used Pokemon Pearl, Metroid Prime Hunters, Advance Wars, with Dragon Quest V held out for validation; ROMs are the
# user's own, none ship here. Needs ~6 min per the plorpOS figure (not measured on Rotate OS).
# GCOV_PREFIX makes the core write build/out/build/**.gcda under OUTDIR; the host's melonds-pgo stage unpacks them at the chroot root.
# Exit: 0 ok, 1 usage, 2 missing file, 3 no .gcda produced.
set -eu
OUT=/var/lib/rotate/pgo-profile; WARM=600; FRAMES=1800
while getopts o:w:f: o; do case $o in o) OUT=$OPTARG;; w) WARM=$OPTARG;; f) FRAMES=$OPTARG;; *) echo "usage: $0 [-o OUTDIR] [-w WARM] [-f FRAMES] ROM..." >&2; exit 1;; esac; done
shift $((OPTIND-1))
[ $# -ge 1 ] || { echo "usage: $0 [-o OUTDIR] [-w WARM] [-f FRAMES] ROM..." >&2; exit 1; }
CORE=/opt/rotate/pgo/melondsds_libretro.pgo.so; BENCH=/opt/rotate/pgo/dsbench
[ -f "$CORE" ] && [ -x "$BENCH" ] || { echo "missing $CORE or $BENCH" >&2; exit 2; }
for r in "$@"; do [ -f "$r" ] || { echo "no ROM: $r" >&2; exit 2; }; done
rm -rf "$OUT"; mkdir -p "$OUT"
export GCOV_PREFIX="$OUT" BENCH_EXIT=1
OPTS='melonds_jit_enable=enabled melonds_render_mode=software melonds_threaded_renderer=enabled'
pin() { if command -v taskset >/dev/null 2>&1; then taskset -c 6,7 "$@"; else "$@"; fi; }
first=1
for rom in "$@"; do
  BENCH_NOINPUT=1 pin "$BENCH" "$CORE" "$rom" "$WARM" "$WARM" /tmp/pgo.ppm $OPTS 2>/dev/null | grep -E '^(ms|roundtrip)' || true
  pin "$BENCH" "$CORE" "$rom" "$WARM" "$FRAMES" /tmp/pgo.ppm $OPTS 2>/dev/null | grep -E '^(ms|roundtrip)' || true
  if [ $first = 1 ]; then
    BENCH_RT=1 BENCH_SER=60 BENCH_RELOAD=1 BENCH_NOINPUT=1 pin "$BENCH" "$CORE" "$rom" "$WARM" 60 /tmp/pgo.ppm $OPTS 2>/dev/null | grep -E '^(ms|roundtrip)' || true
    first=0
  fi
done
n=$(find "$OUT" -name '*.gcda' | wc -l)
echo "profile: $n .gcda files in $OUT"
[ "$n" -gt 0 ] || { echo "no .gcda written (core did not exit cleanly?)" >&2; exit 3; }
