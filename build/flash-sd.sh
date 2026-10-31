#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# flash-sd.sh — Safe microSD writer for Rotate OS
# Verifies that target is a removable block device and matches microSD sizes (8 GiB .. 512 GiB).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_FILE="${REPO_ROOT}/build/out/image/rotate-os.img"
CHECKSUM_FILE="${REPO_ROOT}/build/out/image/SHA256SUMS"

usage() {
    echo "Usage: $0 /dev/sdX (or /dev/mmcblkX)" >&2
    echo "       $0 /dev/sdX [path-to-image.img]" >&2
    exit 1
}

[[ $# -ge 1 ]] || usage
TARGET="$1"
if [[ $# -ge 2 ]]; then
    IMAGE_FILE="$2"
fi

# 1. Root check
if [[ $EUID -ne 0 ]]; then
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

# 4. Safety: Refuse non-removable devices (internal disks/NVMe)
REMOVABLE=$(cat "$SYS_BLOCK/removable" 2>/dev/null || echo "0")
if [[ "$REMOVABLE" != "1" ]]; then
    # For mmcblk devices, check if it's on an MMC/SD bus or removable card reader
    if [[ "$DEV_NAME" =~ ^mmcblk[0-9]+$ ]]; then
        # Check if type is SD
        DEV_TYPE=$(cat "$SYS_BLOCK/device/type" 2>/dev/null || echo "")
        if [[ "$DEV_TYPE" != "SD" ]]; then
            echo "ERROR: Target '$TARGET' is not marked removable ($SYS_BLOCK/removable = $REMOVABLE). Refusing." >&2
            exit 1
        fi
    else
        echo "ERROR: Target '$TARGET' is not marked removable ($SYS_BLOCK/removable = $REMOVABLE). Refusing." >&2
        exit 1
    fi
fi

# Safety: Refuse root device or active swap
MOUNTED_PARTS=$(lsblk -rn -o MOUNTPOINT "$TARGET" | grep -v '^$' || true)
if echo "$MOUNTED_PARTS" | grep -E '^/(|boot|home|usr|var|etc)$' >/dev/null; then
    echo "FATAL: Target '$TARGET' contains system mountpoints! Refusing." >&2
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

# Unmount all mounted partitions on target
for part in $(lsblk -ln -o NAME "$TARGET" | tail -n +2); do
    DEV_PART="/dev/$part"
    if mountpoint -q "$(lsblk -no MOUNTPOINT "$DEV_PART" 2>/dev/null || true)"; then
        echo "Unmounting $DEV_PART..."
        umount "$DEV_PART"
    fi
done

# 7. Write image
echo "Writing image to $TARGET..."
dd if="$IMAGE_FILE" of="$TARGET" bs=4M conv=fsync status=progress
sync

echo "Verifying partition table on $TARGET..."
partprobe "$TARGET" 2>/dev/null || true
lsblk "$TARGET"

echo ""
echo "SUCCESS: Rotate OS image written to $TARGET."
