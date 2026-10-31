#!/bin/sh
# Runs the uinput end-to-end test as root. Exit 77 (ctest SKIP) when /dev/uinput or passwordless sudo is unavailable.
BIN="$1"
if [ -w /dev/uinput ] && [ "$(id -u)" = 0 ]; then exec "$BIN"; fi
if sudo -n true 2>/dev/null && [ -e /dev/uinput ]; then exec sudo -n env TMPDIR="${TMPDIR:-/tmp}" "$BIN"; fi
echo "SKIP: no root access to /dev/uinput" >&2
exit 77
