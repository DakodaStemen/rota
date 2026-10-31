#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# flash-sd.sh — Safe microSD writer for Rotate OS
# Refuses non-removable devices and the disk holding /, checks microSD size, needs the device name typed to confirm. --dry-run writes nothing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_FILE="${REPO_ROOT}/build/out/image/rotate-os.img"
CHECKSUM_FILE="${REPO_ROOT}/build/out/image/SHA256SUMS"

usage() {
    cat >&2 <<EOF
Usage: $0 [--dry-run] [-h|--help] DEVICE [IMAGE]
  DEVICE   whole removable disk, e.g. /dev/sdX (partitions are refused)
  IMAGE    default: build/out/image/rotate-os.img
  --dry-run  run every safety check, print what would be written, write nothing
             (does not need root, does not ask for confirmation)
Refuses: non-block devices, partitions, non-removable devices (removable != 1),
any disk that holds / or another system mount or swap (also via LVM/crypt),
sizes outside 7..550 GiB. Writing needs the device name typed back to confirm.
Exit codes: 0 ok / dry-run passed; 1 refused or error; 2 usage.
EOF
}

DRY_RUN=0
ARGS=()
for a in "$@"; do
    case "$a" in
        -h|--help) usage; exit 0 ;;
        --dry-run) DRY_RUN=1 ;;
        *) ARGS+=("$a") ;;
    esac
done
[[ ${#ARGS[@]} -ge 1 && ${#ARGS[@]} -le 2 ]] || { usage; exit 2; }
TARGET="${ARGS[0]}"
[[ ${#ARGS[@]} -ge 2 ]] && IMAGE_FILE="${ARGS[1]}"

# 1. Root check (writing only)
if [[ $DRY_RUN -eq 0 && $EUID -ne 0 ]]; then
    echo "ERROR: This script must be run as root (sudo $0 $TARGET)" >&2
    exit 1
fi

# 2. Check image existence
if [[ ! -f "$IMAGE_FILE" ]]; then
    echo "ERROR: Image file not found: $IMAGE_FILE" >&2
    exit 1
fi

# Verify checksum if SHA256SUMS is next to the image
IMG_DIR="$(dirname "$IMAGE_FILE")"
IMG_BASE="$(basename "$IMAGE_FILE")"
if [[ -f "$IMG_DIR/SHA256SUMS" ]]; then
    echo "Verifying image checksum..."
    EXPECTED_HASH=$(grep "  $IMG_BASE$" "$IMG_DIR/SHA256SUMS" | awk '{print $1}' || true)
    if [[ -n "$EXPECTED_HASH" ]]; then
        ACTUAL_HASH=$(sha256sum "$IMAGE_FILE" | awk '{print $1}')
        if [[ "$EXPECTED_HASH" != "$ACTUAL_HASH" ]]; then
            echo "ERROR: Image checksum verification failed!" >&2
            echo "Expected: $EXPECTED_HASH" >&2
            echo "Actual:   $ACTUAL_HASH" >&2
            exit 1
        fi
        echo "Image checksum OK ($ACTUAL_HASH)"
    fi
fi

# 3. Check target device exists and is a block device
if [[ ! -b "$TARGET" ]]; then
    echo "ERROR: Target '$TARGET' is not a valid block device." >&2
    exit 1
fi

DEV_NAME=$(basename "$TARGET")
SYS_BLOCK="/sys/block/$DEV_NAME"
if [[ ! -d "$SYS_BLOCK" ]]; then
    echo "ERROR: '$TARGET' appears to be a partition, not a whole disk device." >&2
    echo "Specify the whole disk (e.g. /dev/sdb, not /dev/sdb1)." >&2
    exit 1
fi

# 4. Safety: refuse non-removable devices (internal disks, NVMe, loop, built-in eMMC). No exceptions.
REMOVABLE=$(cat "$SYS_BLOCK/removable" 2>/dev/null || echo "0")
if [[ "$REMOVABLE" != "1" ]]; then
    echo "ERROR: Target '$TARGET' is not marked removable ($SYS_BLOCK/removable = $REMOVABLE). Refusing." >&2
    exit 1
fi

# Safety: refuse the disk that holds / (directly or via LVM/crypt/partition) and any system mount or swap.
ROOT_SRC=$(findmnt -n -o SOURCE / 2>/dev/null || true)
if [[ -n "$ROOT_SRC" && -b "$ROOT_SRC" ]]; then
    if lsblk -nsro NAME "$ROOT_SRC" 2>/dev/null | grep -qx "$DEV_NAME"; then
        echo "FATAL: Target '$TARGET' holds the root filesystem ($ROOT_SRC). Refusing." >&2
        exit 1
    fi
fi
if lsblk -nr -o MOUNTPOINTS "$TARGET" 2>/dev/null | tr ' ' '\n' | grep -E '^(/|/boot|/boot/efi|/home|/usr|/var|/etc|\[SWAP\])$' >/dev/null; then
    echo "FATAL: Target '$TARGET' contains system mountpoints or swap! Refusing." >&2
    exit 1
fi

# 5. Size check: must match standard microSD card capacity (8 GiB .. 512 GiB)
# Size in bytes
SIZE_BYTES=$(blockdev --getsize64 "$TARGET")
SIZE_GIB=$(awk -v b="$SIZE_BYTES" 'BEGIN { printf "%.2f", b / (1024*1024*1024) }')

MIN_BYTES=$(( 7 * 1024 * 1024 * 1024 ))      # 7 GiB minimum (nominally 8 GB)
MAX_BYTES=$(( 550 * 1024 * 1024 * 1024 ))    # 550 GiB maximum (nominally 512 GB)

if [[ $SIZE_BYTES -lt $MIN_BYTES || $SIZE_BYTES -gt $MAX_BYTES ]]; then
    echo "ERROR: Target '$TARGET' size ($SIZE_GIB GiB / $SIZE_BYTES bytes) is outside valid microSD bounds (8 GiB .. 512 GiB)." >&2
    exit 1
fi

# 6. Show target details and confirm
echo "==================================================="
echo "TARGET DEVICE VERIFICATION"
echo "Device:      $TARGET"
echo "Model:       $(cat "$SYS_BLOCK/device/model" 2>/dev/null || cat "$SYS_BLOCK/device/name" 2>/dev/null || echo 'Unknown')"
echo "Size:        $SIZE_GIB GiB ($SIZE_BYTES bytes)"
echo "Removable:   $REMOVABLE"
echo "Image:       $IMAGE_FILE"
echo "==================================================="

if [[ $DRY_RUN -eq 1 ]]; then
    echo "DRY RUN: all checks passed; would write $IMAGE_FILE to $TARGET. Nothing written."
    exit 0
fi

echo "This ERASES everything on $TARGET."
read -r -p "Type the device name ($DEV_NAME) to confirm: " ANSWER
if [[ "$ANSWER" != "$DEV_NAME" ]]; then
    echo "ERROR: confirmation did not match '$DEV_NAME'. Aborting, nothing written." >&2
    exit 1
fi

# Unmount all mounted partitions on target
while read -r part mp; do
    [[ -n "$mp" ]] || continue
    echo "Unmounting /dev/$part..."
    umount "/dev/$part"
done < <(lsblk -nr -o NAME,MOUNTPOINT "$TARGET" | tail -n +2)

# 7. Write image
echo "Writing image to $TARGET..."
dd if="$IMAGE_FILE" of="$TARGET" bs=4M conv=fsync status=progress
sync

echo "Verifying partition table on $TARGET..."
partprobe "$TARGET" 2>/dev/null || true
lsblk "$TARGET"

echo ""
echo "SUCCESS: Rotate OS image written to $TARGET."
