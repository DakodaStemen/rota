# shellcheck shell=bash
# Shared helpers. Sourced by every stage.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"   # rotate-os/
B="$ROOT/build"
OUT="$B/out"; LOGS="$B/logs"; WORK="$B/work"; CACHE="$B/cache"
# shellcheck disable=SC1091
. "$B/pins.env"
JOBS="${JOBS:-16}"
CROSS="${CROSS_COMPILE:-aarch64-linux-gnu-}"
mkdir -p "$OUT" "$LOGS" "$WORK" "$CACHE"

say()  { printf '[%s] %s\n' "${STAGE:-build}" "$*" >&2; }
die()  { printf '[%s] ERROR: %s\n' "${STAGE:-build}" "$*" >&2; exit 1; }

# ccache masquerade: symlinks named like the cross tools, first in PATH.
setup_ccache() {
  export CCACHE_DIR="$CACHE/ccache" CCACHE_MAXSIZE=10G
  mkdir -p "$CCACHE_DIR" "$WORK/ccache-bin"
  local t
  for t in gcc g++ cc c++; do ln -sf "$(command -v ccache)" "$WORK/ccache-bin/${CROSS}$t"; done
  ln -sf "$(command -v ccache)" "$WORK/ccache-bin/gcc"
  export PATH="$WORK/ccache-bin:$PATH"
}

# stage_begin NAME "input-string" out-file...   -> skips (exit 0) when stamp matches and outputs exist.
stage_begin() {
  STAGE="$1"; local inputs="$2"; shift 2
  STAMP_FILE="$OUT/.$STAGE.stamp"
  STAMP="$(printf '%s' "$inputs" | sha256sum | cut -d' ' -f1)"
  if [[ -z "${FORCE:-}" && -f "$STAMP_FILE" && "$(cat "$STAMP_FILE")" == "$STAMP" ]]; then
    local ok=1 f; for f in "$@"; do [[ -e "$f" ]] || ok=0; done
    if (( ok )); then say "up to date (stamp ${STAMP:0:12}), skipping; FORCE=1 to rebuild"; exit 0; fi
  fi
  rm -f "$STAMP_FILE"
  say "building (inputs ${STAMP:0:12})"
}
stage_end() { printf '%s' "$STAMP" > "$STAMP_FILE"; say "done"; }

file_hash() { cat "$@" | sha256sum | cut -d' ' -f1; }
