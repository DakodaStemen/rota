#!/usr/bin/env bash
# Local CI gate (no hosted CI). Runs, in order, and keeps going after a failure so the table is complete:
#   1 build            build/build.sh            (all default stages, incremental via stamps; sudo stages skip when up to date)
#   2 build:kernel-virt build/build.sh kernel-virt (Rotate kernel for QEMU virt; incremental)
#   3 frontend ctest   cmake --build + ctest in frontend/build
#   4 qemu smoke       qemu/smoke-test.sh        (generic ALARM kernel + built Rotate OS image)
#   5 rotate kernel    qemu/rotate-kernel-test.sh (real Rotate kernel: binder, cgroups, PSI, veth/bridge, uinput, first-boot)
#   6 selftest         qemu/selftest-vm.sh (rotate-selftest in the VM: every check PASS/SKIP, none FAIL or hanging)
# Writes qemu/artifacts/ci-summary.txt (table) and per-step logs qemu/artifacts/ci-<step>.log. Exit 0 only if every step passed.
# Env: CI_SKIP=1 skip entirely (exit 0, hook escape hatch); CI_STEPS="build kvirt ctest smoke rotate selftest" to select steps.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(dirname "$HERE"); A=$HERE/artifacts; mkdir -p "$A"
[ -n "${CI_SKIP:-}" ] && { echo "ci.sh: CI_SKIP set, skipping"; exit 0; }
STEPS=${CI_STEPS:-"build kvirt ctest smoke rotate selftest"}
# Test isolation: nothing here may touch the live Hermes board/state.
export HERMES_KANBAN_HOME=$(TMPDIR=/tmp mktemp -d /tmp/rotate-ci-XXXX); export HERMES_KANBAN_DB=$HERMES_KANBAN_HOME/kanban.db
case "$HERMES_KANBAN_HOME" in "$HOME"/.hermes*) echo "abort: scratch inside ~/.hermes"; exit 2;; esac
echo "ci.sh: scratch=$HERMES_KANBAN_HOME board=$HERMES_KANBAN_DB (not the live ~/.hermes/kanban.db)"
declare -A R S N; ORDER=(); rc=0
step() { # id title cmd...
  local id=$1 title=$2; shift 2; ORDER+=("$id"); local log=$A/ci-$id.log t0=$(date +%s)
  echo "=== [$id] $title: $*"
  ( "$@" ) >"$log" 2>&1; local r=$?
  S[$id]=$(( $(date +%s)-t0 )); R[$id]=$r; N[$id]=$(summ "$id" "$log" "$r")
  if [ $r = 0 ]; then echo "    PASS (${S[$id]}s) ${N[$id]}"; else echo "    FAIL exit=$r (${S[$id]}s) see $log"; tail -15 "$log" | sed 's/^/    | /'; rc=1; fi
}
summ() { # id log rc -> one-line detail from the runner's own output
  case $1 in
    ctest) grep -E "tests passed|tests failed" "$2" | tail -1; grep -Ec "Skipped|\*\*\*Skipped" "$2" | sed 's/^/skipped-lines=/' | tr '\n' ' ';;
    smoke) grep -E "^BOOT_SECONDS|SMOKE (PASS|FAIL)" "$2" | tr '\n' ' ';;
    rotate) grep -E "^(A|B1|B2) " "$2" | tr -s ' ' | tr '\n' ';';;
    selftest) grep -E "FAIL verdict|serial-exec rc" "$2" | tr '\n' ';';;
    build|kvirt) grep -E "exit=" "$ROOT/build/logs/summary.log" | tail -3 | sed 's/^[^ ]* //' | tr '\n' ';' | cut -c1-110;;
  esac
}
want() { [[ " $STEPS " == *" $1 "* ]]; }
want build  && step build  "build.sh (incremental)"        "$ROOT/build/build.sh"
want kvirt  && step kvirt  "build.sh kernel-virt"          "$ROOT/build/build.sh" kernel-virt
want ctest  && step ctest  "frontend ctest"                bash -c "cmake --build '$ROOT/frontend/build' -j16 && ctest --test-dir '$ROOT/frontend/build' --output-on-failure"
want smoke  && step smoke  "qemu smoke (generic kernel)"   "$HERE/smoke-test.sh"
want rotate && step rotate "qemu rotate-kernel VM checks"  "$HERE/rotate-kernel-test.sh"
want selftest && step selftest "rotate-selftest in VM" "$HERE/selftest-vm.sh"
rm -rf "$HERMES_KANBAN_HOME"
{
  printf 'Rotate OS local CI  %s  commit %s%s\n\n' "$(date -Is)" "$(git -C "$ROOT" rev-parse --short HEAD)" "$(git -C "$ROOT" diff --quiet HEAD 2>/dev/null || echo '+dirty')"
  printf '%-8s %-6s %6s  %s\n' STEP RESULT SECS DETAIL
  tot=0; for id in "${ORDER[@]}"; do
    printf '%-8s %-6s %6s  %s\n' "$id" "$([ "${R[$id]}" = 0 ] && echo PASS || echo "FAIL($((R[$id])))")" "${S[$id]}" "${N[$id]}"; tot=$((tot+S[$id])); done
  printf '%-8s %-6s %6s\n' TOTAL "$([ $rc = 0 ] && echo PASS || echo FAIL)" "$tot"
} | tee "$A/ci-summary.txt"
exit $rc
