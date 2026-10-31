# Rotate OS QEMU test harness (aarch64 `virt`, 720x720)

Boots the **real built Rotate OS SD image** (`build/out/image/rotate-os.img`, root = `/dev/vda3`) under QEMU TCG. The harness creates no filesystem and formats nothing (no loop mounts, no sudo): the image is used through a qcow2 copy-on-write overlay.

    cd /home/dakoda/Projects/rotate-os/qemu && ./smoke-test.sh      # prepares + boots + checks; exit 0 = pass

## Result of the last run: SMOKE PASS (exit 0)
Full output: `artifacts/smoke-run.log`; serial transcript `artifacts/serial.log`; guest check `artifacts/guest-check.log`; QEMU log `artifacts/qemu.log`; `artifacts/prepare.log`; screendump `artifacts/screendump-720.png`.

| Requirement | Result (real output) |
|---|---|
| serial login | `rotate login: root` -> `[root@rotate ~]#` (serial.log lines 249-251) |
| `/dev/dri/card0` | `RV_PASS dri_card0_exists` |
| 720x720 in `/sys/class/drm/*/modes` | `RV_PASS mode_720x720_listed` (`modes=720x720 5120x2160 ...`); `fb0_size=720,720` |
| screendump | `screendump size 720x720` (`file`: PNG 720 x 720) |
| ssh banner on 127.0.0.1:2222 | `PASS ssh banner on 127.0.0.1:2222: SSH-2.0-OpenSSH_10.6` |
| `SW_LID` via uinput helper | `rotate-swivel emit 1` then `rotate-swivel read` (EVIOCGSW) -> `swivel_read=1`, `RV_PASS swivel_SW_LID_closed_reads_1` |
| virtio-net | `RV_PASS net_virtio` |
| source image untouched | `PASS raw image size/mtime unchanged`; `cmp` of pinned base vs `rotate-os.img` identical after the run |

**Boot time (measured):** `BOOT_SECONDS=40` (serial socket connect to `login:` prompt); guest `uptime=43s` when the check ran; whole `smoke-test.sh` 57 s. Host: 16 cores, QEMU 10.2.1, TCG `-cpu max -smp 4`. An earlier run on the same harness took 169 s to login / 391 s total while another agent's `build.sh` was running on the host; 40 s is the idle-host number. In both runs `iwd.service` fails (no Wi-Fi hardware in the VM; expected).

## How it boots without formatting
- `prepare-vm.sh` (run by `smoke-test.sh`, recreates everything each run):
  - pins a **sparse byte copy** (`cp --sparse=always`) of `rotate-os.img` as read-only `images/rotate-os-base.raw`, because `build.sh` rewrites `build/out/image` in place and a changing backing file corrupts a live overlay (seen once during development). `PIN=0` backs the overlay on the build output directly.
  - `qemu-img create -f qcow2 -F raw -b images/rotate-os-base.raw rotate-os-test.qcow2`: all guest writes go to the overlay.
  - extracts only `boot/Image` and `boot/initramfs-linux.img` from the ALARM tarball with `bsdtar` (Rotate OS's own kernel is for the T618 board, not `virt`).
  - builds `images/payload.tar` (modules for virtio-gpu/uinput/virtio_net plus deps and `modules.*` indexes from the ALARM tarball, kernel `7.1.6-1-aarch64-ARCH`; `rotate-swivel`; `rotate-vm-check.sh`), attached as a **second raw virtio-blk disk with no filesystem**. The guest runs `tar -xf /dev/vdb -C /`. 9p was not usable because `9p.ko` is itself a module.
- `virtio_blk`, `virtio_pci`, `ext4` are listed in the ALARM kernel's `modules.builtin`, so the stock initramfs mounts `root=/dev/vda3` unmodified (approach A worked; B not needed).
- `run-vm.sh`: `virt,gic-version=3`, TCG multi-thread, `virtio-gpu-pci,xres=720,yres=720,edid=on`, xhci+usb-kbd+tablet, virtio-net with `hostfwd 127.0.0.1:2222->22`, virtio-rng, direct `-kernel/-initrd`, `console=ttyAMA0`. `-H` headless (serial on `images/serial.sock`, QMP on `images/qmp.sock`); without `-H` GTK window + stdio serial. `-S` snapshot, `-e` UEFI (untested), `-u` firmware-only display test. Env: `QEMU_CPU`, `QEMU_SMP`.
- VM login is `root`/`root` (ALARM default); test VM only.

## Files
`prepare-vm.sh`, `run-vm.sh`, `smoke-test.sh`, `serial-run.py` (serial login + command + `BOOT_SECONDS`), `screendump.py` (QMP screendump), `guest/rotate-vm-check.sh` (in-guest asserts, final `RV_RESULT=`), `guest/rotate-swivel.c` (static aarch64 build via `aarch64-linux-gnu-gcc -static`). The old image-building script that created a filesystem was deleted.

## Swivel emulation
In-guest uinput helper (`rotate-swivel emit 1 [secs]` creates evdev `rotate-swivel-emu` with `EV_SW/SW_LID`, 1 = closed; `rotate-swivel read` reads it back with `EVIOCGSW`), because QEMU virtio-input devices cannot emit `EV_SW`. Frontend code should read `SW_LID` (thwonp DT; `docs/research/02-power-gaming-ux.md` L19). This proves the kernel uinput/EVIOCGSW path and the helper, not the frontend's own SW_LID handling.

## Gaps / caveats
- Display is `virtio-gpu` on the generic ALARM kernel 7.1.6, not the board's panel/Mali stack. The 720x720 mode comes from QEMU `xres/yres` + EDID, so this checks the userspace image and mode plumbing, not Unisoc DRM/Panfrost. The Rotate OS kernel is never booted here.
- `modes` also lists many larger modes; the test asserts 720x720 is present and fb0 is 720,720, not that it is the only mode.
- Modules come from the ALARM tarball, not the Rotate OS build.
- `rotate-resize.service` ran against `/dev/vda3`; its script exits 0 for non-`mmcblk`/`sd*` roots, so the resize path is untested. `rotate-firstboot` effects were not inspected.
- UEFI path (`run-vm.sh -e`) untested. ALARM tarball GPG signature unverified (`docs/HOST-SETUP.md`).
- `images/` and `*.qcow2` are git-ignored (pinned base is a 6 GB sparse file).
