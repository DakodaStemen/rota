#!/usr/bin/env bash
# Stage rootfs: verified ALARM aarch64 tarball -> extracted as root -> trimmed/configured inside
# systemd-nspawn (qemu-user binfmt) -> out/rootfs/rootfs.tar.zst. Kernel-independent: modules, kernel and
# firmware-initrd are added by the image stage. Needs sudo.
. "$(dirname "$0")/../lib/common.sh"
TB="$ROOT/$ALARM_TARBALL"
RD="$B/rootfs"
stage_begin rootfs "$ALARM_MD5 $(find "$RD" -type f | sort | xargs cat | sha256sum) $(file_hash "$0") $(cat "$OUT/uboot/pm_bootcode.bin" 2>/dev/null | sha256sum)" "$OUT/rootfs/rootfs.tar.zst"
[[ -f "$OUT/uboot/pm_bootcode.bin" ]] || die "run the uboot stage first (pm_bootcode.bin)"
[[ -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]] || die "qemu-aarch64 binfmt not registered"
got="$(md5sum "$TB" | cut -d' ' -f1)"; [[ "$got" == "$ALARM_MD5" ]] || die "tarball md5 $got != pinned $ALARM_MD5"
R="$WORK/rootfs"
sudo umount -R "$R" 2>/dev/null || true
sudo rm -rf "$R"; mkdir -p "$R" "$OUT/rootfs"
say "extracting ALARM tarball (root, -p)"
sudo bsdtar -xpf "$TB" --numeric-owner -C "$R"
# overlay (owned root:root) + MIT-licensed base618 scripts, verbatim from upstream/
sudo rsync -a --chown=0:0 "$RD/overlay/" "$R/"
BS="$ROOT/upstream/base618"
sudo install -d -m755 "$R/usr/share/rotate" "$R/usr/share/licenses/rotate" "$R/usr/lib/firmware/sprd"
sudo install -m755 "$BS/scripts/extract-firmware.sh" "$BS/scripts/make-initrd-fw.sh" "$R/usr/share/rotate/"
sudo install -m755 "$RD/overlay/usr/share/rotate/firstboot.sh" "$RD/overlay/usr/share/rotate/resize-root.sh" "$R/usr/share/rotate/"
sudo install -m644 "$BS/LICENSES/MIT.txt" "$R/usr/share/licenses/rotate/base618-MIT.txt"
sudo install -m644 "$RD/NOTICE-base618.txt" "$R/usr/share/licenses/rotate/NOTICE-base618.txt"
sudo install -m644 "$OUT/uboot/pm_bootcode.bin" "$R/usr/lib/firmware/sprd/pm_bootcode.bin"   # GPL-source-built stub, not a vendor blob
sudo install -m755 "$RD/chroot-setup.sh" "$R/root/chroot-setup.sh"
say "chroot setup (pacman -Syu etc.; needs network)"
sudo systemd-nspawn -q --register=no --resolv-conf=replace-host -D "$R" /bin/bash /root/chroot-setup.sh
sudo rm -f "$R/root/chroot-setup.sh"
# rootfs must contain no vendor blobs
if sudo find "$R" -name 'agdsp.bin' -o -name 'wcnmodem.bin' -o -name 'pm_sys.bin' | grep -q .; then die "vendor blob found in rootfs"; fi
say "packing"
sudo bsdtar --xattrs --acls -cpf - --numeric-owner -C "$R" . | zstd -T0 -3 -q -f -o "$OUT/rootfs/rootfs.tar.zst"
sudo du -sm "$R" | awk '{print "rootfs unpacked MiB:",$1}' >&2
sudo chroot "$R" /usr/bin/pacman -Q 2>/dev/null | sort > "$OUT/rootfs/packages.txt" || sudo systemd-nspawn -q --register=no -D "$R" /usr/bin/pacman -Q > "$OUT/rootfs/packages.txt"
( cd "$OUT/rootfs" && sha256sum rootfs.tar.zst packages.txt | tee SHA256SUMS )
stage_end
