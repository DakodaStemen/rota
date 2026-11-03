#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# build-gles-test.sh : build rootfs/src/rotate-gles-test.c natively (aarch64, qemu-user) in a THROW-AWAY overlay of the
# melonds chroot (the chroot itself is not modified: its compiler/packages must stay the ones that built the PGO core).
# Output: out/rotate-gles-test/rotate-gles-test (aarch64 ELF, links the ALARM mesa/libdrm that the rootfs ships).
# Needs sudo + network (pacman -Syu inside the overlay). Exit: 0 ok, 2 chroot missing, 1 compile failed.
set -euo pipefail
B=$(cd "$(dirname "$0")/.." && pwd); CH=$B/work/melonds-chroot; O=$B/out/rotate-gles-test; U=$B/work/gles-overlay
[[ -f $CH/.ready ]] || { echo "no $CH (run build.sh melonds first)" >&2; exit 2; }
mkdir -p "$O"; sudo rm -rf "$U"; mkdir -p "$U/upper" "$U/work" "$U/merged"
sudo mount -t overlay overlay -o "lowerdir=$CH,upperdir=$U/upper,workdir=$U/work" "$U/merged"
trap 'sudo umount "$U/merged"; sudo rm -rf "$U"' EXIT
sudo systemd-nspawn -q --register=no --resolv-conf=replace-host -D "$U/merged" \
  --bind-ro="$B/rootfs/src:/src" --bind="$O:/out" bash -ec '
  pacman -Syu --noconfirm --needed mesa libdrm >/dev/null
  gcc -O2 -Wall -Wextra -o /out/rotate-gles-test /src/rotate-gles-test.c -lEGL -lGLESv2 -lgbm -ldrm $(pkg-config --cflags libdrm)
  file /out/rotate-gles-test; objdump -T /out/rotate-gles-test | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1; pacman -Q mesa libdrm glibc'
sudo chown -R "$(id -u):$(id -g)" "$O"
( cd "$O" && sha256sum rotate-gles-test | tee SHA256SUMS )
