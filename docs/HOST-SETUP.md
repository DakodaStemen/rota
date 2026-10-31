# Rotate OS build host setup

Host: Ubuntu 26.04, kernel 7.0.0-38-generic, installed 2026-10-31 via `apt-get install` (apt repo candidates checked with `apt-cache policy`).

## Installed packages (dpkg-query)

| Package | Version | Notes |
|---|---|---|
| qemu-user | 1:10.2.1+ds-1ubuntu3.3 | provides `/usr/bin/qemu-aarch64` (static-pie x86-64 ELF) |
| qemu-user-binfmt | 1:10.2.1+ds-1ubuntu3.3 | registers binfmt_misc handlers |
| binfmt-support | 2.2.2-8 | |
| qemu-system-arm | 1:10.2.1+ds-1ubuntu3.3 | `qemu-system-aarch64` 10.2.1 |
| qemu-efi-aarch64 | 2025.11-3ubuntu7.3 | firmware in `/usr/share/AAVMF/` (AAVMF_CODE.fd, AAVMF_VARS.fd) |
| gcc-aarch64-linux-gnu | 4:15.2.0-5ubuntu1 | `aarch64-linux-gnu-gcc` 15.2.0 |
| u-boot-tools | 2025.10-0ubuntu2.2 | `mkimage` 2025.10 |
| device-tree-compiler | 1.7.2-2ubuntu1 | dtc 1.7.2 |
| bc | 1.07.1-4build1 | |
| bison | 2:3.8.2+dfsg-1ubuntu0.26.04.1 | |
| flex | 2.6.4-8.2build2 | |
| libssl-dev | 3.5.5-1ubuntu3.7 | |
| libelf-dev | 0.194-4 | |
| arch-install-scripts | 31-1 | pacstrap, genfstab, arch-chroot present |
| systemd-container | 259.9-0ubuntu0.1 | systemd-nspawn (systemd 259) |
| dosfstools | 4.2-1.2build1 | |
| e2fsprogs | 1.47.2-3ubuntu4 | |
| kpartx | 0.14.3-2ubuntu1 | |
| ccache | 4.12.3-1 | |

(git and make were also requested from apt; already satisfied or installed.)

### Deviation: qemu-user-static
`qemu-user-static` has no install candidate on Ubuntu 26.04 (`apt-cache show` returns nothing). Substitute: `qemu-user` + `qemu-user-binfmt`. The `qemu-aarch64` binary is already static-pie, so it works as a chroot interpreter. I added one symlink so the usual name resolves:
`/usr/local/bin/qemu-aarch64-static -> /usr/bin/qemu-aarch64`

### Verification output
- `qemu-aarch64-static --version` → `qemu-aarch64 version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.3)`
- `ls /proc/sys/fs/binfmt_misc/ | grep aarch64` → `qemu-aarch64`, `qemu-aarch64_be`; entry is `enabled`, interpreter `/usr/bin/qemu-aarch64`, flags `POF` (P=preserve argv0, O=open binary, F=fix binary, so chroots need no copied interpreter).
- Not run: the e2fsprogs/dosfstools `mkfs.*` version probes (blocked by the agent safety filter). Package versions come from dpkg.

Rollback: `sudo apt-get remove <pkgs>`; `sudo rm /usr/local/bin/qemu-aarch64-static`.

## Upstream clones (`/home/dakoda/Projects/rotate-os/upstream/`)

| Dir | Source | Branch | HEAD | Size on disk |
|---|---|---|---|---|
| rg-rotate-linux | github.com/beebono/rg-rotate-linux (`--recurse-submodules --shallow-submodules --depth 50`) | main | 94d2d273c20aeabec2b4c6b160e33e0c2d26dd41 | 5.0G |
| base618 | github.com/thwonp/base618 (depth 50) | main | a1a1801f7dafa649d6dddf6c307529d7ee57b93c | 2.7M |
| plorpOS | github.com/thwonp/plorpOS (depth 50) | main | f555b889f441f28d872f7e5012a6dff8da9a583e | 28M |
| linux-mainline-sprd | github.com/thwonp/linux-mainline-sprd (depth 50, `--branch rotate-fixes`) | rotate-fixes | fedeb60b65055aacc35f4231692f808748c5de31 | 2.1G |
| ums512_spl | github.com/crackerjacques/ums512_spl (depth 50) | rotate | ed5b882070e5d9ff237b91420c076bb4a03688db | 46M |
| MIROKI | github.com/beebono/MIROKI (depth 50) | main | ff83b88c514172065bace70d5f393a521a381c1f | 94M |
| GammaOSNext | github.com/TheGammaSqueeze/GammaOSNext (depth 1) | main | 3ed709a29b26625f8cd52f98e164c98acb609beb | 20M |

GammaOSNext: GitHub API reported repo size 9878 KB, far under the 5 GB cap, so it was cloned (UX reference only).

### rg-rotate-linux submodules (`git submodule status`)
Names differ from the mission brief (it says linux-mainline-sprd / u-boot-ums512 / ums512_spl):

| Path | Commit | Size |
|---|---|---|
| src/busybox | 371fe9f71d445d18be28c82a2a6d82115c8af19d (master) | 20M |
| src/linux-7-1-sprd | 464b3e7bf45bb300e190339977abba36c8078e1f | 1.8G |
| src/spl | ed5b882070e5d9ff237b91420c076bb4a03688db (rotate) | 40M |
| src/u-boot | 34afab15d051a89ebfc4d2dd1c8cda8e1ce56ea9 | 138M |
| tools/pac-extractor | 7d4e59b6a5ba86a7ea4e8c42d9c5f229037a6893 (main) | 28K |
| vendor/android-kernel-5-4-realme | a76a8d81db82bf3c338f49647c00d0cdfcb25cf5 (master) | 1.2G |
| vendor/bootloader-unlock | 3a7f452a31015ece8102b74fcbd90cce25e71bf2 | 92K |
| vendor/linux-kernel-5-4-ums512 | ead7c4e34de9a1465cb9badd7e0cf56691800b16 (master) | 1.2G |
| vendor/spl-uboot-patch | 35f79e746964a9e32ff0765119675b7899cec1b1 | 20M |

Plain dirs (not submodules): src/initramfs, src/rootfs-build, tools/spd_dump (present, not run), etc.
`src/spl` HEAD equals the standalone ums512_spl `rotate` HEAD.

## Arch Linux ARM rootfs (`upstream/alarm/`)
- `ArchLinuxARM-aarch64-latest.tar.gz` (829,367,415 bytes), `.md5`, `.sig` downloaded from http://os.archlinuxarm.org/os/.
- `md5sum -c` → `ArchLinuxARM-aarch64-latest.tar.gz: OK` (md5 23eec86365b24f7913c403e8f4e8719b).
- GPG `.sig` not verified (no ALARM signing key imported). UNVERIFIED.

## Failures
- None among repos: all seven cloned (none private or missing).
- `qemu-user-static` package unavailable (see deviation above).
- apt output noted outdated-process/needrestart notices for other sessions; no services were restarted.
