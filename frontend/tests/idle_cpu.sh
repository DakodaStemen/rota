#!/bin/sh
# Starts rotate-frontend on SDL's dummy video driver with a fake ROM tree, lets it settle, then measures
# the CPU time it burns while idle (no input). Event-driven means: 0 clock ticks over a 2 s window.
BIN="$1"
T=$(mktemp -d "${TMPDIR:-/tmp}/rotate-idle-XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/roms/gba" "$T/state"
for g in "Advance Wars" "Metroid Fusion" "Wario Land 4"; do : > "$T/roms/gba/$g.gba"; done
cat > "$T/c.ini" <<EOF
[paths]
roms_root = $T/roms
state_dir = $T/state
sysfs_root = $T/sys
socket = $T/none.sock
[system.gba]
name = Game Boy Advance
core = mgba
extensions = gba
EOF
SDL_VIDEODRIVER=dummy "$BIN" --config "$T/c.ini" --exit-after-ms 5000 &
PID=$!
sleep 1.5
a=$(awk '{print $14+$15}' /proc/$PID/stat) || exit 1
sleep 2
b=$(awk '{print $14+$15}' /proc/$PID/stat) || exit 1
threads=$(ls /proc/$PID/task | wc -l)
echo "frontend pid $PID threads=$threads cpu ticks at 1.5s=$a at 3.5s=$b (100 ticks = 1 s), idle delta=$((b-a))"
wait $PID
rc=$?
[ "$rc" = 0 ] || { echo "frontend exited $rc"; exit 1; }
[ $((b-a)) -le 2 ] || { echo "FAIL: idle CPU delta too high"; exit 1; }
echo PASS
