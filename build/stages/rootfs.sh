#!/usr/bin/env bash
# Stage rootfs: verified ALARM aarch64 tarball -> extracted as root -> trimmed/configured inside
# systemd-nspawn (qemu-user binfmt) -> out/rootfs/rootfs.tar.zst. Kernel-independent: modules, kernel and
# firmware-initrd are added by the image stage. Needs sudo.
. "$(dirname "$0")/../lib/common.sh"
TB="$ROOT/$ALARM_TARBALL"
RD="$B/rootfs"
stage_begin rootfs "$ALARM_MD5 $(find "$RD" -type f | sort | xargs cat | sha256sum) $(file_hash "$0") $(cat "$OUT/uboot/pm_bootcode.bin" 2>/dev/null | sha256sum) $PORTMASTER_SHA256 $(cat "$OUT/.melonds.stamp" 2>/dev/null) $(cat "$OUT/.melonds-pgo.stamp" 2>/dev/null)" "$OUT/rootfs/rootfs.tar.zst"
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
sudo install -m644 "$RD/packages.list" "$R/root/packages.list"
# PortMaster: pinned release zip (sha256 in pins.env), unpacked as root into /opt/rotate/portmaster/PortMaster
PMZ="$CACHE/PortMaster-$PORTMASTER_VERSION.zip"
[[ -f "$PMZ" ]] || curl -sfL -o "$PMZ" "$PORTMASTER_URL" || die "PortMaster download failed"
[[ "$(sha256sum "$PMZ" | cut -d' ' -f1)" == "$PORTMASTER_SHA256" ]] || die "PortMaster sha256 mismatch"
sudo install -d -m755 "$R/opt/rotate/portmaster"
sudo bsdtar -xf "$PMZ" --no-same-owner -C "$R/opt/rotate/portmaster"
sudo chown -R 0:0 "$R/opt/rotate/portmaster"
[[ -f "$R/opt/rotate/portmaster/PortMaster/PortMaster.sh" ]] || die "PortMaster.sh missing after unpack"
sudo install -m644 "$R/usr/share/rotate/portmaster/control.txt" "$R/opt/rotate/portmaster/PortMaster/control.txt"
printf '%s\n' "$PORTMASTER_VERSION" | sudo tee "$R/opt/rotate/portmaster/VERSION" >/dev/null
sudo install -d -m755 "$R/roms/ports" "$R/roms/bios" "$R/var/lib/rotate"/{saves,states,screenshots,playlists,logs}
# melonDS DS core: only artifacts that exist. The PGO-use build is the live core; the -fprofile-generate build is the
# training core and is NOT loaded by the frontend (it writes .gcda files); it ships under /opt/rotate/pgo for the device-side trainer.
sudo install -d -m755 "$R/usr/lib/libretro" "$R/opt/rotate/pgo"
[[ -f "$OUT/melonds/melondsds_libretro.so" ]] && sudo install -m755 "$OUT/melonds/melondsds_libretro.so" "$R/usr/lib/libretro/"
if [[ -f "$OUT/melonds/melondsds_libretro.pgo.so" ]]; then
  sudo install -m755 "$OUT/melonds/melondsds_libretro.pgo.so" "$R/opt/rotate/pgo/"
  ${CROSS}gcc -O2 -Wall "$B/tools/dsbench.c" -ldl -o "$WORK/dsbench"   # MIT, plorpOS tools/dsbench.c; single file, dynamic
  sudo install -m755 "$WORK/dsbench" "$R/opt/rotate/pgo/dsbench"
fi
say "chroot setup (pacman -Syu etc.; needs network)"
sudo systemd-nspawn -q --register=no --resolv-conf=replace-host -D "$R" /bin/bash /root/chroot-setup.sh
sudo rm -f "$R/root/chroot-setup.sh"
sudo cp "$R/root/retroarch-features.txt" "$OUT/rootfs/retroarch-features.txt" 2>/dev/null || true
sudo rm -f "$R/root/packages.list" "$R/root/retroarch-features.txt"
# rootfs must contain no vendor blobs
if sudo find "$R" -name 'agdsp.bin' -o -name 'wcnmodem.bin' -o -name 'pm_sys.bin' | grep -q .; then die "vendor blob found in rootfs"; fi
say "packing"
sudo bsdtar --xattrs --acls -cpf - --numeric-owner -C "$R" . | zstd -T0 -3 -q -f -o "$OUT/rootfs/rootfs.tar.zst"
sudo du -sm "$R" | awk '{print "rootfs unpacked MiB:",$1}' >&2
sudo chroot "$R" /usr/bin/pacman -Q 2>/dev/null | sort > "$OUT/rootfs/packages.txt" || sudo systemd-nspawn -q --register=no -D "$R" /usr/bin/pacman -Q > "$OUT/rootfs/packages.txt"
( cd "$OUT/rootfs" && sha256sum rootfs.tar.zst packages.txt | tee SHA256SUMS )
stage_end
