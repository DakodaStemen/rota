#!/usr/bin/env bash
# Build a QEMU test disk image (single ext4 rootfs, no partition table) from the ALARM tarball.
# Uses a loop FILE only; refuses any block-device path. Needs sudo (mount, tar ownership).
# Usage: build-vm-image.sh [-s SIZE_GB] [-f] [IMAGE]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
TARBALL=${TARBALL:-$ROOT/upstream/alarm/ArchLinuxARM-aarch64-latest.tar.gz}
SIZE_GB=6; FORCE=0
while getopts s:f o; do case $o in s) SIZE_GB=$OPTARG;; f) FORCE=1;; *) exit 64;; esac; done
shift $((OPTIND-1))
IMG=${1:-$HERE/images/alarm-rootfs.img}
case "$IMG" in /dev/*) echo "refusing device path: $IMG" >&2; exit 1;; esac
[ -b "$IMG" ] && { echo "refusing block device: $IMG" >&2; exit 1; }
[ -f "$TARBALL" ] || { echo "missing tarball $TARBALL" >&2; exit 1; }
if [ -e "$IMG" ] && [ $FORCE = 0 ]; then echo "$IMG exists (use -f to rebuild)"; exit 0; fi
mkdir -p "$HERE/images/boot" "$HERE/artifacts"
rm -f "$IMG"; truncate -s "${SIZE_GB}G" "$IMG"
MKE2FS=$(command -v mke2fs)
"$MKE2FS" -q -t ext4 -F -L rootfs "$IMG"
MNT=$(TMPDIR=/tmp mktemp -d /tmp/rotate-img-XXXX)
cleanup() { sudo umount "$MNT" 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT
sudo mount -o loop "$IMG" "$MNT"
echo "extracting tarball (takes a minute)..."
sudo tar --numeric-owner --xattrs --xattrs-include='*' -xpzf "$TARBALL" -C "$MNT" 2>&1 | grep -v 'Ignoring unknown extended header' || true
# kernel + initramfs for direct boot
sudo cp "$MNT/boot/Image" "$HERE/images/boot/Image"
sudo cp "$MNT/boot/initramfs-linux.img" "$HERE/images/boot/initramfs-linux.img"
sudo chown "$(id -u):$(id -g)" "$HERE"/images/boot/*
# guest config
echo rotate-vm | sudo tee "$MNT/etc/hostname" >/dev/null
echo '/dev/vda / ext4 rw,noatime 0 1' | sudo tee "$MNT/etc/fstab" >/dev/null
printf 'virtio-gpu\nuinput\n' | sudo tee "$MNT/etc/modules-load.d/rotate-vm.conf" >/dev/null
# guest helpers
[ -x "$HERE/guest/rotate-swivel" ] || aarch64-linux-gnu-gcc -static -O2 -o "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-swivel.c"
sudo install -m755 "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-vm-check.sh" "$MNT/usr/local/bin/"
# ssh: key-based root login (key stays in qemu/images)
[ -f "$HERE/images/vm_key" ] || ssh-keygen -q -t ed25519 -N '' -C rotate-vm-test -f "$HERE/images/vm_key"
sudo install -d -m700 "$MNT/root/.ssh"
sudo install -m600 "$HERE/images/vm_key.pub" "$MNT/root/.ssh/authorized_keys"
echo 'PermitRootLogin prohibit-password' | sudo tee "$MNT/etc/ssh/sshd_config.d/99-rotate-vm.conf" >/dev/null
sudo ln -sf /usr/lib/systemd/system/sshd.service "$MNT/etc/systemd/system/multi-user.target.wants/sshd.service"
sync
echo "image: $IMG  kernel: $HERE/images/boot/Image"
