#!/usr/bin/env bash
# Second QEMU lane: boot the REAL Rotate OS kernel (build.sh kernel-virt: our patches + trim fragment + virt.d) on the
# built Rotate OS image (qcow2 overlays, root=/dev/vda3, no initramfs) and prove, scripted:
#   scenario A (no eMMC)  : binderfs + binder-control, cgroup v2 controllers, PSI, veth+bridge, uinput+swivel helper,
#                           first-boot service failure path (marker, boot continues)
#   scenario B (fake eMMC): first-boot service runs for real at boot, extracts firmware from raw GPT partitions
#                           (synthetic payloads, qemu/make-fake-emmc.sh), writes initrd-fw.cpio.gz, powers the VM off;
#                           second boot asserts files, rebuilt initrd, and that the service does not run again.
# Nothing here touches a block device or creates a filesystem. Exit 0 = all pass. Logs: qemu/artifacts/rotate/.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(dirname "$HERE"); A=$HERE/artifacts/rotate; I=$HERE/images
PORT=${ROTATE_SSH_PORT:-2223}; BOOT_TIMEOUT=${BOOT_TIMEOUT:-600}; POWEROFF_TIMEOUT=${POWEROFF_TIMEOUT:-300}
mkdir -p "$A" "$I"; rm -f "$A"/*.log "$A"/summary.txt
KIMG=$ROOT/build/out/kernel-virt/Image; IMG=$ROOT/build/out/image/rotate-os.img; BASE=$I/rotate-os-base.raw
[ -f "$KIMG" ] || { echo "FAIL: $KIMG missing (build/build.sh kernel-virt)"; exit 3; }
if [ ! -f "$BASE" ] || [ "$IMG" -nt "$BASE" ]; then "$HERE/prepare-vm.sh" -f >"$A/prepare.log" 2>&1 || { echo "FAIL: prepare-vm.sh"; exit 3; }; fi
echo "kernel Image: $KIMG ($(cat "$ROOT/build/out/kernel-virt/release")) sha256=$(sha256sum "$KIMG" | cut -c1-16)"
echo "base image  : $BASE (read-only backing; guest writes go to qcow2 overlays in $I)"
# guest payload (raw tar on a raw virtio disk, no filesystem)
[ -x "$I/rotate-binder-add" ] || aarch64-linux-gnu-gcc -static -O2 -Wall -o "$I/rotate-binder-add" "$HERE/guest/rotate-binder-add.c" || exit 3
[ -x "$HERE/guest/rotate-swivel" ] || aarch64-linux-gnu-gcc -static -O2 -Wall -o "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-swivel.c" || exit 3
W=$(TMPDIR=/tmp mktemp -d /tmp/rotate-rk-XXXX); mkdir -p "$W/usr/local/bin"
install -m755 "$I/rotate-binder-add" "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-kernel-check.sh" "$HERE/guest/rotate-firstboot-check.sh" "$W/usr/local/bin/"
tar -C "$W" --owner=0 --group=0 -cf "$I/payload-rotate.tar" usr; rm -rf "$W"
EMMC=$I/fake-emmc.raw; read -r SA SP SW < <("$HERE/make-fake-emmc.sh" "$EMMC" 2>"$A/make-fake-emmc.log")
[ -n "${SA:-}" ] || { echo "FAIL: make-fake-emmc.sh"; exit 3; }
echo "fake eMMC   : $EMMC (GPT via sgdisk, no filesystem); expected sha256 agdsp=${SA:0:12} pm_sys=${SP:0:12} wcn=${SW:0:12}"
rc=0; declare -A RES; declare -A SECS
newoverlay() { rm -f "$1"; qemu-img create -q -f qcow2 -F raw -b "$BASE" "$1"; }
bootvm() { # name overlay [extra run-vm args]; starts VM headless in background, sets QPID
  local n=$1 ov=$2; shift 2
  rm -f "$I/rk-serial.sock" "$I/rk-qmp.sock"
  "$HERE/run-vm.sh" -k rotate -H -i "$ov" -P "$I/payload-rotate.tar" -p "$PORT" -s "$I/rk-serial.sock" -q "$I/rk-qmp.sock" -L "$A/$n.serial.log" "$@" >"$A/$n.qemu.log" 2>&1 &
  QPID=$!
}
guestrun() { # name cmd...
  local n=$1; shift
  python3 -I "$HERE/serial-run.py" "$I/rk-serial.sock" "$A/$n.login.log" "$BOOT_TIMEOUT" -- "$@" | tee "$A/$n.guest.log"
  return "${PIPESTATUS[0]}"
}
stopvm() { kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null; }
trap 'stopvm' EXIT

echo "== scenario A: kernel features (no eMMC attached)"
T0=$(date +%s); newoverlay "$I/rk-a.qcow2"; bootvm A "$I/rk-a.qcow2"
guestrun A "tar -xf /dev/vdb -C / && echo RV_INFO uptime=\$(cut -d. -f1 /proc/uptime)s && /usr/local/bin/rotate-kernel-check.sh"
[ $? = 0 ] && RES[A]=PASS || { RES[A]=FAIL; rc=1; }
stopvm; SECS[A]=$(( $(date +%s)-T0 ))

echo "== scenario B1: first boot with fake eMMC; service must extract and power the VM off"
T0=$(date +%s); newoverlay "$I/rk-b.qcow2"; bootvm B1 "$I/rk-b.qcow2" -E "$EMMC"
SECONDS_START=$(date +%s); ok=0
while kill -0 "$QPID" 2>/dev/null; do
  [ $(( $(date +%s)-SECONDS_START )) -gt "$POWEROFF_TIMEOUT" ] && break; sleep 2
done
if kill -0 "$QPID" 2>/dev/null; then echo "FAIL: VM still running after ${POWEROFF_TIMEOUT}s (no poweroff)"; stopvm; ok=1; QRC=timeout
else wait "$QPID"; QRC=$?; echo "qemu exited by itself, status $QRC after $(( $(date +%s)-T0 ))s (guest poweroff)"; [ "$QRC" = 0 ] || ok=1; fi
L=$A/B1.serial.log
for pat in "extract-firmware: .*agdsp.bin from .*l_agdsp_b" "l_agdsp_a is not the expected image" "extract-firmware: .*pm_sys.bin from .*pm_sys_a" "wcnmodem_a is not the expected image" "extract-firmware: .*wcnmodem.bin from .*wcnmodem_b" "rotate-firstboot: firmware initrd written, powering off"; do
  if grep -aqE "$pat" "$L"; then echo "RV_PASS B1 serial: $pat"; else echo "RV_FAIL B1 serial: $pat"; ok=1; fi
done
grep -aE "extract-firmware|rotate-firstboot" "$L" | sed 's/\r//' | head -20
[ $ok = 0 ] && RES[B1]=PASS || { RES[B1]=FAIL; rc=1; }
SECS[B1]=$(( $(date +%s)-T0 ))

echo "== scenario B2: second boot on the same overlay; verify files, rebuilt initrd, service does not run again"
T0=$(date +%s); bootvm B2 "$I/rk-b.qcow2" -E "$EMMC"
guestrun B2 "tar -xf /dev/vdb -C / && echo RV_INFO uptime=\$(cut -d. -f1 /proc/uptime)s && /usr/local/bin/rotate-firstboot-check.sh $SA $SP $SW; sync"
[ $? = 0 ] && RES[B2]=PASS || { RES[B2]=FAIL; rc=1; }
stopvm; SECS[B2]=$(( $(date +%s)-T0 ))
{ for k in A B1 B2; do printf '%-4s %-5s %4ss\n' "$k" "${RES[$k]}" "${SECS[$k]}"; done; } | tee "$A/summary.txt"
[ $rc = 0 ] && echo "ROTATE-KERNEL LANE PASS" || echo "ROTATE-KERNEL LANE FAIL"
exit $rc
