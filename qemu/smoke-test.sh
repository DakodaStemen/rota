#!/usr/bin/env bash
# End-to-end: boot headless, log in on serial, run in-guest checks, screendump, ssh banner check, shut down.
# Exit nonzero on any failure. Logs/artifacts in qemu/artifacts/.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); A=$HERE/artifacts; mkdir -p "$A" "$HERE/images"
PORT=${SSH_PORT:-2222}; BOOT_TIMEOUT=${BOOT_TIMEOUT:-600}
SS=$HERE/images/serial.sock; QS=$HERE/images/qmp.sock; QLOG=$A/qemu.log
[ -f "$HERE/images/alarm-rootfs.img" ] || { echo "FAIL: no image; run ./build-vm-image.sh"; exit 3; }
echo "image: $HERE/images/alarm-rootfs.img  (snapshot mode, image not modified)"
T0=$(date +%s)
"$HERE/run-vm.sh" -H -S -p "$PORT" -s "$SS" -q "$QS" >"$QLOG" 2>&1 & QPID=$!
trap 'kill $QPID 2>/dev/null; wait $QPID 2>/dev/null' EXIT
rc=0
python3 -I "$HERE/serial-run.py" "$SS" "$A/serial.log" "$BOOT_TIMEOUT" -- /usr/local/bin/rotate-vm-check.sh | tee "$A/guest-check.log"
[ ${PIPESTATUS[0]} = 0 ] || { echo "FAIL: guest checks"; rc=1; }
python3 -I "$HERE/screendump.py" "$QS" "$A/screendump-720.png" || { echo "FAIL: screendump"; rc=1; }
python3 -I - "$A/screendump-720.png" <<'PY' || rc=1
import struct,sys
d=open(sys.argv[1],'rb').read(); w,h=struct.unpack('>II',d[16:24]); print("screendump size %dx%d"%(w,h))
sys.exit(0 if (w,h)==(720,720) else 1)
PY
if command -v nc >/dev/null && timeout 10 bash -c "head -c 20 < /dev/tcp/127.0.0.1/$PORT" 2>/dev/null | grep -q '^SSH-'; then echo "PASS ssh banner on 127.0.0.1:$PORT"; else echo "FAIL ssh banner"; rc=1; fi
echo "total_seconds=$(( $(date +%s) - T0 ))"
[ $rc = 0 ] && echo "SMOKE PASS" || echo "SMOKE FAIL"
exit $rc
