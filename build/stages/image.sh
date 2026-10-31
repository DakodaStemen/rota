#!/usr/bin/env bash
# Stage image: GPT SD image in a regular FILE attached to a loop device (never a real block device).
#   p1 uboot  (1 MiB..17 MiB, raw uboot_dhtb.img at partition start)
#   p2 bootfs (FAT32): extlinux/extlinux.conf, Image, dtb/sprd/ums512-rg-rotate.dtb, initrd-fw.cpio.gz (stub)
#   p3 rootfs (ext4)
# Output: out/image/rotate-os.img (+ .sha256), plus a copy of the SPL (flashed to eMMC boot0 separately).
. "$(dirname "$0")/../lib/common.sh"
IMG="$OUT/image/rotate-os.img"
for f in "$OUT/uboot/uboot_dhtb.img" "$OUT/kernel/Image" "$OUT/kernel/ums512-rg-rotate.dtb" "$OUT/rootfs/rootfs.tar.zst" "$OUT/spl/spl_a_open.img"; do
  [[ -f "$f" ]] || { STAGE=image; die "missing input $f (run earlier stages)"; }
done
stage_begin image "$(cat "$OUT"/.uboot.stamp "$OUT"/.kernel.stamp "$OUT"/.rootfs.stamp "$OUT"/.spl.stamp) $BOOT_PART_MIB $IMAGE_SIZE_MIB $(file_hash "$0")" "$IMG"
mkdir -p "$OUT/image"; M="$WORK/mnt"; mkdir -p "$M"
TMPIMG="$IMG.part"; rm -f "$TMPIMG"
LOOP=""
cleanup() {
  set +e
  mountpoint -q "$M/boot" && sudo umount "$M/boot"
  mountpoint -q "$M" && sudo umount "$M"
  [[ -n "$LOOP" ]] && sudo losetup -d "$LOOP"
}
trap cleanup EXIT
truncate -s "${IMAGE_SIZE_MIB}M" "$TMPIMG"
# fixed GUIDs => PARTUUID is deterministic (root=PARTUUID=...) and survives rotate-resize
DISK_GUID=7a1b2c3d-0000-4000-8000-526f7461744f
P3_GUID=7a1b2c3d-0003-4000-8000-526f7461744f
BOOT_START=$((UBOOT_PART_START_MIB + UBOOT_PART_MIB))
ROOT_START=$((BOOT_START + BOOT_PART_MIB))
sfdisk --quiet "$TMPIMG" <<SF
label: gpt
label-id: $DISK_GUID
1 : name="uboot",  start=${UBOOT_PART_START_MIB}MiB, size=${UBOOT_PART_MIB}MiB, type=8DA63339-0007-60C0-C436-083AC8230908
2 : name="bootfs", start=${BOOT_START}MiB, size=${BOOT_PART_MIB}MiB, type=BC13C2FF-59E6-4262-A352-B275FD6F7172
3 : name="rootfs", start=${ROOT_START}MiB, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, uuid=$P3_GUID
SF
LOOP="$(sudo losetup --find --show --partscan "$TMPIMG")"
case "$LOOP" in /dev/loop*) ;; *) die "refusing: $LOOP is not a loop device";; esac
[[ "$(sudo losetup -nO BACK-FILE "$LOOP" | tr -d ' ')" == "$TMPIMG" ]] || die "loop device does not map our image file"
say "loop $LOOP"; sudo partprobe "$LOOP" 2>/dev/null || true
for i in 1 2 3; do for _ in $(seq 50); do [[ -b ${LOOP}p$i ]] && break; sleep 0.1; done; [[ -b ${LOOP}p$i ]] || die "no ${LOOP}p$i"; done
# p1: raw DHTB at partition start (payload must stay < 1 MiB)
python3 -I "$B/lib/check-dhtb.py" "$OUT/uboot/uboot_dhtb.img" - "$UBOOT_MAX_PAYLOAD"
sudo dd if="$OUT/uboot/uboot_dhtb.img" of="${LOOP}p1" bs=512 conv=fsync status=none
# p2 / p3
sudo mkfs.vfat -F 32 -n BOOTFS "${LOOP}p2" >/dev/null
sudo mkfs.ext4 -q -F -L rootfs -E lazy_itable_init=0 "${LOOP}p3"
sudo mount "${LOOP}p3" "$M"
say "unpacking rootfs"
zstd -dc "$OUT/rootfs/rootfs.tar.zst" | sudo bsdtar --xattrs --acls -xpf - --numeric-owner -C "$M"
sudo mkdir -p "$M/boot"; sudo mount "${LOOP}p2" "$M/boot"
KREL="$(cat "$OUT/kernel/release")"
say "kernel $KREL: modules, Image, dtb"
sudo rsync -a --chown=0:0 "$OUT/kernel/modules/lib/modules/" "$M/usr/lib/modules/"
sudo depmod -b "$M/usr" "$KREL"
sudo install -D -m644 "$OUT/kernel/Image" "$M/boot/Image"
sudo install -D -m644 "$OUT/kernel/ums512-rg-rotate.dtb" "$M/boot/dtb/sprd/ums512-rg-rotate.dtb"
# firmware-initrd stub: dev/console + pm_bootcode only; first boot replaces it with the unit's real firmware
STUB="$WORK/initrd-stub"; rm -rf "$STUB"; mkdir -p "$STUB/lib/firmware/sprd"
cp "$OUT/uboot/pm_bootcode.bin" "$STUB/lib/firmware/sprd/"
cat > "$WORK/initrd.list" <<LIST
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
dir /lib 0755 0 0
dir /lib/firmware 0755 0 0
dir /lib/firmware/sprd 0755 0 0
file /lib/firmware/sprd/pm_bootcode.bin $STUB/lib/firmware/sprd/pm_bootcode.bin 0644 0 0
LIST
GIC="$WORK/kernel-build/usr/gen_init_cpio"
[[ -x "$GIC" ]] || die "$GIC missing (kernel stage builds it)"
"$GIC" -t 0 "$WORK/initrd.list" | gzip -9 -n | sudo tee "$M/boot/initrd-fw.cpio.gz" >/dev/null
sudo mkdir -p "$M/boot/extlinux"
sudo tee "$M/boot/extlinux/extlinux.conf" >/dev/null <<EXT
# Rotate OS. U-Boot (vendor BSP) scans mmc 1:2 for this file.
default rotate
timeout 1
menu title Rotate OS

label rotate
  kernel /Image
  fdt /dtb/sprd/ums512-rg-rotate.dtb
  initrd /initrd-fw.cpio.gz
  append root=PARTUUID=$P3_GUID rootwait rw console=tty0 loglevel=1 log_buf_len=8M net.ifnames=0
EXT
sync
# verify what got written
sudo test -f "$M/usr/lib/modules/$KREL/modules.dep"
sudo test -f "$M/usr/share/rotate/extract-firmware.sh"
sudo test -L "$M/etc/systemd/system/multi-user.target.wants/rotate-firstboot.service"
sudo test -L "$M/etc/systemd/system/multi-user.target.wants/sshd.service"
sudo test -L "$M/etc/systemd/system/multi-user.target.wants/iwd.service"
if sudo find "$M" -name 'agdsp.bin' -o -name 'wcnmodem.bin' -o -name 'pm_sys.bin' | grep -q .; then die "vendor blob in image"; fi
sudo df -h "$M" "$M/boot" >&2
cleanup; LOOP=""; trap - EXIT
mv "$TMPIMG" "$IMG"
sfdisk -l "$IMG" >&2
# check p1 from the file itself
python3 -I - "$IMG" "$UBOOT_PART_START_MIB" <<'PY'
import sys, subprocess
img, mib = sys.argv[1], int(sys.argv[2])
open("/dev/stdout","w")
with open(img,"rb") as f:
    f.seek(mib<<20); d=f.read(1<<20)
open(sys.argv[1]+".p1.tmp","wb").write(d)
PY
python3 -I "$B/lib/check-dhtb.py" "$IMG.p1.tmp" - "$UBOOT_MAX_PAYLOAD" && rm -f "$IMG.p1.tmp"
cp "$OUT/spl/spl_a_open.img" "$OUT/image/rotate-os.spl.img"
( cd "$OUT/image" && sha256sum rotate-os.img rotate-os.spl.img | tee SHA256SUMS )
stage_end
