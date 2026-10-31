#!/usr/bin/env bash
# selftest-vm.sh : prove rotate-selftest degrades cleanly when hardware is missing, in the QEMU VM (virt, generic kernel,
# no Unisoc hardware). Uses the same no-format harness as smoke-test.sh (qcow2 overlay on a pinned copy of the image).
#   1. boot headless; the AUTO unit (rotate-selftest-auto.service, after rotate-firstboot) runs on its own: watch it finish
#   2. run `rotate-selftest --no-suspend` on demand a second time AFTER the payload's modules are loaded (virtio-gpu etc.)
#   3. run `rotate-selftest --only suspend --suspend-secs 20` (suspend path)
#   4. copy the newest tarball out to artifacts/selftest-vm/ and verify it unpacks
# Exit: 0 when every check of every run ended PASS/FAIL/SKIP within its timeout and no run exceeded its budget (note: a FAIL
# verdict is expected in the VM for the checks that need real hardware only if the VM were an RG Rotate; here they must SKIP),
# 1 on any FAIL verdict inside the VM, 2 harness failure.   Logs: artifacts/selftest-vm/*.log
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); A=$HERE/artifacts/selftest-vm; mkdir -p "$A" "$HERE/images"
SS=$HERE/images/serial.sock; QS=$HERE/images/qmp.sock; PORT=${SSH_PORT:-2222}
"$HERE/prepare-vm.sh" -f > "$A/prepare.log" 2>&1 || { echo "FAIL prepare-vm.sh (see $A/prepare.log)"; exit 2; }
T0=$(date +%s)
"$HERE/run-vm.sh" -H -p "$PORT" -s "$SS" -q "$QS" > "$A/qemu.log" 2>&1 & QPID=$!
trap 'kill $QPID 2>/dev/null; wait $QPID 2>/dev/null' EXIT
python3 -I "$HERE/serial-exec.py" "$SS" "$A/serial.log" 900 -- \
  "300:for i in \$(seq 1 280); do [ -e /var/lib/rotate/selftest.done ] && break; sleep 1; done; systemctl status rotate-selftest-auto --no-pager -l | head -40; echo; cat /var/log/rotate-selftest/*/summary.txt; ls -l /var/log/rotate-selftest /var/lib/rotate/selftest.done" \
  "60:tar -xf /dev/vdb -C / && depmod -a && modprobe virtio-gpu && modprobe uinput && sleep 2; ls /dev/dri /dev/input 2>&1 | head" \
  "600:time rotate-selftest --no-suspend --no-prompt 2>&1" \
  "300:time rotate-selftest --only suspend --suspend-secs 20 2>&1" \
  "60:ls -l /var/log/rotate-selftest/; systemctl is-active rotate-selftest-auto; systemctl list-units --failed --no-legend | head; ps -eo pid,ppid,stat,args | grep -E 'rotate-(selftest|gles|spin|evdump)|timeout|rtcwake' | grep -v grep; echo ORPHANS_DONE" \
  "120:cd /var/log/rotate-selftest && f=\$(ls -t *.tar.gz | head -1) && echo TARNAME=\$f && echo B64BEGIN && base64 -w0 \$f && echo && echo B64END" \
  > "$A/vm-session.log" 2>&1
rc=$?
echo "serial-exec rc=$rc, total harness seconds $(( $(date +%s) - T0 ))" | tee -a "$A/vm-session.log"
python3 -I - "$A/vm-session.log" "$A/last.tar.gz" <<'PY'
import base64, re, sys
t = open(sys.argv[1], errors="replace").read()
m = re.search(r"B64BEGIN\r?\n([A-Za-z0-9+/=]+)\r?\n?B64END", t)
if m: open(sys.argv[2], "wb").write(base64.b64decode(m.group(1))); print("tarball saved", sys.argv[2])
else: print("no tarball in session log")
PY
[ -f "$A/last.tar.gz" ] && tar tzf "$A/last.tar.gz" | head -50 > "$A/last.tar.list" && wc -l "$A/last.tar.list"
nfail=$(grep -c '^\[FAIL\]' "$A/vm-session.log"); echo "FAIL verdict lines in the VM session: $nfail"
exit $(( rc != 0 ? 2 : (nfail > 0 ? 1 : 0) ))
