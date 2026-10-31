# Rotate OS QEMU test harness (aarch64 `virt`, 720x720)

Status: **scripts written and syntax-checked; guest boot NOT run.** The image build needs `mke2fs`, and the agent sandbox that wrote this hard-blocks filesystem formatting. So there are no boot, `/dev/dri/card0`, 720x720 mode, SW_LID or boot-time results yet. Nothing below is claimed as passing except what is listed under "Verified".

## To produce the image and run the proof (Dakoda, one command)
    cd /home/dakoda/Projects/rotate-os/qemu && ./build-vm-image.sh && ./smoke-test.sh

## Files
| File | Purpose |
|---|---|
| `build-vm-image.sh [-s GB] [-f] [IMG]` | 6 GB sparse ext4 **loop file** (`images/alarm-rootfs.img`) from `upstream/alarm/*.tar.gz`; refuses `/dev/*` and block devices. Copies kernel `Image` + `initramfs-linux.img` from the tarball `/boot` to `images/boot/`. Installs hostname, fstab (`/dev/vda`), modules-load (`virtio-gpu`, `uinput`), guest helpers, sshd key login (key `images/vm_key`). |
| `run-vm.sh` | `virt,gic-version=3`, TCG multi-thread, `-cpu max`, `-smp 4`, 4096 MB, `virtio-gpu-pci,xres=720,yres=720,edid=on`, xhci+usb-kbd+tablet, virtio-net user networking with `hostfwd 127.0.0.1:2222->22`, virtio-blk, virtio-rng. Direct kernel boot (ALARM `Image`, `root=/dev/vda console=ttyAMA0`). `-H` headless: `-display none`, serial on unix socket `images/serial.sock`, QMP on `images/qmp.sock`. No `-H`: GTK window + serial on stdio. `-e` UEFI (AAVMF) boot, `-u` firmware-only display test, `-S` snapshot (disk writes discarded). Env: `QEMU_CPU`, `QEMU_SMP`. |
| `screendump.py SOCK OUT.png` | Headless PNG via QMP `screendump`. |
| `serial-run.py` | Waits for serial login prompt, logs in root/root (VM only), runs a command, logs transcript, prints `BOOT_SECONDS`. |
| `smoke-test.sh` | Boot headless (snapshot), serial login, run `guest/rotate-vm-check.sh`, screendump (must be 720x720), ssh banner on forwarded port. Exit nonzero on any failure. Logs in `artifacts/` (`serial.log`, `guest-check.log`, `qemu.log`, `screendump-720.png`). |
| `guest/rotate-vm-check.sh` | In-guest asserts: `/dev/dri/card0`, `720x720` in `/sys/class/drm/*/modes`, fb0 720,720, swivel round trip, virtio-net. Final line `RV_RESULT=PASS/FAIL`. |
| `guest/rotate-swivel.c` | Swivel emulation (see below). Static aarch64 build via `aarch64-linux-gnu-gcc -static`. |

## Swivel emulation
Chosen: **in-guest uinput helper** (`rotate-swivel emit 1 [secs]` creates evdev device `rotate-swivel-emu` with `EV_SW/SW_LID`, 1 = closed; `rotate-swivel read` reads it back with `EVIOCGSW`). Reason: QEMU's virtio-input device types (keyboard/mouse/tablet/`virtio-input-host`) cannot emit `EV_SW`; `virtio-input-host` only passes through a host evdev node. The kernel's `uinput.ko` is present in the ALARM tarball (`usr/lib/modules/7.1.6-1-aarch64-ARCH/kernel/drivers/input/misc/uinput.ko`). Frontend code should read `SW_LID` (thwonp DT; see `docs/research/02-power-gaming-ux.md` L19) and the same helper drives it in tests.

## Verified (real runs, this session)
- `bash -n` on all shell scripts, `py_compile` on both Python scripts: OK.
- `aarch64-linux-gnu-gcc -static -O2 -Wall guest/rotate-swivel.c`: builds, ELF aarch64 static, no warnings.
- Tarball contents (via `tar tzf`): `/boot/Image`, `/boot/Image.gz`, `/boot/initramfs-linux.img`; kernel modules dir `7.1.6-1-aarch64-ARCH`; `drivers/gpu/drm/virtio/virtio-gpu.ko`, `virtio_net.ko`, `uinput.ko`, `usr/bin/sshd`, `usr/bin/agetty` present. No `modetest` in the tarball, so the check uses `/sys/class/drm/*/modes`.
- `./run-vm.sh -H -u` (AAVMF firmware only, headless) launches under QEMU 10.2.1 with the virtio-gpu-pci 720x720 device config; QMP `screendump` works and wrote `artifacts/fwonly-uefi.png` (PNG, **640x480**: that is the AAVMF GOP framebuffer, not the Linux KMS mode, so it proves nothing about 720x720).
- `./smoke-test.sh` without an image exits 3 with "no image".

## Not verified / gaps
- Kernel boot under TCG, boot time, serial login, `/dev/dri/card0`, 720x720 in `modes`, EDID effect, SW_LID round trip, hostfwd ssh, 720x720 screendump of the Linux console: **not run**.
- Whether the initramfs mounts `/dev/vda` (needs `virtio_blk` builtin or in initramfs) and whether `virtio-gpu.ko` autoloads from the root fs: unknown until first boot.
- UEFI/grub path (`-e`) untested; image has no ESP/grub.
- ALARM tarball GPG signature is unverified (see `docs/HOST-SETUP.md`).
