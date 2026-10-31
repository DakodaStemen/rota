#!/usr/bin/env bash
# End-to-end: boot headless, log in on serial, run in-guest checks, screendump, ssh banner check, shut down.
# Exit nonzero on any failure. Logs/artifacts in qemu/artifacts/.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); A=$HERE/artifacts; mkdir -p "$A" "$HERE/images"
PORT=${SSH_PORT:-2222}; BOOT_TIMEOUT=${BOOT_TIMEOUT:-600}
SS=$HERE/images/serial.sock; QS=$HERE/images/qmp.sock; QLOG=$A/qemu.log
"$HERE/prepare-vm.sh" -f >"$A/prepare.log" 2>&1 || { echo "FAIL: prepare-vm.sh (see artifacts/prepare.log)"; exit 3; }
RAW=$HERE/images/rotate-os-base.raw; SRC_SHA=$(sha256sum "$HERE/../build/out/image/rotate-os.img" | cut -d" " -f1); echo "source rotate-os.img sha256=$SRC_SHA"; cmp -s "$HERE/../build/out/image/rotate-os.img" "$RAW" && echo "pinned base identical to source"
RAW_STAT0=$(stat -c '%s %Y' "$RAW")
echo "backing image: $RAW (read-only via qcow2 overlay $HERE/rotate-os-test.qcow2, fresh each run)"
T0=$(date +%s)
"$HERE/run-vm.sh" -H -p "$PORT" -s "$SS" -q "$QS" >"$QLOG" 2>&1 & QPID=$!
trap 'kill $QPID 2>/dev/null; wait $QPID 2>/dev/null' EXIT
rc=0
python3 -I "$HERE/serial-run.py" "$SS" "$A/serial.log" "$BOOT_TIMEOUT" -- "tar -xf /dev/vdb -C / && echo RV_INFO uptime=\$(cut -d. -f1 /proc/uptime)s && /usr/local/bin/rotate-vm-check.sh" | tee "$A/guest-check.log"
[ ${PIPESTATUS[0]} = 0 ] || { echo "FAIL: guest checks"; rc=1; }
python3 -I "$HERE/screendump.py" "$QS" "$A/screendump-720.png" || { echo "FAIL: screendump"; rc=1; }
python3 -I - "$A/screendump-720.png" <<'PY' || rc=1
import struct,sys
d=open(sys.argv[1],'rb').read(); w,h=struct.unpack('>II',d[16:24]); print("screendump size %dx%d"%(w,h))
sys.exit(0 if (w,h)==(720,720) else 1)
PY
BANNER=""; for i in $(seq 1 30); do BANNER=$(timeout 5 bash -c "head -c 40 < /dev/tcp/127.0.0.1/$PORT" 2>/dev/null | head -1 | tr -d '\r'); case $BANNER in SSH-*) break;; esac; sleep 2; done
if [[ $BANNER == SSH-* ]]; then echo "PASS ssh banner on 127.0.0.1:$PORT: $BANNER"; else echo "FAIL ssh banner"; rc=1; fi
[ "$(stat -c '%s %Y' "$RAW")" = "$RAW_STAT0" ] && echo "PASS raw image size/mtime unchanged" || { echo "FAIL raw image modified"; rc=1; }
echo "total_seconds=$(( $(date +%s) - T0 ))"
[ $rc = 0 ] && echo "SMOKE PASS" || echo "SMOKE FAIL"
exit $rc
