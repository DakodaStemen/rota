#!/usr/bin/env bash
# Prepare the QEMU test inputs with no filesystem creation, no loop mounts, no sudo.
#  1. qcow2 copy-on-write overlay on top of the built Rotate OS SD image (the raw image is never written).
#  2. ALARM generic kernel Image + initramfs (bsdtar, only those two members) into images/boot/.
#  3. images/payload.tar: raw tar archive attached as a 2nd virtio-blk disk (no filesystem on it); the guest
#     does `tar -xf /dev/vdb -C /` to get generic-kernel modules (virtio-gpu, uinput, virtio_net + deps) and
#     the test helpers. Rotate OS's rootfs has no generic kernel modules.
# Usage: prepare-vm.sh [-f]   (-f recreates the overlay and payload)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(dirname "$HERE")
RAW=${RAW:-$ROOT/build/out/image/rotate-os.img}
TARBALL=${TARBALL:-$ROOT/upstream/alarm/ArchLinuxARM-aarch64-latest.tar.gz}
OVL=$HERE/rotate-os-test.qcow2; KVER=7.1.6-1-aarch64-ARCH
FORCE=0; [ "${1:-}" = -f ] && FORCE=1
[ -f "$RAW" ] || { echo "missing $RAW (run build/build.sh)" >&2; exit 1; }
[ -f "$TARBALL" ] || { echo "missing $TARBALL" >&2; exit 1; }
mkdir -p "$HERE/images/boot" "$HERE/artifacts"
# The build can be re-run while we test (it rewrites build/out/image in place), which would corrupt a qcow2
# overlay mid-boot. So pin a sparse byte copy (cp, not formatting) as the overlay's backing file; the copy is
# only ever opened read-only. Set PIN=0 to back the overlay directly on $RAW.
if [ "${PIN:-1}" = 1 ]; then
  BASE=$HERE/images/rotate-os-base.raw
  if [ ! -f "$BASE" ] || [ $FORCE = 1 ]; then cp --sparse=always "$RAW" "$BASE.tmp" && mv "$BASE.tmp" "$BASE"; fi
  chmod a-w "$BASE"; BACK=$BASE
else BACK=$(realpath "$RAW"); fi
if [ ! -f "$OVL" ] || [ $FORCE = 1 ]; then
  rm -f "$OVL"; qemu-img create -q -f qcow2 -F raw -b "$BACK" "$OVL"
fi
if [ ! -f "$HERE/images/boot/Image" ] || [ ! -f "$HERE/images/boot/initramfs-linux.img" ]; then
  W=$(TMPDIR=/tmp mktemp -d /tmp/rotate-qemu-XXXX)
  bsdtar -C "$W" -xf "$TARBALL" --include boot/Image --include boot/initramfs-linux.img 2>&1 | grep -v 'unknown extended header' || true
  cp "$W/boot/Image" "$W/boot/initramfs-linux.img" "$HERE/images/boot/"; rm -rf "$W"
fi
if [ ! -f "$HERE/images/payload.tar" ] || [ $FORCE = 1 ]; then
  [ -x "$HERE/guest/rotate-swivel" ] || aarch64-linux-gnu-gcc -static -O2 -Wall -o "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-swivel.c"
  W=$(TMPDIR=/tmp mktemp -d /tmp/rotate-qemu-XXXX); M=usr/lib/modules/$KVER
  K=kernel/drivers
  bsdtar -C "$W" -xf "$TARBALL" \
    --include "./$M/modules.*" \
    --include "./$M/$K/gpu/drm/virtio/virtio-gpu.ko" --include "./$M/$K/virtio/virtio_dma_buf.ko" \
    --include "./$M/$K/gpu/drm/drm_shmem_helper.ko" --include "./$M/$K/gpu/drm/clients/drm_client_lib.ko" \
    --include "./$M/$K/input/misc/uinput.ko" --include "./$M/$K/net/virtio_net.ko" \
    --include "./$M/$K/net/net_failover.ko" --include "./$M/kernel/net/core/failover.ko" 2>&1 | grep -v 'unknown extended header' || true
  mkdir -p "$W/usr/local/bin"
  install -m755 "$HERE/guest/rotate-swivel" "$HERE/guest/rotate-vm-check.sh" "$W/usr/local/bin/"
  tar -C "$W" --owner=0 --group=0 -cf "$HERE/images/payload.tar" usr
  rm -rf "$W"
fi
echo "overlay: $OVL (backing: $BACK)"; ls -l "$HERE/images/boot" "$HERE/images/payload.tar"
