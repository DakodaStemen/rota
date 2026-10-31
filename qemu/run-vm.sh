#!/usr/bin/env bash
# Boot the Rotate OS userspace test VM (aarch64 virt, TCG, 720x720 virtio-gpu).
# Usage: run-vm.sh [-H] [-i IMAGE] [-p SSH_PORT] [-s SERIAL_SOCK] [-q QMP_SOCK] [-m MEM_MB] [-e] [-u] [-S]
#   -H  headless (-display none); serial on unix socket, QMP socket for screendump
#   -e  boot via UEFI (AAVMF) instead of direct -kernel (needs a grub on the image; not used by smoke test)
#   -u  UEFI firmware only, no disk/kernel (display self-test)
#   default (no -H): GTK/SDL window + serial on this terminal (stdio)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
IMG=$HERE/rotate-os-test.qcow2; PAYLOAD=$HERE/images/payload.tar; SSH_PORT=2222; MEM=4096
SERIAL_SOCK=$HERE/images/serial.sock; QMP_SOCK=$HERE/images/qmp.sock
HEADLESS=0; UEFI=0; FWONLY=0; SNAP=0
while getopts Hi:p:s:q:m:euS o; do case $o in
  H) HEADLESS=1;; i) IMG=$OPTARG;; p) SSH_PORT=$OPTARG;; s) SERIAL_SOCK=$OPTARG;; q) QMP_SOCK=$OPTARG;;
  m) MEM=$OPTARG;; e) UEFI=1;; u) FWONLY=1;; S) SNAP=1;; *) exit 64;; esac; done
case "$IMG" in /dev/*) echo "refusing device path" >&2; exit 1;; esac
CPU=${QEMU_CPU:-max}; SMP=${QEMU_SMP:-4}
args=(-machine virt,gic-version=3 -accel tcg,thread=multi -cpu "$CPU" -smp "$SMP" -m "$MEM"
  -device virtio-gpu-pci,xres=720,yres=720,edid=on
  -device qemu-xhci -device usb-kbd -device usb-tablet
  -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22" -device virtio-net-pci,netdev=n0
  -device virtio-rng-pci)
if [ $UEFI = 1 ] || [ $FWONLY = 1 ]; then
  VARS=$HERE/images/AAVMF_VARS.fd; [ -f "$VARS" ] || { mkdir -p "$HERE/images"; cp /usr/share/AAVMF/AAVMF_VARS.fd "$VARS"; }
  args+=(-drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.no-secboot.fd
         -drive if=pflash,format=raw,file="$VARS")
fi
SNAPOPT=""; [ $SNAP = 1 ] && SNAPOPT=,snapshot=on
if [ $FWONLY = 0 ]; then
  [ -f "$IMG" ] || { echo "no image $IMG; run prepare-vm.sh" >&2; exit 1; }
  args+=(-drive if=none,id=d0,format=qcow2,file="$IMG"${SNAPOPT} -device virtio-blk-pci,drive=d0)
  if [ -f "$PAYLOAD" ]; then args+=(-drive if=none,id=d1,format=raw,readonly=on,file="$PAYLOAD" -device virtio-blk-pci,drive=d1); fi
  if [ $UEFI = 0 ]; then
    args+=(-kernel "$HERE/images/boot/Image" -initrd "$HERE/images/boot/initramfs-linux.img"
           -append "root=/dev/vda3 rw rootwait console=ttyAMA0 loglevel=4")
  fi
fi
if [ $HEADLESS = 1 ]; then
  rm -f "$SERIAL_SOCK" "$QMP_SOCK"
  args+=(-display none -chardev socket,id=ser0,path="$SERIAL_SOCK",server=on,wait=off -serial chardev:ser0
         -qmp unix:"$QMP_SOCK",server=on,wait=off)
else
  args+=(-display gtk -serial mon:stdio -qmp unix:"$QMP_SOCK",server=on,wait=off)
fi
echo "+ qemu-system-aarch64 ${args[*]}" >&2
exec qemu-system-aarch64 "${args[@]}"
