#!/usr/bin/env bash
# Rotate OS image build driver. See build/README.md.
# usage: build.sh [-h] [stage...]    stages: spl uboot kernel melonds rootfs image (default: all, in order); melonds-pgo and kernel-virt (explicit only; kernel-virt = Rotate kernel for QEMU virt)
# env: FORCE=1 rebuild even if stamp matches; JOBS=16
# exit: 0 ok; 2 usage; 3 missing host tool; else the failing stage's exit status.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ALL=(spl uboot kernel melonds rootfs image)
OPTIONAL=(melonds-pgo kernel-virt)   # needs MELONDS_PROFILE=<dir|tgz> from the device; not in the default run
usage() { sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//'; }
case "${1:-}" in -h|--help) usage; exit 0;; esac
STAGES=("$@"); (( ${#STAGES[@]} )) || STAGES=("${ALL[@]}")
for s in "${STAGES[@]}"; do [[ " ${ALL[*]} ${OPTIONAL[*]} " == *" $s "* ]] || { echo "unknown stage: $s" >&2; usage >&2; exit 2; }; done
for t in git make python3 patch ccache aarch64-linux-gnu-gcc; do command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; exit 3; }; done
mkdir -p "$HERE/logs"
for s in "${STAGES[@]}"; do
  log="$HERE/logs/$s.log"
  echo "=== stage $s -> $log" >&2
  start=$(date +%s)
  set +e; "$HERE/stages/$s.sh" 2>&1 | tee "$log.new" >&2; r=${PIPESTATUS[0]}; set -e
  # a skipped (up-to-date) run must not overwrite the log of the run that built the artifact
  if (( r == 0 )) && grep -q 'up to date (stamp' "$log.new"; then rm -f "$log.new"; else mv -f "$log.new" "$log"; fi
  echo "$(date -Is) stage $s exit=$r ($(( $(date +%s)-start ))s)" | tee -a "$HERE/logs/summary.log" >&2
  if (( r != 0 )); then echo "stage $s FAILED, see $log" >&2; exit "$r"; fi
done
