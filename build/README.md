# Rotate OS image build

Produces a flashable SD image (GPT: `uboot` / `bootfs` / `rootfs`) plus the one-time eMMC SPL image.
Host: Ubuntu 26.04 x86-64, GCC 15.2 cross toolchain, qemu-user binfmt (see `../docs/HOST-SETUP.md`).
Nothing here writes to a real block device: the image is a regular file attached with `losetup`, and the image
stage refuses any device that is not `/dev/loop*` backed by its own `.part` file. Nothing was flashed.

## Commands

```sh
cd /home/dakoda/Projects/rotate-os/build
./build.sh              # all stages: spl uboot kernel rootfs image
./build.sh kernel       # one stage (or several: ./build.sh uboot image)
FORCE=1 ./build.sh uboot    # ignore the cache stamp
JOBS=16 ./build.sh ...      # default 16; CROSS_COMPILE=aarch64-linux-gnu- by default
./build.sh -h
```

Exit codes: 0 ok; 2 unknown stage/usage; 3 missing host tool; otherwise the failing stage's exit status
(stage failure prints `stage X FAILED, see build/logs/X.log`). The `rootfs` and `image` stages call `sudo`
(extract as root, nspawn, loop device, mount). Network is needed once by `rootfs` (`pacman -Syu`) and by `uboot`
if the bare git cache `cache/u-boot-ums512.git` is missing.

Layout: `pins.env` (all pinned SHAs and sizes), `stages/*.sh`, `lib/` (helpers, DHTB wrap/check),
`patches/{spl,uboot}/`, `kernel/` (config fragments), `rootfs/` (overlay + chroot script), `out/<stage>/`
(artifacts + `SHA256SUMS`), `logs/<stage>.log` (last real build of that stage) and `logs/summary.log`
(every invocation incl. failures), `work/` and `cache/` (scratch, ccache; safe to delete).
Idempotence: each stage writes `out/.<stage>.stamp` = hash of pins + patches + fragments + script; a matching
stamp with existing outputs makes the stage skip (verified: second full run, all five skipped in 0 s).
Sources are exported with `git archive` of the pinned SHA into `work/`; `upstream/` is never modified.
Compilers go through ccache (symlink masquerade in `work/ccache-bin`, `CCACHE_DIR=cache/ccache`).

## Stage results (this host, 2026-10-31)

| Stage | Result | Time | Artifact | Size | sha256 |
|---|---|---|---|---|---|
| spl | PASS (2nd try) | 16 s | `out/spl/spl_a_open.img` (DHTB OK, payload 54632 B) | 4,194,304 | `ca641d90…1fd1e8dd3` |
| uboot | PASS (2nd try) | 11 s | `out/uboot/uboot_dhtb.img` (DHTB OK, payload 571,904 B < 1 MiB) | 572,416 | `df3b67d6ac0249eb3d78f9eefbb5fdcbb02c0ba74efed71c158c3c23a3cf6c89` |
| kernel | PASS | 541 s (cold ccache) | `out/kernel/Image`, `ums512-rg-rotate.dtb`, modules (7.8 MB), `config` | Image 31,447,552 | Image `456c71f6…946ab` |
| rootfs | PASS | 344 s | `out/rootfs/rootfs.tar.zst` (694 MiB unpacked, 148 packages) | 195,154,406 | `2db449d6…ee24aa` |
| image | PASS (2nd try) | 8 s | `out/image/rotate-os.img` (sparse 6 GiB, 897 MiB used) + `rotate-os.spl.img` | 6,442,450,944 | img `e8663e77b068597d41bd443ba1dde5ea62a2558f00593275f3947a172e326570` |

Full hashes: `out/*/SHA256SUMS`. Kernel release string: `7.2.9-sprd-ums512+` (the `+` comes from kbuild's
git-dirty detection on the shared checkout; contents are the pinned commit, tracked tree clean).

Failures hit and fixed (logs of the failed runs were overwritten by the passing runs; the history is in `logs/summary.log`):
1. **spl** exit 1: GCC 15 defaults to C23 where `bool` is a keyword (`chipram/include/sci_types.h:26 typedef bool`).
   Fix: `patches/spl/0001-config.mk-force-std-gnu11-for-GCC15.patch` (adds `-std=gnu11` to chipram `CFLAGS`), applied to the work copy.
2. **uboot** exit 2: same C23 problem in `include/crypto/sprdrsa.h` and `fs/f2fs-tools/include/f2fs_fs.h`.
   Fix without source patch: `make ARCH=arm … KCFLAGS=-std=gnu11` (U-Boot appends `KCFLAGS` to `KBUILD_CFLAGS`, Makefile:623).
   Side effect checked: the Makefile's own `KCFLAGS += -DAVB_…` is under `CONFIG_VBOOT_V2`, which is not set in `ums512_rg_rotate_defconfig`, so nothing is lost.
   The kernel did not need this (Kbuild already passes `-std=gnu11`).
3. **image** exit 1: my post-check used `test -e` on absolute enable-symlinks, which dangle from the host. Changed to `test -L`.

### U-Boot recipe as built
`crackerjacques/u-boot-ums512 @ d1f91089bcb2…` + the five Armbian patches (`patches/uboot/0001..0005`, from
armbian/build `6085c92674bd`), `ARCH=arm` on the defconfig and the build pass, `ums512_rg_rotate_defconfig`,
`DEVICE_TREE=ums512_rg_rotate u-boot-dtb.bin`, wrapped by `lib/dhtb-wrap.py` (base618, MIT; identical format to Armbian's).
`pm_bootcode.bin` (124 B, CM4 boot stub) is generated from the GPL `cp_boot.h` with base618's `build-pm-bootcode.sh`.

### Kernel recipe as built
`upstream/linux-mainline-sprd` @ `fedeb60b65…` (checked, tracked tree clean), `O=work/kernel-build`,
`ums512_defconfig` + `kernel/rotate.config` (`CONFIG_ANDROID_BINDERFS=y`) + `kernel/fragments.d/*.config`.
The stage fails if any requested symbol does not land in the final `.config`; result in `out/kernel/binder.txt`:
`CONFIG_ANDROID_BINDER_IPC=y`, `CONFIG_ANDROID_BINDERFS=y`.
**Trim hook:** put the trim lane's fragment at `build/kernel/fragments.d/50-trim.config` (plain `CONFIG_X=…` lines);
the directory is hashed into the stamp so the kernel rebuilds. Empty today, so the shipped kernel is untrimmed.

### Rootfs recipe as built
ALARM tarball (md5 checked against `pins.env`; GPG `.sig` NOT verified) → `bsdtar -xpf` as root → `systemd-nspawn`
with qemu-user binfmt runs `rootfs/chroot-setup.sh`: `pacman -Syu`, removes the generic `linux-aarch64`, all
`linux-firmware*`, vim, netctl, gpm, mkinitcpio, pciutils, net-tools, dhcpcd; installs `iwd cpio wireless-regdb`
(`IgnorePkg = linux-aarch64 linux-aarch64-headers` so pacman cannot bring the generic kernel back).
Enabled: sshd (root login off), iwd, systemd-networkd/resolved/timesyncd, `serial-getty@ttyGS0`, `rotate-firstboot`,
`rotate-resize`, `rotate-keyring`.
- **iwd rather than NetworkManager:** iwd does association and DHCP itself (`EnableNetworkConfiguration=true`), a single small daemon and no dbus/glib/polkit/ModemManager stack, which fits "minimal" and the battery priority. Cost: no GUI/NM tooling; Wi-Fi is configured with `iwctl`. Wi-Fi itself cannot work until firmware extraction has run on the device.
- **Serial console:** the kernel has `CONFIG_USB_G_SERIAL=y`, so `ttyGS0` exists when the gadget binds; the getty drop-in is the Armbian-equivalent non-blocking override (written fresh). Host sees `/dev/ttyACM0`.
- **First-boot firmware:** `/usr/share/rotate/extract-firmware.sh` and `make-initrd-fw.sh` are verbatim copies of thwonp/base618 `scripts/` (SPDX MIT; source commit `a1a1801f7daf…`); `firstboot.sh` / `rotate-firstboot.service` are adapted from `base618-firstboot.*` (MIT; header says so; notice in `/usr/share/licenses/rotate/`). It reads `l_agdsp_*`, `pm_sys_*`, `wcnmodem_*` from the unit's own eMMC, writes `/boot/initrd-fw.cpio.gz`, then powers off.
- No vendor blobs: the rootfs and image stages `find` for `agdsp.bin`, `pm_sys.bin`, `wcnmodem.bin` and fail if present (none found). `linux-firmware*` was removed.
- Default accounts are the ALARM defaults (`alarm`/`alarm`, `root`/`root`). **Change them on first login.** sshd is enabled but no network is configured out of the box.
- The pacman keyring generated at build time is deleted; `rotate-keyring.service` regenerates it on first boot.

### Image layout (checked from the finished file)
`sfdisk -d`: p1 `uboot` 2048..34815 (16 MiB, type 8DA63339-…), p2 `bootfs` FAT32 256 MiB, p3 `rootfs` ext4 (fixed
PARTUUID `7a1b2c3d-0003-4000-8000-526f7461744f`, used in `root=PARTUUID=`). p1 holds `uboot_dhtb.img` at its first
byte: read back from the file at 1 MiB, `check-dhtb.py` passes. `fsck.vfat -n` and `e2fsck -fn` clean.
`bootfs`: `Image`, `dtb/sprd/ums512-rg-rotate.dtb`, `initrd-fw.cpio.gz` (stub: `/dev/console` + `pm_bootcode.bin`),
`extlinux/extlinux.conf` (`kernel /Image`, `append root=PARTUUID=… rootwait rw console=tty0 loglevel=1 log_buf_len=8M net.ifnames=0`).
`systemd-analyze verify` of the three rotate units only printed warnings about newer-systemd keys in stock units (host systemd is older).

## Flashing (for Dakoda; not performed)
1. SPL (once, eMMC boot area): `out/image/rotate-os.spl.img` — follow the Armbian sequence in `docs/research/01-boot-kernel.md` section 3 (back up `splloader` first, write `splloader`, cold power cycle, then `splloader_bak`). Needs the vendor FDL files; which physical button enters `1782:4d00` is unresolved (Armbian vs beebono disagree).
2. SD: write `out/image/rotate-os.img` to a card (≥ 6 GiB) with a tool you trust, e.g. `sudo dd if=rotate-os.img of=/dev/sdX bs=4M conv=fsync status=progress` after confirming the device node. First boot powers itself off after extracting firmware; power on again.

## Not verified / known gaps
- **Nothing was booted.** No hardware attached; qemu cannot emulate the T618 boot chain. U-Boot loading `Image` with `booti` at `kernel_addr_r=0x80080000` (ums512_rg_rotate.h:249) is read from the header, not run. base618 uses a `Image.uimg` and a different U-Boot (thwonp `plorpos` branch with 64 MiB gunzip limit); we use the pinned crackerjacques U-Boot with a raw `Image` as Armbian does. If U-Boot refuses it, fall back to base618's `make-boot-files.sh` approach.
- `rotate-resize.service` (grows p3 to the card, keeps the PARTUUID) and `rotate-keyring.service`/`rotate-firstboot` logic were written but not executed (needs the device or a booted guest). `extract-firmware.sh` was not run (no eMMC images available).
- Rootfs was not booted under qemu-system; only inspected (mounted read-only, file/symlink checks, fsck).
- ALARM tarball GPG signature not verified (no key imported); md5 only. Package versions float with the mirror at build time, so the rootfs stage is not bit-reproducible (`packages.txt` records the result).
- No hw-watchdog feeder service yet (research 02: userspace must feed `/dev/watchdog0/1`), no audio/UCM, no Mesa/RetroArch/frontend: later phases. logind lid handling is set to ignore.
- Kernel is untrimmed (hook above). Windows/macOS hosts are not supported by these scripts.
