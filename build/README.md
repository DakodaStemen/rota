# Rotate OS image build

Produces a flashable SD image (GPT: `uboot` / `bootfs` / `rootfs`) plus the one-time eMMC SPL image.
Host: Ubuntu 26.04 x86-64, GCC 15.2 cross toolchain, qemu-user binfmt (see `../docs/HOST-SETUP.md`).
Nothing here writes to a real block device: the image is a regular file attached with `losetup`, and the image
stage refuses any device that is not `/dev/loop*` backed by its own `.part` file. Nothing was flashed.

## Commands

```sh
cd /home/dakoda/Projects/rotate-os/build
./build.sh              # all stages: spl uboot kernel melonds rootfs image
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
`patches/{spl,uboot,kernel}/`, `kernel/` (config fragments), `rootfs/` (overlay + chroot script), `out/<stage>/`
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
| kernel | PASS (trimmed, 2026-10-31 re-run) | 358 s build.sh / 5m58 wall (-j16, ccache cold for the new source path; was 541 s untrimmed) | `out/kernel/Image`, `ums512-rg-rotate.dtb`, modules (1.3 MB, 17 .ko), `config` | Image 20,170,760 (was 31,447,552) | Image `e6f6b693…026ee` |
| rootfs | PASS (superseded by the Phase 6 row below) | 344 s | `out/rootfs/rootfs.tar.zst` (694 MiB unpacked, 148 packages) | 195,154,406 | `2db449d6…ee24aa` |
| melonds | PASS (`-fprofile-generate`) | 1225 s wall = chroot creation + 125 s cmake configure (FetchContent clones) + 781 s compile/link at `-j16` under qemu-user | `out/melonds/melondsds_libretro.pgo.so` | 10,251,072 | `f9625592…2cf15` |
| rootfs | PASS (Phase 6: RetroArch/cores/PortMaster) | 284 s (incl. pacman of 186 extra packages, qemu-user) | `out/rootfs/rootfs.tar.zst` (**1675 MiB unpacked, 334 packages**; was 694 MiB / 148) | 492,008,541 | `82fe7f66…d4441` |
| image | PASS (Phase 6 rebuild) | 11 s | `out/image/rotate-os.img` (sparse 6 GiB, p3 5.7 GiB) | 6,442,450,944 | img `7abccb38e8ce79ee90c4e4b41b221b9c2c3a1c6777e4551ac580538afcd9e7bf` |
| image | PASS (rebuilt after trim + watchdog) | 8 s (rootfs re-run 179 s for the overlay change) | `out/image/rotate-os.img` (sparse 6 GiB) + `rotate-os.spl.img` | 6,442,450,944 | img `d7684959afd86c3fcfb6c92987da55c06febf389bddbbe446c9f0cebbfd9ef22` |

Full hashes: `out/*/SHA256SUMS`. Kernel release string: `7.2.9-sprd-ums512` (the build now uses a `git archive` work copy, so no `+` dirty marker; it was `…+` before).

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
`git archive` of `upstream/linux-mainline-sprd` @ `fedeb60b65…` into `work/kernel-src`, then `patches/kernel/*.patch` applied
there (`upstream/` is never edited; the stage still checks it is at the pin and clean), `O=work/kernel-build`,
`ums512_defconfig` + `kernel/rotate.config` + `kernel/fragments.d/*.config` (merge_config.sh). Patch files are part of the stamp hash.
The stage fails if any requested symbol does not land in the final `.config`.
- `patches/kernel/0001-drm-sprd-select-DRM_CLIENT_SELECTION.patch`: `select DRM_CLIENT_SELECTION` in `config DRM_SPRD`. With it the
  `DRM_HDLCD=y` keep-alive was dropped (now `# CONFIG_DRM_HDLCD is not set`); `DRM_CLIENT_SELECTION`, `DRM_FBDEV_EMULATION`, `FRAMEBUFFER_CONSOLE` stay `=y`.
- `kernel/fragments.d/20-trim.config` = `docs/kernel-trim.config` (copied; only change: HDLCD line). `CONFIG_DMABUF_HEAPS=y` (+SYSTEM, CMA) lives
  in this fragment rather than in a defconfig patch. `kernel/rotate.config` (binderfs) is unchanged.

#### Trim result (before = untrimmed build of 06:06, after = 06:18)
| | before | after |
|---|---|---|
| `Image` | 31,447,552 B | 20,170,760 B (-35.9 %) |
| modules (`.ko`) | 156 (7.8 MB) | 17 (1.3 MB) |
| `=y` / `=m` in `.config` | 2035 / 155 | 1488 / 18 |
| kernel stage time | 541 s | 358 s |
| image sha256 | `e8663e77…2326570` | `d7684959afd86c3fcfb6c92987da55c06febf389bddbbe446c9f0cebbfd9ef22` |

Key symbols (before -> after): `DRM_SPRD` y->y, `DRM_CLIENT_SELECTION` y->y, `DRM_FBDEV_EMULATION` y->y, `FRAMEBUFFER_CONSOLE` y->y,
`DRM_HDLCD` y->n, `DMABUF_HEAPS` n->y (+`_SYSTEM`, `_CMA` now stick), `INPUT_UINPUT` n->y, `ANDROID_BINDERFS` y->y, `SPRD_WATCHDOG`/`SC27XX_WATCHDOG` y->y,
`PREEMPT_LAZY`/`PREEMPT_DYNAMIC` n->y, `DRM_PANFROST` m->m, `SND_SOC_SPRD` y->y, `SND_SOC_UNISOC_VBC_V4_SC2730` y->y, `SND_SOC_AW87390` y->y,
`SND_SOC_GENERIC_DMAENGINE_PCM` y->gone, `SND_SOC_UMS9230_DIGITAL` y->n.
Audio check: all 22 `sound/soc/sprd/vendor/*.o` (incl. `vbc-dai`, `sprd-codec`, `sprd-dmaengine-pcm`, `vbc-rxpx-codec-sc27xx`) still compile and
`snd-soc-unisoc-vbc-v4-sc2730` is in `modules.builtin`. The two dropped symbols were NOT restored: the DTS codec node uses
`unisoc,audio-codec-dig-agcp` (vendor stack), `UMS9230_DIGITAL` only matches `sprd,ums9230-digital-codec`, and nothing under `sound/soc/sprd` references `snd_dmaengine_*`
(the vendor `sprd-dmaengine-pcm.c` is self-contained; link succeeded). Not runtime-tested: audio needs the AGDSP firmware and hardware. If audio fails on device, first re-add
`CONFIG_SND_SOC_GENERIC_DMAENGINE_PCM=y` and `CONFIG_SND_SOC_UMS9230_DIGITAL=y` to the fragment (cheap).
`sound/soc/sprd` objects built; the kernel log has `-Wmissing-prototypes`/`const` warnings in `vendor/agdsp_access.c`, `audio_mem.c` (pre-existing vendor code).

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
- Hardware watchdog: `rootfs/overlay/etc/systemd/system.conf.d/10-rotate-watchdog.conf` sets `RuntimeWatchdogSec=20s`, `RebootWatchdogSec=2min` (`SPRD_WATCHDOG=y`, DT `timeout-sec=30`, driver range 3-60 s; the SoC watchdog only counts once userspace opens it, so PID 1 arms it). File verified inside the built image; **not run on hardware**. There are two watchdog drivers (`sprd_wdt`, `sc27xx_wdt`); systemd opens `/dev/watchdog`, which is whichever registered as watchdog0 - check `cat /sys/class/watchdog/watchdog0/identity` on device and set `RuntimeWatchdogDevice=` if it is the PMIC one. No audio/UCM, no Mesa/RetroArch/frontend: later phases. logind lid handling is set to ignore.
- Trimmed kernel only builds and links; it has never booted. Windows/macOS hosts are not supported by these scripts.

## Phase 6 prep: RetroArch on KMS, cores, melonDS PGO, PortMaster

Everything below was built/inspected on this host (2026-10-31). Nothing ran on the device: items marked UNVERIFIED need it.

### RetroArch config (`rootfs/overlay/etc/retroarch.cfg`, `etc/asound.conf`, `usr/share/rotate/autoconfig/udev/gpio-keys-gamepad.cfg`)
- **`video_driver = "gl"`, `video_context_driver = "kms"`.** `glcore` is not usable: it needs desktop GL 3.2 core and Mesa documents Panfrost G52 as
  OpenGL 3.1 / OpenGL ES 3.1 (https://docs.mesa3d.org/drivers/panfrost.html). Arch's `retroarch 1.22.2-5` reports OpenGL, OpenGLES, EGL and KMS as built
  (`out/rootfs/retroarch-features.txt`), so `gl` + `kms` is available; whether `gl` picks GLES or desktop GL at runtime is UNVERIFIED (needs the GPU).
- No compositor, `video_threaded = false`, `video_vsync = true`, `video_max_swapchain_images = 3` (2 = less latency, more dropped frames), hard sync off,
  720x720 fullscreen, core aspect kept. ALSA direct (no pipewire), `audio_latency = 64` (lower is UNVERIFIED on this codec).
- `network_cmd_enable = true`, `network_cmd_port = 55355`; `savestate_auto_save` / `savestate_auto_load = true`.
  **Checked for real** in the built rootfs under qemu-user: with this config RetroArch logs `[NetCMD] bringing_up_command_interface_at_port 55355.` and answers UDP
  `VERSION` -> `1.22.2`, `GET_STATUS` -> `GET_STATUS CONTENTLESS` on 127.0.0.1. (The frontend's `RetroArchCtl` uses IPv4 UDP: `frontend/src/retroarch.cpp` L67-L87.)
- **Found by that test:** Arch's RetroArch has the Qt desktop UI and aborts without a display ("could not load the Qt platform plugin"), so the config sets
  `ui_companion_enable`, `ui_companion_start_on_boot`, `desktop_menu_enable` to false.
- Input autoconfig for the RG Rotate pad: device name `gpio-keys-gamepad` comes from the DT node (`ums512-rg-rotate.dts` L213; `gpio_keys.c` L886 falls back to the
  platform-device name); plorpOS's SDL GUID `19007b94…` contains CRC16("gpio-keys-gamepad") = 0x947b, which I recomputed. **Evdev name and button numbering are UNVERIFIED on a running device.**
  The pad has no analog sticks: only `BTN_THUMBR`.
- Sources: MIROKI `system/rootfs/etc/retroarch.cfg` L4-L38 and `etc/asound.conf` (no licence; knobs read, text re-written), `tools/build_retroarch.sh` L40-L60.
  ALSA is pinned to 48 kHz mono S16 on `hw:0,0` following MIROKI's note that the AGDSP/VBC path is one mono speaker; card/device index and mono pin UNVERIFIED on our kernel.

### Cores (A75x2 + A55x6, Mali-G52; ALARM aarch64 repos queried 2026-10-31: `build/work/pkgdb/{core,extra,alarm,aur}.db`)
| System | Core installed | Why / not chosen |
|---|---|---|
| GB / GBC / GBA | `libretro-mgba` (`mgba`) | accurate and light; `gambatte`/`sameboy` available but unnecessary |
| NES | `libretro-nestopia` | `fceumm` not packaged |
| SNES | `libretro-snes9x` | fast on A55; `bsnes`/`bsnes-hd` are in the repo, accuracy cores want far more CPU than the 2 big cores can spare |
| MD/SMS/GG | `libretro-genesis-plus-gx` | `picodrive`/`blastem` also in repo |
| PC Engine | `libretro-beetle-pce-fast` | |
| N64 | `libretro-mupen64plus-next` | GLES3 path suits Panfrost; `parallel-n64` is in the repo, older. Performance UNVERIFIED |
| PS1 | `libretro-pcsx-rearmed` | ARM dynarec + NEON; `beetle-psx`/`duckstation`/`mednafen-psx` are heavier |
| Dreamcast | `libretro-flycast` | will be heavy on a G52; UNVERIFIED |
| NDS | `libretro-melonds` now, PGO `melondsds` later | `desmume` also packaged |
| PSP | **none installed** | no `libretro-ppsspp` in ALARM; only the standalone `ppsspp 1.20.4` package (not tested). Follow-up |
`frontend/config/frontend.ini` was reconciled: every `core =` resolves to a file in the built rootfs (`usr/lib/libretro/<core>_libretro.so`; all 13 systems checked
against `rootfs.tar.zst`, 0 missing). Added NES, SMS, GG, PCE, Dreamcast systems; NDS stays on `melonds` until `melondsds` is trained. The frontend test suite still passes (7/7).
ALARM packaging bug: `libretro-core-info` and `libretro-pcsx-rearmed` both ship `pcsx_rearmed_libretro.info`; the rootfs setup allows that one overwrite.

**Cost of using Arch's `retroarch`:** it depends on qt6-base, ffmpeg, pipewire/pulse, X11 libs. Rootfs went from 148 packages / 694 MiB to 334 packages / 1675 MiB. A trimmed RetroArch
built with MIROKI's flag list (as a reference) would remove most of that; not done here (child-card candidate).

### melonDS PGO (`stages/melonds.sh`, `stages/melonds-pgo.sh`, `patches/melonds/`, `tools/dsbench.c`)
- Recipe = thwonp/plorpOS `mk/build-melondsds.sh` (sources JesseTG/melonds-ds `f394adba` + melonDS `16f127db`, two plorpOS patches copied verbatim into `patches/melonds/`, MIT).
  Flags: `-mcpu=cortex-a75.cortex-a55`, `-DENABLE_OPENGL=OFF -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON`, Release.
- **Built natively in an aarch64 ALARM chroot under qemu-user, not cross-compiled**, because a `.gcda` profile is only valid for the GCC that instrumented it (here GCC 16.1.1) and the
  same chroot compiler does the `-fprofile-use` pass. Compile+link 781 s at `-j16` (host: 16 cores); whole stage 1225 s including chroot creation (cached in `work/melonds-chroot`) and a 125 s configure.
- `./build.sh melonds` -> `out/melonds/melondsds_libretro.pgo.so` (`-fprofile-generate -fprofile-update=atomic`). Checked: aarch64 ELF, 15 `gcov` symbols, 20 CRC32 instructions (plorpOS patch active),
  no undefined unversioned symbols, needs `GLIBC_2.43` (matches the ALARM rootfs, NOT Debian), and it `dlopen`s and initialises under qemu-user via `dsbench` (it then rejects a dummy ROM; no real ROM was available, so no frame was emulated and no `.gcda` was produced here).
- **Device step (for Dakoda):** on the RG Rotate run `/usr/lib/rotate/ds-pgo-train.sh /roms/nds/<your .nds files>` (plorpOS trained on Pokemon Pearl, Metroid Prime Hunters, Advance Wars; ~6 min).
  It leaves `/var/lib/rotate/pgo-profile/build/out/build/**.gcda`. Then on the host:
  `tar czf melonds-profile.tgz -C <that dir> . && MELONDS_PROFILE=melonds-profile.tgz build/build.sh melonds-pgo rootfs image` -> `out/melonds/melondsds_libretro.so` installed as the live core;
  then set `core = melondsds` for `[system.nds]`. `melonds-pgo` fails fast without `MELONDS_PROFILE`. The `-fprofile-use` pass is UNRUN (no profile exists).
- The instrumented core, `dsbench` (cross-built, MIT) and the trainer are in the image under `/opt/rotate/pgo` and `/usr/lib/rotate/ds-pgo-train.sh`. Do not use the instrumented core for play.

### PortMaster (`pins.env`, `stage rootfs`, `rootfs/overlay/usr/share/rotate/portmaster/control.txt`, `usr/lib/rotate/rotate-pm-run`)
- Pinned **PortMaster-GUI 2026.10.29-1355**, asset `PortMaster.zip`, sha256 `6261911f…ed9643` (matches GitHub's asset digest and the release's `.md5`), cached in `cache/`, unpacked to `/opt/rotate/portmaster/PortMaster`.
  Not the AUR `portmaster-bin` (that is the unrelated Safing firewall).
- Runtime deps installed (from the PortMaster CFW requirements gist, https://gist.github.com/bmdhacks/f26dbb60dc0ff0b9bc8e043fac8b813b): bash, coreutils, python (lzma import checked in the rootfs),
  squashfs-tools (`mksquashfs 4.7.5`), unzip, zip, rsync, xdelta3, dialog, 7zip, sdl2-compat + image/mixer/ttf. Kernel: `SQUASHFS`(+zlib/xz/zstd/lz4), `BLK_DEV_LOOP`, `INPUT_UINPUT`, `ZRAM` are `=y` in `out/kernel/config`.
- `control.txt` (own file, structure per PortMaster's contract; the SDL-GUID one-line gamecontrollerdb trick from plorpOS `portmaster/control.txt`): `DEVICE_NAME="RG Rotate"` (also `/etc/device_model`),
  `ANALOGSTICKS=0` (no sticks), `LOWRES=N`, 720x720, `CFW_NAME=ROCKNIX` for ports (mainline + Panfrost + SDL2 KMSDRM = ROCKNIX's environment; same as plorpOS), ports in `/roms/ports`.
  `rotate-pm-run` restores it before every run (a PortMaster self-update restores the stock one) and keeps the CFW name for scripts that re-source `device_info.txt`.
  **Checked:** `source control.txt; get_controls` in the built rootfs under qemu prints `CFW=ROCKNIX DEV=RG Rotate param=rgrotate sticks=0 arch=aarch64 dir=roms` and the expected one-line controller DB.
  No port was run (no device, no ports); first-run PortMaster GUI behaviour, `pylibs.zip` unpack (a `cp ... pylibs/resources/*.ttf` error appears before first run) and gptokeyb are UNVERIFIED.
- plorpOS's `chdread.c/.h` (PolyForm Noncommercial) were not used; the image contains no `chdread` file.
