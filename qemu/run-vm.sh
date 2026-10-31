#!/usr/bin/env bash
# Boot the Rotate OS userspace test VM (aarch64 virt, TCG, 720x720 virtio-gpu).
# Usage: run-vm.sh [-k generic|rotate] [-E EMMC_RAW] [-L SERIAL_LOG] [-P PAYLOAD] [-H] [-i IMAGE] [-p SSH_PORT] [-s SERIAL_SOCK] [-q QMP_SOCK] [-m MEM_MB] [-e] [-u] [-S]
#   -k rotate  (also masks iwd.service: the VM has no Wi-Fi, and iwd's restart loop can hold network.target, hence the login getty, for 10+ min)
#              boot the REAL Rotate OS kernel built by `build.sh kernel-virt` (build/out/kernel-virt/Image): virtio/PCI/PL011
#      built in, root=/dev/vda3, NO initramfs. Default `generic` = ALARM kernel + initramfs (smoke-test.sh).
#   -E FILE   attach FILE (raw, read-only) as an extra virtio-blk disk = fake eMMC for the first-boot firmware test
#   -L FILE   also log the serial console to FILE (captures output before anyone connects to the socket)
#   -P FILE   payload tar (2nd virtio disk) override
#   -H  headless (-display none); serial on unix socket, QMP socket for screendump
#   -e  boot via UEFI (AAVMF) instead of direct -kernel (needs a grub on the image; not used by smoke test)
#   -u  UEFI firmware only, no disk/kernel (display self-test)
#   default (no -H): GTK/SDL window + serial on this terminal (stdio)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
IMG=$HERE/rotate-os-test.qcow2; PAYLOAD=$HERE/images/payload.tar; SSH_PORT=2222; MEM=4096
SERIAL_SOCK=$HERE/images/serial.sock; QMP_SOCK=$HERE/images/qmp.sock
HEADLESS=0; UEFI=0; FWONLY=0; SNAP=0; KMODE=generic; EMMC=; SLOG=
while getopts Hi:p:s:q:m:euSk:E:L:P: o; do case $o in
  k) KMODE=$OPTARG;; E) EMMC=$OPTARG;; L) SLOG=$OPTARG;; P) PAYLOAD=$OPTARG;;
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
  if [ -n "$EMMC" ]; then
    case "$EMMC" in /dev/*) echo "refusing device path" >&2; exit 1;; esac
    [ -f "$EMMC" ] || { echo "no eMMC file $EMMC" >&2; exit 1; }
    args+=(-drive if=none,id=d2,format=raw,readonly=on,file="$EMMC" -device virtio-blk-pci,drive=d2)
  fi
  if [ $UEFI = 0 ] && [ "$KMODE" = rotate ]; then
    KIMG=$HERE/../build/out/kernel-virt/Image
    [ -f "$KIMG" ] || { echo "no $KIMG; run build/build.sh kernel-virt" >&2; exit 1; }
    args+=(-kernel "$KIMG" -append "root=/dev/vda3 rw rootwait console=ttyAMA0 loglevel=4 systemd.mask=iwd.service")
  elif [ $UEFI = 0 ]; then
    args+=(-kernel "$HERE/images/boot/Image" -initrd "$HERE/images/boot/initramfs-linux.img"
           -append "root=/dev/vda3 rw rootwait console=ttyAMA0 loglevel=4")
  fi
fi
if [ $HEADLESS = 1 ]; then
  rm -f "$SERIAL_SOCK" "$QMP_SOCK"
  LF=; [ -n "$SLOG" ] && LF=,logfile="$SLOG"
  args+=(-display none -chardev socket,id=ser0,path="$SERIAL_SOCK",server=on,wait=off${LF} -serial chardev:ser0
         -qmp unix:"$QMP_SOCK",server=on,wait=off)
else
  args+=(-display gtk -serial mon:stdio -qmp unix:"$QMP_SOCK",server=on,wait=off)
fi
echo "+ qemu-system-aarch64 ${args[*]}" >&2
exec qemu-system-aarch64 "${args[@]}"
