# Phase 1 research 02: power, gaming, UX, Waydroid

Accessed 2026-10-31. Researcher: `research` profile. Local reads are from fresh `--depth 1` clones in
`~/.hermes/profiles/research/cache/scratch/r/` (not the `upstream/` dir) at these commits:
base618 `a1a1801` (2026-10-26), plorpOS `f555b88` (2026-10-30), MIROKI `ff83b88` (2026-10-27),
GammaOSNext README (2026-10-03). Kernel DT read from `thwonp/linux-mainline-sprd` branch `rotate-fixes` via the GitHub API.
Paths below are repo-relative; `L<n>` is a line number (lines in `platform_rotate.c` above L1700 and in the build scripts are approximate, +/- 5; grep the quoted function name). Nothing here was run on hardware (no hardware attached). "Source states" = read, not measured by me.

Source kinds: **P** primary (upstream repo/official docs), **S** secondary (wiki, forum, issue thread).

---

## 0. Headline findings (read this first)

1. **Deep sleep is a recipe, not a config switch.** base618 gets 35.7 mA -> 3.3 mA with five things: run the CM4 `pm_sys`, gate the USB UTMI clock at suspend, cooperatively power off the AGDSP, `arm,no-tick-in-suspend`, and release every peripheral that holds power. Rotate OS must take the kernel patches from `thwonp/linux-mainline-sprd` `rotate-fixes` and the SPL without `cm4_sec()`. (P: base618 `docs/deep-sleep.md` L1-L34)
2. **Userspace sleep hygiene is worth as much as kernel work.** plorpOS measured 149 mA asleep with panel + Wi-Fi on, 35 mA with both off, and 8.1 vs 5.4 mA from a PipeWire sink holding the codec PCM open. (P: plorpOS `src/platform_rotate.c` L1800-L1805, L1828-L1834)
3. **MIROKI does NOT use Panfrost.** It ships Arm's proprietary `libmali` blobs plus the out-of-tree `mali_kbase` driver (`beebono/mali_kbase` branch `ums512`). base618/plorpOS use mainline Panfrost + Mesa. Rotate OS goal says Panfrost: that matches base618, not MIROKI. (P: MIROKI `.gitmodules`, `system/prebuilts/libmali-blobs/`; base618 `docs/hardware.md` L8)
4. **cpufreq conflict between upstreams.** beebono says cpufreq works (A55 614 MHz-1.82 GHz, A75 1.23-2.0 GHz, schedutil). thwonp's `rotate-fixes` DT comment says the clock never follows the request: cluster0 pinned at 1536 MHz, cluster1 at 1228.8 MHz, measured with the PMU cycle counter, so cpufreq cooling is inert. Governor profiles (power save / balanced / max) may therefore do nothing until verified on device. See section 8 and open question Q1. (P: `DEVICE-BRINGUP.md` L35; `ums512.dtsi` L176-L190 on rotate-fixes)
5. **The swivel is an evdev switch, nothing more.** One hall GPIO (`ap_gpio 54`). thwonp DT reports `SW_LID` (1 = closed), beebono's bring-up doc says `SW_TABLET_MODE`. Rotate OS must pick one and read it with `EVIOCGSW`. MIROKI only sleeps on it; plorpOS turns the sway output 90 degrees with it. (P: see section 4)
6. **Waydroid on T618/Mali-G52: no report found.** Zero hits in waydroid/waydroid issues for Unisoc, T618 or UMS512, and zero in beebono/rg-rotate-linux, thwonp/base618, thwonp/plorpOS code search (GitHub search 2026-10-31). Related prior art on other Mali-G52 boards exists but is old (2022-2023) and shows breakage. See section 7.

---

## 1. thwonp/base618 (P)

License per directory (P: `README.md` L87-L88, `LICENSE`, `LICENSES/`):
- `scripts/`, `src/`: MIT, each file has an SPDX line.
- `README.md`, `AGENTS.md`, `docs/`: CC-BY-4.0.
- Kernel/U-Boot/SPL are separate repos (GPL-2.0 family, not re-verified here).

### 1.1 Deep sleep mechanism (`docs/deep-sleep.md`)
- Symptom: `echo mem > /sys/power/state` works and ATF completes PSCI suspend, but the SoC draws ~36 mA because it never reaches deep states. (L7-L8)
- Proof counters in PMU regmap debugfs (`dummy-syscon@...327e0000`): 0x788/0x770 AP light/deep, 0x758 APCPU_TOP, **0x784 PUB (DDR) deep "the one that matters"**, AON DCXO_LC_REG1 0x40 and PMIC 0x1b70/0x1b74 for 26 MHz XO low-current. (L11-L16)
- Required, in order (L18-L30):
  1. Run the CM4 (`pm_sys`) via remoteproc; needs the SPL without `cm4_sec()`, firmware from eMMC, DT node enabled, start at boot.
  2. USB PHY UTMI clock (AON EB1 b8) off at the final moment (musb noirq suspend). Otherwise PUB deep is refused. Stock fails with USB plugged for the same reason.
  3. AGDSP truly off. VBC driver PM ops shut it down, release AON 0x14c b5, wait for 0xb8 == 7, restart and restore MCDT DMA channels on resume. Forcing the domain off kills audio.
  4. `arm,no-tick-in-suspend` on the arch timer.
  5. Peripherals: speaker amp PM ops, codec MICBIAS hold, Wi-Fi (release it, ~60 mA if left associated), Bluetooth off, panel output off, no open PCM.
- Not needed: GPU/MM power domains, cluster light/deep, DMC low-power config, TrustOS, stock DT AON timers. (L36-L39)
- Measurement method: `rtcwake -m mem -s 180` plus fuel gauge `charge_now` before/after, unplugged, radios as in use; ignore the ~30 uAh tail of each wake. (L41-L44)
- Kernel summary: crackerjacques 7.2.9 fork + Armbian `sprd-ums512-7.2` patches + 34 base618 commits (`README.md` L46). Branch to track: `thwonp/linux-mainline-sprd` `rotate-fixes` (branch exists, GitHub API 2026-10-31).

### 1.2 First-boot firmware extraction
Files: `scripts/extract-firmware.sh` (MIT), `scripts/base618-firstboot.sh`, `scripts/base618-firstboot.service`, `scripts/make-initrd-fw.sh`, docs `docs/firmware.md`.

| Firmware | eMMC partition (by-partlabel, slot a then b) | Bytes (`dd bs=512`) | Check | Output |
|---|---|---|---|---|
| AGDSP audio DSP | `l_agdsp_a`/`_b` | 5242880 | first 14 bytes `SharkL5_AUDCP_` | `lib/firmware/updates/agdsp.bin` |
| CM4 pm_sys | `pm_sys_a`/`_b` | 1048576 | magic `dff80cd0` | `lib/firmware/sprd/pm_sys.bin` |
| WCN Wi-Fi/BT | `wcnmodem_a`/`_b` | 947200 (0xe7400, driver window; longer breaks the driver) | magic `a00a1a00` | `lib/firmware/updates/wcnmodem.bin` |

(`extract-firmware.sh` L9-L11, L28-L29, L47-L49; `docs/firmware.md` L5-L9.) Exit 0 only if all three valid. Reads `/dev/disk/by-partlabel` (override `PARTS=`).
- Plus `pm_bootcode.bin` (CM4 boot stub) built from GPL source by `scripts/build-pm-bootcode.sh` and shipped. (`docs/firmware.md` L11)
- First boot flow (`base618-firstboot.sh` L30-L51): show tty1 message, `udevadm settle`, rewrite `root=PARTUUID=` in `/boot/extlinux/extlinux.conf` after Armbian's resize changes it, run `extract-firmware.sh /`, run `make-initrd-fw.sh / /boot/initrd-fw.cpio.gz` (initrd = `dev/console` + 3 files; the built-in audio/pm drivers need firmware before rootfs), `depmod -a`, touch `/boot/base618-fw.done`, **power off** (reboot is unreliable on this chain), failure writes `/boot/base618-fw.failed` and boots without audio/Wi-Fi/deep sleep.
- Unit guards: `ConditionPathExists=!/boot/base618-fw.done` and `!...failed`; ordered after `local-fs.target`, `armbian-resize-filesystem.service`. (`scripts/base618-firstboot.service`)
- Pitfall: do not leave `wcnmodem.bin -> uwe5622/` symlinks from armbian-firmware; WCN driver oopses. (`docs/firmware.md` L32-L33)
- Redistribution: base618 ships nothing vendor. MIROKI does ship `agdsp.bin`, `pm_sys.bin`, sensorhub blobs in its repo (`system/prebuilts/firmware/`) with no license file found. Prefer base618's extract-from-own-device model.

### 1.3 USB serial console
- Linux console is USB gadget serial: `/dev/ttyACM0` on host, 115200, only after Linux is up (nothing earlier), getty takes 1-2 min. Fallback: NCM USB network gadget via configfs for ssh. (`AGENTS.md` L7-L9)
- Implementation: enables `serial-getty@ttyGS0.service`, masks `getty@tty1` (`scripts/make-card.sh` L170-L175, L207). The gadget itself comes from Armbian's image, not base618 scripts (UNVERIFIED which package).
- MIROKI has a self-contained configfs gadget script worth copying: `system/initramfs/usb-gadget` (ACM `ttyGS0` plus removable mass-storage LUN for the data partition; `start`, `storage on/off`). Idle VID/PID 0x1d6b/0x0104. License of MIROKI unknown (section 3).
- Host gotcha: ModemManager probes the ACM port; needs a udev rule (MIROKI `README.md` Console section).

### 1.4 Charging, audio, rotation (`docs/hardware.md`, `docs/quirks.md`)
- Charger: `bq2415x`. After OTG host use, charging stopped until defaults restored (`vbus_disable`); at shutdown the boost must be turned off or a USB device attached makes power-off reboot. (`hardware.md` L15; `quirks.md` L14, L16)
- Fuel gauge: `sc27xx-fgu`, `charge_now` in uAh; "the only usable current meter on the device". 1900 mAh battery. (`hardware.md` L16)
- Audio: SC2730 codec + speaker amp + AGDSP via VBC/MCDT. Pitfalls: first PCM open after sound server start stalls (warm it up); HPL/HPR gain inverted and reset on every PCM open (re-seed); mic is mono on channel 1; close the PCM when idle. (`hardware.md` L13, L24-L28)
- Rotation: hall = `SW_LID`, 1 = closed, closed = rotate picture 90 degrees. Let the compositor turn the output (`sway bindswitch lid:on output DSI-1 transform 90`) and map touch to the output; GT911 touch reports raw panel axes. (`hardware.md` L9-L12, L19-L22)
- Panel: backlight black at <= 80, dead zone ~90-170. (`hardware.md` L9)
- GPU: Mali-G52 MP2, panfrost, devfreq 384-850 MHz, GL 3.1 / GLES 3.x. (`hardware.md` L8)

---

## 2. thwonp/plorpOS (P)

Licensing is per file (`NOTICE`): MIT default (TortOS by Eric Reinsmidt + Thwonp). Exception: `src/chdread.c`, `src/chdread.h` derive from NextUI under PolyForm Noncommercial 1.0.0 (`LICENSES/PolyForm-Noncommercial-1.0.0.txt`). Also `LICENSE-FBNeo.txt`, `LICENSE-fake08.md`, `third_party/*`. Anything Rotate OS lifts must be checked by SPDX line first; **do not take `chdread.*`**.

### 2.1 Architecture on RG Rotate
- Runs from the SD card on base618; Android stays on eMMC (`README.md` Devices table).
- Session: `sd/rotate/plorpos.service` (replaces `getty@tty1`) runs `dbus-run-session sh session.sh`; `session.sh` starts `pipewire`, `wireplumber`, `pipewire-pulse`, then `sway -c sway.conf` (kiosk, every window fullscreen, `LIBSEAT_BACKEND=noop`, root). So plorpOS uses sway, **not** bare KMS.
- Launcher: `src/platform_rotate.c`, "SDL2 kmsdrm + GLES2 at 720x720, gpio-keys input by name, sysfs backlight, sc27xx-fgu battery, suspend to mem" (from commit message; see `docs/how-it-works.md`).
- Resident emulator design: the frontend "Diatom" (MIT libretro frontend) stays up on a Unix socket, cores are `dlopen`ed once at boot. Process start costs ~1100 ms (`GFX_init` ~620, dlopen ~170, audio/settings ~140, ROM open ~36); warm launch 26-44 ms RUN->RUNNING, ~70 ms from button press. Six cores mapped plus one running measured 15.0 MB. (`docs/how-it-works.md` L20-L49)

### 2.2 Game switcher
- MENU held ~0.7 s opens recent games as cards: left/right pick, A play, X remove, Y favorite. (`docs/guide.md` L38). Pause/resume window choreography: show launcher window, draw first frame underneath, then move game to hidden workspace; reverse to resume. (`src/platform_rotate.c` L240-L260, `screen_yield`)
- Auto-save on every exit path, auto-resume of `<ROM>.auto.state` on launch, 6 manual slots. (`docs/how-it-works.md` L156-L170)

### 2.3 Resume-on-wake / sleep implementation (`src/platform_rotate.c`)
- Power tap = sleep: screen dark at once, device suspends 10 s later; in a game the game is saved first. (`docs/guide.md` L41). Never sleeps while charging (`guide.md` L72-L74; a Brick-era rule, unverified for Rotate).
- `plat_sleep()` (L1822-L1862) order: `swaymsg "output * power off"` -> join pending radio restore thread -> `sync()` -> remember Wi-Fi and BT state -> `wlan_set(false)`, `bt_set(false)` -> `pactl suspend-sink` on the speaker sink (closes the PCM; 26 ms) -> `sleep_mem()` -> `output * power on` -> restore radios on a thread (wpa_supplicant re-associates in ~4 s).
- `sleep_mem()` (L1806-L1822): write `mem` to `/sys/power/state`, retry up to 5 times one second apart on EBUSY.
- Numbers in source comments: panel + display controller ~55 mA, associated Wi-Fi ~60 mA, 149 mA suspended with both vs 35 mA without; codec held by PipeWire PCM 8.1 mA vs 5.4 mA; Bluetooth ~0.8 mA asleep. (L1800-L1805, L1828-L1834, L1846-L1848)
- Game state is therefore kept in RAM (the process survives suspend) and also autosaved; "resume on wake" is plain suspend/resume plus the autosave safety net. No special snapshot mechanism.

### 2.4 melonDS PGO build recipe (`mk/build-melondsds.sh`, `tools/ds-pgo-train.sh`, `mk/pgo/melondsds.tgz`)
- Source pins: `JesseTG/melonds-ds` commit `f394adba...` (v1.4.0) and `JesseTG/melonDS` commit `16f127db...` (L52-L53). Two patches applied with `git apply`.
- Build in a Debian trixie cross docker image (`mk/rotate-cores.Dockerfile`), cmake + Ninja, toolchain `mk/rotate-aarch64.cmake`; `-mcpu=cortex-a75.cortex-a55`; flags `-DENABLE_OPENGL=OFF -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON`, Release (L83-L96). ~3 minutes (L32).
- PGO: `--instrument` mode adds `-fprofile-generate -fprofile-update=atomic`; use mode unpacks `mk/pgo/melondsds.tgz` and adds `-fprofile-use -fprofile-partial-training -fprofile-correction -Wno-missing-profile` (L74-L84). Profile keyed by object path, so both modes build in `/out/build`.
- Training: `tools/ds-pgo-train.sh`, ~6 min on the device over ssh, unthrottled on the two A75s, using `tools/dsbench.c` with Pokemon Pearl and Metroid Prime Hunters 3D attract scenes, Advance Wars and Pokemon scripted input, save states and a reload. Dragon Quest V is deliberately held out as the validation ROM.
- Output sha256 is pinned; build fails if toolchain/source/patches/profile drift (L102-L109).
- Patches:
  - `mk/patches/melonds-plorpos.patch` (123 lines, 5 files): ARMv8 CRC32 instructions in `src/CRC32.cpp` (bit-identical, 35x faster); DS-mode savestates keep 4 MiB main RAM instead of 16 (19.7 -> 7.2 MB, serialize 13.7 -> 4.4 ms, savestate minor 1, old states still load); JIT slices `madvise`-release pages on free; CPSR-restore warning downgraded to Debug (Pokemon Pearl triggers it ~750,000 times/s).
  - `mk/patches/melondsds-plorpos.patch` (62 lines): "unload leaves a fresh core" so a second game in a resident process no longer segfaults; frees the ROM copy (128 MiB for DQ V).
- **Fps numbers, source states** (commit `cc3b38c`, message body, from `git log`/GitHub API; not in the tree files): PGO took Pokemon 3D 10.0 -> 8.6 ms/frame, Metroid Prime Hunters p99 16.3 -> 15.4 ms, **Dragon Quest V 57.6 fps / 37 resyncs -> 59.8 fps / 0 resyncs** in Diatom with rewind on. RSS after a DS game 218 -> 52 MB. Later commit: resident Diatom, Advance Wars 59.51 fps, Metroid 59.85 fps, 0 resyncs past startup.
- Alternative core: DSperate-lr (DSperate v3.0.5, GCC 13.3 PGO profile, sha256-pinned; `mk/build-dsperate.sh`) is plorpOS's default DS core; melonDS DS is the option on Rotate. (`docs/install-rotate.md`)

### 2.5 Square DS layouts
- Layout row: Focus Top, Focus Bottom, Even Split, Top Overlay, Bottom Overlay; hotkey Screen Layout (MENU+X) cycles. Display row sets overlay big screen to Aspect, Stretch or Integer. Stylus Mode and Stylus Tap hotkeys; touch on bottom screen is the stylus. (`docs/install-rotate.md` DS section)
- Rotate melonDS options are in `config/systems-rotate.cfg` (not read in detail; UNVERIFIED which keys).

### 2.6 PortMaster integration (`sd/rotate/portmaster/`)
- Unofficial; PortMaster is the first entry on the Ports shelf. Ports land in `Roms/Ports/`, PortMaster in `.userdata/shared/portmaster/`. (`docs/install-rotate.md` L113-L115)
- `pm-run.sh` (every port launches through it): sets `XDG_DATA_HOME`, `HM_TOOLS_DIR`, `HM_PORTS_DIR`, `HM_SCRIPTS_DIR`; `DISPLAY=:0` for X11 ports (sway starts Xwayland on demand, quits 10 s after last client); prepends `/usr/lib/plorpos-mesa/lib` to `LD_LIBRARY_PATH` (own Mesa: no LLVM, with GLES 1, because Debian's Mesa linked libLLVM crashed Mono/FNA ports and gave no GLES 1); sets `HOME` to PortMaster's folder; cleans up stale squashfs/bind mounts left by killed ports.
- `control.txt` replaces PortMaster's stock one before each run (a self-update restores the stock); names the pad by SDL GUID `19007b94010000000100000000010000` (`param_device=rgrotate`) to avoid passing the 470 KB gamecontrollerdb ("Argument list too long"). Modelled on spruce.
- Own Mesa recipe: `mk/build-ports-mesa.sh` (Panfrost only, no LLVM, GLES 1). (also `base618 docs/quirks.md` L26-L30)

### 2.7 Swivel handling in plorpOS
- `sd/rotate/sway.conf`: `input type:touch map_to_output DSI-1`; `bindswitch --reload --locked lid:on output DSI-1 transform 90`; `lid:off ... transform normal`. Keys are read from evdev directly; sway's keyboard handling is not disabled because touch and hall share libinput devices with key capability.
- `lid_closed()` in `src/platform_rotate.c` L960-L966 reads state with `EVIOCGSW` on the power-key fd; used for touch coordinate mapping (`gs_screen`) and `gs.closed` at L998.
- Closed (screen over buttons) = touch UI: tap=A, swipe=d-pad, edge swipe=B, long press=Y, 2-finger tap=X, 3-finger tap=Menu. (`docs/install-rotate.md` L79-L95)
- No "swivel closed pauses the game" logic found in plorpOS beyond rotation. Rotate OS's pause-on-swivel is new work (MIROKI's sleep-on-close is the closest reference).

---

## 3. beebono/MIROKI (P)

- Standalone CFW booting from eMMC to RetroArch with a custom menu driver `ramen`. **Destroys Android, no dual boot.** (`README.md` Flashing)
- **No LICENSE file** in the tree; README has no license text. Submodules carry their own (RetroArch GPL-3, kernel GPL-2, etc.). Treat MIROKI's own code (`system/ramen`, `system/aggregator`, `system/initramfs`, `tools/`) as all-rights-reserved until the author says otherwise. Ask beebono, or write fresh. Fonts: Audiowide under OFL (`system/ramen/assets/fonts/OFL.txt`).
- Boot chain: rebuilt Unisoc SPL + a small EL3 monitor of its own (no stock secure world/u-boot); kernel `beebono/linux-mainline-sprd` branch `emmc-cfw`; squashfs root; busybox init; `data` partition exFAT; optional SD for extra ROMs/cores. Submodule list in `.gitmodules`.
- Graphics: **proprietary Arm libmali 1.10.0 + `mali_kbase` out-of-tree driver**, not Panfrost (`system/prebuilts/libmali-blobs/`, `ext/system/mali-kbase` -> `beebono/mali_kbase` branch `ums512`).
- RetroArch build (`tools/build_retroarch.sh` L40-L60): applies `system/ramen/retroarch-hook.patch` and copies `ramen.c` into `menu/drivers/`; CFLAGS `-O2 -mcpu=cortex-a75.cortex-a55`; `--enable-kms --enable-egl --enable-opengles --enable-opengles3 --enable-opengles3_1 --enable-opengles3_2 --disable-opengl1 --disable-vulkan --enable-alsa --enable-udev --enable-threads`; disables x11, wayland, sdl, sdl2, pulse, pipewire, jack, systemd, networking, slang/glslang, freetype, cheevos, materialui, xmb. `make HAVE_RAMEN=1`.
- RetroArch config (`system/rootfs/etc/retroarch.cfg`): `video_driver="gl"`, `audio_driver="alsa"`, `input_driver="udev"`, `input_joypad_driver="udev"`, `menu_driver="ramen"`, `menu_screensaver_timeout="60"`; no `video_context_driver` set (auto-selects `kms`).
- KMS/DRM details: `system/rootfs/usr/bin/miroki-frontend` unbinds fbcon (`/sys/class/vtconsole/vtcon*/bind`) and zeroes `/dev/fb0` before `exec retroarch`, because RetroArch's DRM context restores the CRTC on every video reinit and would flash the console. Respawned by init (`etc/inittab`).
- **How the swivel is read (answer to the brief):** `ramen` does not read it. `system/aggregator/main.c` (`miroki-inputd`) grabs `gpio-keys-system` and on `EV_SW`/`SW_LID` value 1 calls `go_to_sleep("swivel closed")` (L516-L517). Sleep is `mem`, EL3 `SYSTEM_SUSPEND`; backlight is switched off by hand around it. Power key short = sleep, hold 2 s = off with vibrator buzz. The same daemon merges `gpio-keys-gamepad` and the vibrator into one uinput pad with FF_RUMBLE, owns volume (softvol via `tmix`) and Mode+volume = brightness.
- `ramen` itself: touch read straight from evdev; accelerometer through IIO buffer `sprd-shub-accel` (sensorhub firmware, `system/prebuilts/firmware/sprd/sensorhub/`) for physics-animated carousel; pivot corner constants `RAMEN_PIVOT_X/Y` assume the open orientation (`ramen.c` L283-L292). Design doc: `system/ramen/menu-design.md` (glass-tube quarter-arc around the pivot, capsules for games).
- Note conflict: beebono's `DEVICE-BRINGUP.md` L50 says stock HW has only the hall switch and no accel/gyro in the stock DT, yet MIROKI loads sensorhub accel firmware and reads `sprd-shub-accel`. Treat accelerometer availability as UNVERIFIED on the kernel Rotate OS will use.
- Kernel config (`tools/cfw-kernel.config`): `NO_HZ_IDLE`, `PREEMPT` (full), `HZ_250`, `CPU_FREQ` with schedutil default, performance governor available. Compare with the `rotate-fixes` config before trimming.

---

## 4. Swivel summary table (P unless noted)

| Item | Value | Source |
|---|---|---|
| GPIO | `ap_gpio 54`, active-low, debounce 10 ms, wakeup-source (wake on open only) | thwonp `ums512-rg-rotate.dts` L316-L330 (rotate-fixes) |
| Event | thwonp: `EV_SW`/`SW_LID`, 1 = closed. beebono bring-up: `SW_TABLET_MODE` | DTS above; `DEVICE-BRINGUP.md` L50 |
| Read state | `ioctl(fd, EVIOCGSW)` | plorpOS `platform_rotate.c` L960-L966 |
| Compositor path | sway `bindswitch lid:on output DSI-1 transform 90` | plorpOS `sd/rotate/sway.conf`; base618 `docs/hardware.md` L21 |
| No-compositor path | DRM plane rotation or RetroArch `video_rotation` / own render rotation (not researched; Q4) | n/a |
| Sleep on close | MIROKI, `aggregator/main.c` L516 | MIROKI |

---

## 5. TheGammaSqueeze/GammaOSNext (UX reference only)

- License: Apache-2.0 (`LICENSE`). Repo contains README, images, `FixOTA.7z`, `MAGISK_ES-DE_fix.zip`; **no source for the Rotate slide/swivel handling or governor tables** (repo tree listing 2026-10-31). Cannot cite code for either.
- README Performance section (L53-L60): three profiles. **Max Performance** (highest draw), **Normal Performance** (dynamic scaling), **Power Save** (lower frequencies). Quick-settings tiles for them. **Deep Sleep Mode** (cgroup freeze of background apps). No frequency numbers published. T618 governor/frequency tables from GammaOS: **not found**.
- GammaOS Nano v1.4.1 for Rotate release notes (release `v1.4.1-ANBERNICRGROTATE`, dated 13 Aug 2026): "DRM-direct rendering straight to the panel, early-boot audio engine, native power and sleep management, and direct control of the panel and the slide sensor"; Linux support moved to beebono's `distribution` releases (ROCKNIX-T618 fork). The notes say nothing about swivel-triggered behaviors in detail. Wiki home (https://thegammasqueeze.github.io/GammaOSNano-WIKI) lists themes, controls cheat sheet, settings reference but I did not open the controls page; swivel UX specifics: UNVERIFIED.
- The brief's list (rotate, sleep/wake, launch app, clock UI on slide-shut) is not confirmed by any file I could read. Take UX ideas from the mission brief, not from GammaOS.
- Release assets include `UnisocTools_RG_ROTATE.zip` (FDL files for spd_dump) per the release asset list.
- Useful frequency source instead: the T618 OPP tables in thwonp `ums512.dtsi` (section 8).

---

## 6. Platform stack research

### 6.1 Arch Linux ARM aarch64 rootfs
- Tarball: `http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz`, redirects to a mirror; Content-Length 829,367,415, Last-Modified Wed 05 Aug 2026 12:41:36 GMT (HEAD request 2026-10-31). (P: https://archlinuxarm.org/platforms/armv8/generic)
- Extract as real root (not sudo) with `bsdtar -xpf ...tar.gz -C mountpoint` to keep xattrs/ACLs. Default: `base` group, openssh, haveged, user `alarm`/`alarm`, root password `root`, systemd-networkd DHCP for `eth0`/`en*`, systemd-resolved, timesyncd. First boot: `pacman-key --init; pacman-key --populate archlinuxarm`. (same page)
- The generic tarball ships mainline `linux-aarch64` (EFI-stub `Image`, dtbs in `/boot/dtbs`); Rotate OS must not use it (needs the thwonp kernel + DTB). Remove the package from the image and `IgnorePkg = linux-aarch64 linux-aarch64-headers`, or pacman will replace our kernel. (inference; UNVERIFIED on device)
- Mirror snapshot (http://mirror.archlinuxarm.org/aarch64/core|extra, 2026-10-31): `linux-aarch64 7.3.1-1`, `mesa 1:26.2.6-1`, `retroarch 1.22.2-5`, `waydroid 1.6.3-1`, `libretro-melonds 20260719`, `libretro-mupen64plus-next`, `libretro-pcsx-rearmed`, `libretro-beetle-psx`, `libretro-picodrive`, `libretro-genesis-plus-gx`, `libretro-snes9x`, `libretro-mgba`, `libretro-flycast`, `libretro-dolphin`, `libretro-yabause`, `libretro-scummvm`, `libretro-core-info`. No PortMaster package. Non-PGO melonDS; Rotate OS must build its own for PGO.
- `pacstrap`: host has `arch-install-scripts 31-1` (Ubuntu apt) and `libarchive-tools 3.8.5` (bsdtar). `qemu-user-static` has **no apt candidate** on this host (checked `apt-cache policy` 2026-10-31); check `qemu-user` + `binfmt-support` instead (Phase 2 lane). pacstrap needs a `pacman.conf` pointing at an ALARM mirror and the ALARM keyring; running it from x86 is possible only via qemu-user chroot of an ALARM tarball. Simplest reproducible route: **extract the ALARM tarball, chroot with qemu-user, run `pacman -Syu` and install the package list**. pacstrap-on-Ubuntu: UNVERIFIED, not attempted.
- Docs for ALARM licensing: packages carry their own licenses; no redistribution issue for the tarball (unverified).

### 6.2 Mesa Panfrost on Mali-G52 (Bifrost v7)
- Mesa docs table (P, https://docs.mesa3d.org/drivers/panfrost.html, accessed 2026-10-31): row "G31, G51, G52, G76 / Bifrost (v7)": OpenGL ES **3.1** with footnote "Mali-G52 OpenGL ES 3.1 submission", OpenGL 3.1, **Vulkan 1.3**, OpenCL 3.0. So GLES 3.1 conformance is a submission footnote (the mission's "conformant" is supported to that extent); the Vulkan 1.3 cell carries no conformance footnote.
- PanVK: "On GPUs where PanVK support is experimental, the driver refuses to load by default. `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` enables it." The page does not say per row which GPUs are experimental, so G52 PanVK being experimental is UNVERIFIED but the only G52 Vulkan work I found elsewhere is alpha-stage (below). Plan GLES 3.1 only for gaming.
- Related Vulkan work: `LukeValen/panvk-mali-g52` v0.0.1-alpha runs PanVK on Mali-G52 MC2 through proprietary `mali_kbase` on Android/Termux, without a DRM render node. Not applicable to a Panfrost kernel. (S, https://github.com/LukeValen/panvk-mali-g52)
- Distribution: Arch's PKGBUILD builds `vulkan-drivers=...panfrost...` and `gallium-drivers=all` (checked in the x86_64 Arch PKGBUILD, `pkgver=26.2.6`: https://gitlab.archlinux.org/archlinux/packaging/packages/mesa/-/raw/main/PKGBUILD); ALARM builds from the same PKGBUILD (UNVERIFIED, I only confirmed the version on the ALARM mirror).
- On the device: GPU node `gpu@60000000`, compatible `sprd,ums512-mali`, `arm,mali-bifrost`, OPP 384/512/614.4/768/850 MHz at 0.70-0.80 V (`ums512.dtsi` L1374-L1420 on rotate-fixes; enabled in `ums512-rg-rotate.dts` L740-L742). Working under mainline panfrost per beebono (`DEVICE-BRINGUP.md` L37) and base618 (`hardware.md` L8).
- plorpOS pitfall: Debian's Mesa linking LLVM crashed Mono ports; GLES1 absent. Arch's mesa builds LLVM-based llvmpipe; for ports build a Panfrost-only Mesa like `mk/build-ports-mesa.sh`.

### 6.3 RetroArch on KMS/DRM, no compositor
- Official: Mesa with libgbm and libdrm, an open KMS driver, build flags `--enable-kms --enable-egl`; check `retroarch --features` shows "KMS/EGL context support: yes". Vulkan needs `VK_KHR_display` (Mesa 21+). (P, https://docs.libretro.com/guides/kms-mode/)
- In source the DRM context driver's ident is `"kms"` (`gfx/drivers_context/drm_ctx.c` L1480, upstream master) so `video_context_driver = "kms"` can force it; `video_context_driver` is a normal setting (`configuration.c` L2037). Leave blank for auto; set explicitly for determinism. (P)
- Known-working recipe from MIROKI (section 3): `video_driver="gl"` (GLES via EGL), `input_driver="udev"`, `audio_driver="alsa"`, unbind fbcon first, no seat manager. Without logind/seatd, run as root or add the user to `video`, `input`, `render` groups.
- Arch ALARM `retroarch 1.22.2-5` should include KMS (UNVERIFIED which options Arch enables; check `retroarch --features` in the Phase 2 image, else rebuild with MIROKI's flag set).
- Rotation without a compositor: not researched in a source I can cite. Options to test: RetroArch `video_rotation`, a DRM plane rotation property, `fbcon=rotate`. Q4.

### 6.4 PortMaster on Arch without Debian-isms
- PortMaster CFW requirements (S, bmdhacks gist, Feb 2026, https://gist.github.com/bmdhacks/f26dbb60dc0ff0b9bc8e043fac8b813b): real `/bin/bash` 4+, GNU coreutils (BusyBox breaks hundreds of ports), `tar unzip gzip mount umount`, `sudo` or `doas`, `systemctl`, recommended `dos2unix xdelta3 dialog rsync 7z`; kernel `SQUASHFS` (zlib/lzo/xz/zstd), `BLK_DEV_LOOP`, `INPUT_UINPUT` (`/dev/uinput` for gptokeyb), `ZRAM`; SDL2 family (`libSDL2`, `mixer`, `image`, `ttf`); aarch64; **Python 3 with liblzma** (`import lzma`); `control.txt` provides `$DEVICE_ARCH`, `$PM_CAN_MOUNT`, etc.
- Arch fit: bash, GNU coreutils, systemd, sudo, python (with lzma), sdl2 are all in Arch ARM repos (python dependency list on https://archlinuxarm.org/packages/aarch64/python; sdl2 not individually confirmed). Debian-isms in the gist: it points at Debian 11 compat libraries (darkOS `fetch_compat_libs.sh`) because ports bundle old libs in `libs.aarch64/`; Arch's newer glibc is backward compatible so bundled libs work; do not set `LD_PRELOAD`.
- Arch-specific risks: no `apt`; scripts that call `apt` or detect `CFW_NAME` will not match; RetroArch-based ports key on `$CFW_NAME` path switch so put `retroarch` in a standard `$PATH` or add a case (gist section on RetroArch path variance).
- Work already done: plorpOS `control.txt` + `pm-run.sh` are the template (section 2.6). Both are MIT. They hard-code `/mnt/SDCARD/...`; parametrize.
- Caveat: AUR `portmaster-bin` is **Safing Portmaster (firewall)**, unrelated. Do not install it.
- No ALARM-specific PortMaster report found (search 2026-10-31).

---

## 7. Waydroid on Rotate OS

Requirements (S/P):
- Kernel: `CONFIG_ANDROID=y`, `CONFIG_ANDROID_BINDER_IPC`, `CONFIG_ANDROID_BINDERFS`, `CONFIG_ANDROID_BINDER_DEVICES`; also netlink conntrack (`CONFIG_NF_CT_NETLINK=y`), PSI (`CONFIG_PSI=y`), loop (`CONFIG_BLK_DEV_LOOP=m`, module loaded before start). (S, ArchWiki https://wiki.archlinux.org/title/Waydroid, accessed 2026-10-31; the wiki flags its kernel section "being considered for removal"/"out of date")
- Waydroid dev (aleasto), P-adjacent: for mainline, **binderfs is "heavily recommended"**; Waydroid creates `binder`, `hwbinder`, `vndbinder` itself, `BINDER_DEVICES` is only the initial set. Many distro kernels already suffice; the Android base config (android-6.1) is the reference. `ashmem` is gone from mainline since 5.18; Android userspace uses memfd (`sys.use_memfd=true` seen in a prop file). (S, https://github.com/waydroid/waydroid/issues/811)
- Needs a **Wayland session**; ArchWiki suggests `cage` for a single-app kiosk. This contradicts "no compositor" only for the Waydroid mode; plan: launch `cage` (or sway as plorpOS does) on demand, then `waydroid show-full-ui`, tear down on exit. (S, ArchWiki)
- GPU: ArchWiki says it works best with Intel/AMD; nothing about Mali. Android side uses Mesa gbm gralloc (`ro.hardware.gralloc=gbm`, `ro.hardware.egl=mesa`); fallback to software (`swiftshader`) exists. (S, issue #770, #671 prop file)
- Arch ARM has `waydroid 1.6.3-1` in `extra` (mirror, 2026-10-31). The user-space ABI is native arm64, so ARM apps run natively with no translation.

Prior reports on Panfrost boards (S, all old):
- #356 (2022-04): Mali G52 Panfrost, mainline 5.17, Mesa 22.0.1: display corrupted, works with software rendering. Closed.
- #671 (Dec 2022): Mali-G52 (Ugoos AM6b Plus), Manjaro ARM, kernel 6.0.12: `waydroid-container.service` fails to load. Closed.
- #770 (Feb 2023): RK3399 Mali-T860 under a wlroots compositor failed to start; worked under weston, and with `default`/`swiftshader`; fixed by newer Mesa. Closed.
- These show Panfrost + Waydroid is possible but has bitten others with GPU buffer sharing; no recent (2025-2026) G52 success report found.

**T618 / UMS512 / Rotate: no report exists** in what I could search. GitHub issue search on waydroid/waydroid for "Unisoc", "T618", "UMS512": 0 results each; code search for "waydroid" in beebono/rg-rotate-linux, thwonp/base618, thwonp/plorpOS: 0 results (2026-10-31). Web search found none either.

Rotate-specific constraints (inferred, UNVERIFIED):
- 6x A55 + 2x A75, 720x720, probably 2-4 GB RAM (not checked). Waydroid image + Android system RAM will compete with games; memory budget needs measuring. Run on demand only.
- Audio goes through the AGDSP path whose cooperative suspend is delicate; Android's audio HAL in the container must go through PipeWire/ALSA on the host, and must not hold the codec at suspend.
- Deep sleep: Waydroid must be stopped before suspend or the container wakes the CPU.

---

## 8. Power budget (verified against source)

All are current at the battery while suspended, unless stated. "Source states" means I read the number, not measured it.

| Case | mA | Evidence |
|---|---|---|
| Stock Android, suspended | **2.55** | base618 `docs/deep-sleep.md` L33: "stock Android's 2.55 mA on the same unit" |
| base618/plorpOS overnight, deep sleep | **3.30** (~2 %/12 h on 1900 mAh) | same file L32-L34; title L1 says "within 29 %" (0.75 mA gap) |
| Plain mainline, `mem` suspend | **35.7** (~36) | same file L1, L8 |
| plorpOS suspend with panel + Wi-Fi left on | 149 | `platform_rotate.c` L1803 |
| plorpOS suspend, panel + Wi-Fi off | 35 | same, L1804 (before kernel deep sleep fixes) |
| Ladder from deep-sleep.md L32 | 149 -> 41 -> 29 (PUB deep) -> 14 (XO low) -> 4.1 -> 3.30 | each step is a fix in section 1.1 |
| Codec held by idle PipeWire PCM | 8.1 vs 5.4 closed | `platform_rotate.c` L1828-L1834, bead 74.19.1 |
| Bluetooth powered during sleep | ~0.8 extra | same, L1846-L1848 |
| Associated Wi-Fi during suspend | ~60 | same, L1801-L1802, `deep-sleep.md` L29 |
| Panel + display controller in light sleep | ~55 | same, L1801 |

Checks: 3.30/2.55 = 1.29, i.e. the "within 29 %" claim holds. 36/3.3 is about 11x. The user brief says "~3.3 mA, stock 2.55, mainline 36" — all three match source. Not measured by me. **Active-state draw (gaming, idle menu, screen on) is not documented in any source read**; the budget table has no screen-on numbers. Q2.

Also useful:
- Battery 1900 mAh (`hardware.md` L16), so 3.3 mA -> ~24 days theoretical, 2 %/12 h measured. Fuel gauge `charge_now` has ~30 uAh tail per wake: short tests are noisy.
- CPU OPP tables from `ums512.dtsi` L81-L113 (rotate-fixes). Cluster0 (cpumask 0x3f, 6x A55, kHz/uV): 614400/756250, 768000/756250, 962000/756250, 1144000/756250, 1228800/787500, 1482000/875000, 1536000/890625, 1716000/953125, 1820000/990625, 2002000/1050000. Cluster1 (cpumask 0xc0, 2x A75): 1228800/750000, 1482000/850000, 1536000/870000, 1742000/950000, 1872000/1000000, 2002000/1050000. `transition_delay_us=500`. Node comment: tables carry T618 values on this SoC generation.
- cpufreq status disagreement (also headline 4): same file, L176-L190: "cluster0 sits at a fixed 1536MHz and cluster1 at 1228.8MHz no matter what is requested ... cpufreq cooling devices are inert"; throttling is by idle injection (`thermal-idle`, `duration-us=10000`). beebono says cpufreq works (`DEVICE-BRINGUP.md` L35). The two statements come from different branches/dates; branch `rotate-fixes` is what Rotate OS will track.
- GPU OPP: 384/512/614.4/768/850 MHz, 0.70-0.80 V.
- DDR devfreq: not present; memory clock pinned at boot frequency ("one confirmed gap", `DEVICE-BRINGUP.md` L77).
- GammaOS governor tables: not available (section 5).

---

## 9. What Rotate OS should port, and from which file

| # | Port | From (file) | License | Notes |
|---|---|---|---|---|
| 1 | Kernel deep-sleep patch set | `thwonp/linux-mainline-sprd` branch `rotate-fixes` (34 commits) | GPL-2.0 | Base of the kernel; trim config after; keep CM4/pm_sys remoteproc, VBC AGDSP PM, musb noirq UTMI gate, arch timer `no-tick-in-suspend`, amp, MICBIAS, WCN |
| 2 | SPL without `cm4_sec()` | thwonp `ums512_spl` branch `plorpos-cm4` (`docs/install.md` table) and `boot-chain.md` | GPL-2.0-or-later | Hard requirement for deep sleep; SD-first with eMMC Android fallback unchanged |
| 3 | Firmware extraction | `scripts/extract-firmware.sh`, `make-initrd-fw.sh`, `base618-firstboot.sh/.service`, `build-pm-bootcode.sh` | MIT | Re-write the `armbian-resize` ordering for Arch; the PARTUUID fix-up logic is needed whenever the root partition grows |
| 4 | Power key + sleep helper | `src/base618-power.c` + `scripts/base618-power.service`, `base618-power-logind.conf` | MIT | Tap = suspend, hold 3 s = off |
| 5 | Sleep sequence | plorpOS `src/platform_rotate.c` `plat_sleep()`/`sleep_mem()` L1806-L1862 | MIT (check SPDX per file) | Order: output off, radios off, close audio PCM, `echo mem`, restore. Replace swaymsg with DRM DPMS/connector off |
| 6 | USB serial gadget | MIROKI `system/initramfs/usb-gadget` (idea) or Armbian's | MIROKI: unlicensed -> rewrite; Armbian: GPL-2 | `serial-getty@ttyGS0` enable as in `make-card.sh` L207 |
| 7 | Swivel read + rotate | plorpOS `sd/rotate/sway.conf`, `platform_rotate.c` L960-L966; `hardware.md` L19-L22 | MIT | `EVIOCGSW`; mask touch to output |
| 8 | Sleep on swivel close | MIROKI `system/aggregator/main.c` L516 (idea) | unlicensed -> rewrite | Rotate OS wants pause + low-power, not just sleep |
| 9 | melonDS build | `mk/build-melondsds.sh`, `mk/patches/melonds-plorpos.patch`, `melondsds-plorpos.patch`, `mk/pgo/melondsds.tgz`, `tools/ds-pgo-train.sh` | MIT | Profile was trained on this CPU with Debian trixie GCC; retrain on Arch GCC (profile keyed by path; mismatch = `-Wno-missing-profile`) |
| 10 | Game switcher / resume | plorpOS `src/platform_rotate.c` L240-L260 and launcher | MIT | Frontend is Phase 5; take design only (resident core model gives 26-44 ms warm launch) |
| 11 | PortMaster glue | `sd/rotate/portmaster/pm-run.sh`, `control.txt` | MIT | Replace `/mnt/SDCARD` paths; PortMaster itself is MIT but fetch it from upstream |
| 12 | Ports Mesa | `mk/build-ports-mesa.sh` (plorpOS branch `feat/rotate`) | MIT | Panfrost-only, no LLVM, GLES1; apply only to port runs |
| 13 | RetroArch build flags | MIROKI `tools/build_retroarch.sh` L40-L60 | unlicensed -> use as flag reference only | Compare with Arch's `retroarch` options before rebuilding |
| 14 | DS layouts | plorpOS DSperate/melonDS layout options (`config/systems-rotate.cfg`, `docs/install-rotate.md`) | MIT | Rotate square 720x720: 5 layouts above |
| 15 | Hardware quirks list | base618 `docs/hardware.md`, `docs/quirks.md` | CC-BY-4.0 | Codec gain inversion, first PCM open stall, charger boost, mic mono ch1 |
| NO | `src/chdread.c`, `chdread.h` | plorpOS | PolyForm Noncommercial | Do not take |
| NO | libmali blobs and `mali_kbase` | MIROKI | proprietary / unclear | Conflicts with Panfrost plan |
| NO | Vendor firmware blobs in repo | MIROKI `system/prebuilts/firmware/` | unclear | Extract from own eMMC instead |

---

## 10. Open questions

- **Q1** Does CPU frequency scaling actually change clocks on `rotate-fixes`? DT comment says no (fixed 1536/1228.8 MHz), beebono says yes. Needs a device test: pin a busy loop, read the PMU cycle counter at each `scaling_setspeed`. Until then, "performance profiles" may only be idle-injection and governor settings. **Device step for Dakoda:** none yet; an SD image test script will ask for it in Phase 4.
- **Q2** Screen-on idle and gaming current draw: not in any source. Need device measurement via `sc27xx-fgu` `charge_now`/`current_now` (does `current_now` exist? `hardware.md` L16 says `charge_now` is the usable meter).
- **Q3** Why does plorpOS use sway rather than bare KMS? The commit history says sway is chosen for Xwayland (X11 ports) and window hand-off between launcher and game. For Rotate OS, can RetroArch KMS + a small own launcher avoid it, with Xwayland only for ports?
- **Q4** Rotation without a compositor: DRM plane rotation on the sprd DPU driver, RetroArch `video_rotation`, or fbcon rotation. Not researched in a citable source.
- **Q5** Hall event type: `SW_LID` (thwonp DT) vs `SW_TABLET_MODE` (beebono). Which does the kernel we build emit? Check `ums512-rg-rotate.dts` of the pinned branch and `evtest` on device.
- **Q6** Is the accelerometer present? beebono says no sensors besides hall; MIROKI reads `sprd-shub-accel`. Probably a MIROKI-branch driver only.
- **Q7** MIROKI licensing: ask beebono for a license, or rewrite everything from its README design. Also thwonp kernel/U-Boot/SPL licenses were not re-read here.
- **Q8** Does a PGO profile trained on Debian trixie GCC apply with Arch's GCC? Probably partially; retrain with `ds-pgo-train.sh` on device (about 6 min). Needs ROMs on device.
- **Q9** pacstrap on Ubuntu 26.04 for ALARM: `qemu-user-static` has no apt candidate; confirm `qemu-user` + `binfmt` plus ALARM tarball chroot works. Phase 2.
- **Q10** PanVK Vulkan on G52: is it experimental/disabled by default? Doc does not specify per GPU; test with `vulkaninfo` after boot. Not on the critical path.
- **Q11** Waydroid RAM: device RAM size not confirmed from a source I read (plorpOS Brick doc says 975 MB for that device, irrelevant). Check `/proc/meminfo`.
- **Q12** GammaOS slide-sensor behaviors and governor tables: not in the public repo; not recoverable without reading the firmware images. Skip, no value over our own design.
