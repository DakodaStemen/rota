#!/bin/sh
# Grow partition 3 (rootfs) to the end of the card and resize2fs it. Keeps the partition UUID (root=PARTUUID=
# stays valid). Moves the backup GPT to the end of the device first. Runs once.
set -eu
ROOT=$(findmnt -no SOURCE /)
case "$ROOT" in /dev/mmcblk*p3) DISK=${ROOT%p3};; /dev/sd*3) DISK=${ROOT%3};; *) echo "rotate-resize: unexpected root $ROOT" >&2; exit 0;; esac
mkdir -p /var/lib/rotate
sfdisk --relocate gpt-bak-std "$DISK" || true
echo ", +" | sfdisk --no-reread --force -N 3 "$DISK" || true
partx -u "$DISK" || true
resize2fs "$ROOT"
touch /var/lib/rotate/resized
