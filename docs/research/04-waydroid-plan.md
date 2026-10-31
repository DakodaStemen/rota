# 04 – Waydroid on Rotate OS: experiment plan (priority 4, optional)

Accessed 2026-10-31. Kernel config checked: `build/out/kernel/config` (Linux 7.2.9, `CONFIG_LOCALVERSION="-sprd-ums512"`, line 2). Nothing here was run on hardware (no device on the host). Claims without a link or file:line are marked UNVERIFIED.

## Bottom line

- Waydroid is a LXC container running a Lineage 20 (Android 13) arm64 image on the host kernel. Our kernel already has binder+binderfs, PSI, cgroups, namespaces, veth, bridge, nftables NAT, memfd. **Two real gaps and several "check on first boot" gaps** (below).
- GPU: try Mesa Panfrost first (it is the same Mesa as the host); fall back to SwiftShader. Expect software to be unusable for 3D games on this CPU; fine for UI/light apps (UNVERIFIED, no T618 data).
- Nobody has published Waydroid on T618/UMS512 or on this device (queries listed at the end). This is first-of-its-kind; budget for debugging.

## A. Kernel config check against `build/out/kernel/config`

Requirement sources: Waydroid's own debug page says the kernel must support "binder and either ashmem or memfd" (https://docs.waydro.id/debugging/getting-essential-information, primary); Gentoo wiki list: PSI, MEMFD_CREATE, NETFILTER_XT_TARGET_MASQUERADE, NFT_MASQ/NFT_NAT etc. (https://wiki.gentoo.org/wiki/Waydroid, secondary); Arch wiki (https://wiki.archlinux.org/title/Waydroid, secondary); maintainers' answer for Lineage 18.1+: run `lxc-checkconfig`, cgroup v1 controllers, CFS_BANDWIDTH, RT_GROUP_SCHED reported to help (https://github.com/waydroid/waydroid/issues/738, user report, not maintainer). The maintainers have no authoritative "ideal config" page (https://github.com/waydroid/waydroid/issues/811, open question).

| Need | Option | State | Source of need |
|---|---|---|---|
| binder | `ANDROID_BINDER_IPC=y`, `ANDROID_BINDERFS=y`, `ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder,puddlejumper,hwpuddlejumper,vndpuddlejumper"` | OK | Waydroid docs above; also `build/out/kernel/binder.txt` |
| PSI | `PSI=y`, `PSI_DEFAULT_DISABLED` not set | OK (so `/proc/pressure` exists without `psi=1`) | Gentoo; https://github.com/waydroid/waydroid/issues/783 (PSI needed by Android 11+ lmkd) |
| memfd | `MEMFD_CREATE=y`, `TMPFS=y`, `SHMEM=y` | OK | Waydroid docs above |
| ashmem | `ASHMEM` absent (removed from mainline in 5.18, https://github.com/waydroid/waydroid/issues/406) | Not needed: memfd replaces it | docs above; UNVERIFIED that Lineage 20 image has no ashmem hard dependency (Waydroid says "ashmem or memfd") |
| cgroups | `CGROUPS, CGROUP_DEVICE, CGROUP_FREEZER, CGROUP_PIDS, CGROUP_SCHED, CGROUP_CPUACCT, CPUSETS, CGROUP_BPF, FAIR_GROUP_SCHED, BLK_CGROUP, MEMCG` | OK | lxc needs; issue 738 |
| cgroup extras | `CFS_BANDWIDTH` not set; `RT_GROUP_SCHED` not set; `MEMCG_V1` not set | **Gap (low risk)**: set `CFS_BANDWIDTH=y`, `MEMCG_V1=y` for the first attempt if boot fails with cgroup errors (issue 738 reported they helped) | issue 738 (user report) |
| namespaces | `NAMESPACES, USER_NS, PID_NS, NET_NS, IPC_NS, UTS_NS` | OK | LXC |
| seccomp | `SECCOMP, SECCOMP_FILTER` | OK | Waydroid lxc config uses a seccomp profile (https://raw.githubusercontent.com/waydroid/waydroid/main/data/configs/config_3) |
| veth/bridge | `VETH=y, BRIDGE=y, BRIDGE_NETFILTER=y, TUN=y` | OK | config_3: `lxc.net.0.type = veth`, link `waydroid0` |
| NAT | `NF_CONNTRACK, NF_NAT, NF_TABLES, NFT_NAT, NFT_MASQ, NFT_CT, NETFILTER_XT_NAT, NETFILTER_XT_TARGET_MASQUERADE, XT_MATCH_ADDRTYPE/CONNTRACK/STATE/MARK, NF_NAT_MASQUERADE` | OK; Waydroid's nft or iptables-nft path works | Gentoo |
| legacy iptables NAT | `IP_NF_NAT`, `IP_NF_FILTER`, `IP_NF_MANGLE`, `IP_NF_TARGET_MASQUERADE` all **absent**; `NETFILTER_XTABLES_LEGACY` not set (config: `# CONFIG_NETFILTER_XTABLES_LEGACY is not set`) | **Gap if the userspace uses legacy `iptables`**: use `iptables-nft` (Arch default) or nft; otherwise enable `NETFILTER_XTABLES_LEGACY`, `IP_NF_FILTER/NAT/MANGLE` | Gentoo; UNVERIFIED which backend Waydroid 1.6.x picks first |
| policy routing | `IP_ADVANCED_ROUTER` not set, `IP_MULTIPLE_TABLES` absent | **Gap (likely real)**: Android's netd uses `ip rule`/multiple tables inside the container netns; set `IP_ADVANCED_ROUTER=y`, `IP_MULTIPLE_TABLES=y`. UNVERIFIED for Waydroid specifically; rule of Android networking | UNVERIFIED |
| Android netd extras | `NETFILTER_XT_MATCH_QUOTA/OWNER/...`, `NET_CLS_CGROUP` (not set), `CGROUP_NET_PRIO` (not set), `IP_NF_RAW`/`IP6_NF_RAW`, `SW_SYNC` (not set), `UDMABUF` (not set) | Unknown need; add only if logcat/netd complains | UNVERIFIED |
| sync | `SYNC_FILE=y`, `DMABUF_HEAPS=y`, `DMABUF_HEAPS_SYSTEM=y`, `DMABUF_HEAPS_CMA=y` | OK | gralloc/GBM path; UNVERIFIED |
| GPU node | `DRM_PANFROST=m` (module!), `DRM_SPRD=y` | OK but must `modprobe panfrost` before the container; ensure `/dev/dri/renderD128` + `card0` exist (Waydroid mounts both: https://github.com/waydroid/waydroid/pull/676) | |
| input | `INPUT_UINPUT=y`, `INPUT_EVDEV=y` | OK | Waydroid injects input via the compositor, not uinput; fine |
| fs | `EXT4_FS, OVERLAY_FS, SQUASHFS, FUSE_FS` | OK | images are loop-mounted ext4/erofs? Image format UNVERIFIED; `EROFS_FS` is **not set** – add `EROFS_FS=y` if the system image is EROFS (check with `file`) |
| other | `SYSVIPC=y, FHANDLE=y, INOTIFY_USER, EPOLL, EVENTFD, TIMERFD`; `POSIX_MQUEUE` not set; `AUDIT` not set; `SECURITY_SELINUX` absent | `POSIX_MQUEUE` is an lxc-checkconfig item (UNVERIFIED need). SELinux: Waydroid runs with `lxc.apparmor.profile = unconfined`; Android SELinux is handled in-container (UNVERIFIED) | config_3 |

First-boot gap list (apply before building the test kernel): `IP_ADVANCED_ROUTER`, `IP_MULTIPLE_TABLES`, `CFS_BANDWIDTH`, `MEMCG_V1`, `POSIX_MQUEUE`, `EROFS_FS`; optionally legacy xtables. Run `lxc-checkconfig` on-device first; it is the cheapest oracle (issue 738).

Also: kernel `-sprd-ums512` has **no** loadable binder module; binder is built in, so no DKMS (Arch wiki says `rust_binder`/binder_linux DKMS only for kernels lacking it).

## B. GPU: Panfrost vs software

- Waydroid bundles its own Mesa inside the image (images carry mesa; version may lag host). Issue #770: on Panfrost (RockPro64, Mali-T860) with a wlroots compositor the image's Mesa was too old and the container failed; weston worked; setting `gralloc=default`, `egl=swiftshader` in `waydroid_base.prop` also worked (https://github.com/waydroid/waydroid/issues/770, closed). The image Mesa version observed in 2025 was 25.1.6 (https://github.com/waydroid/waydroid/issues/2087), so the old bug is probably gone, but **cage is wlroots-based** → test cage first, weston as fallback (if the result is a failure at start, that is the #770 signature).
- Performance: reported low GL ES performance vs host on Panfrost/Panthor (Mali-G610, https://github.com/waydroid/waydroid/issues/2087, open). Black-screen reports with `ro.hardware.gralloc=minigbm_gbm_mesa` on other hardware (https://github.com/waydroid/waydroid/issues/1445). Use `ro.hardware.gralloc=gbm` and `ro.hardware.egl=mesa` as the starting point (UNVERIFIED for G52).
- Mali-G52: Mesa Panfrost lists G52 as supported; Collabora reports GLES 3.1 conformance on Mali-G52 (https://www.collabora.com/news-and-blog/news-and-events/panfrost-achieves-opengl-es-3.1-conformance-on-mali-g52.html, secondary/vendor blog; I read only the title/snippet). Mesa doc: https://docs.mesa3d.org/drivers/panfrost.html.
- Our GPU state: Panfrost probes `mali-g52 id 0x7402`, `shader_present=0x3` (2 cores); **shader execution via SUBMIT was untested** as of bring-up (`upstream/rg-rotate-linux/DEVICE-BRINGUP.md:37`; `docs/research/01-boot-kernel.md:119`). This is the main blocker: pass a native test (`kmscube`/glmark2-es2-drm or `weston-simple-egl` under Panfrost) **before** touching Waydroid.
- Blockers/unknowns (no T618 data): (1) GPU power domain/clock/devfreq (384-850 MHz) stability under load; (2) no DDR devfreq, memory-bound; (3) Android 13 HWUI wants GLES 3.x and often Vulkan; Panfrost GLES is fine, PanVK on G52 is not production (a community fork exists: https://github.com/LukeValen/panvk-mali-g52; UNVERIFIED state). Waydroid uses GLES through SurfaceFlinger, so Vulkan isn't required for basic operation.
- Software option: SwiftShader inside the image (`persist.waydroid`/`ro.hardware.egl=swiftshader`, `ro.hardware.gralloc=default`; confirmed as a working fallback on Panfrost boards in #770). Expected: UI usable on 8 cores, 3D games unusable (UNVERIFIED, no benchmark found). llvmpipe applies to the host compositor (cage/weston with `WLR_RENDERER=pixman`/pixman), not to the Android side.
- Compositor rendering: set `WLR_RENDERER_ALLOW_SOFTWARE=1` or `WLR_RENDERER=pixman` for cage as fallback (wlroots env vars, UNVERIFIED on this stack).

## C. Compositor: launched on demand, fully gone on exit

- Official pattern: `cage waydroid show-full-ui` as a Wayland session (https://docs.waydro.id/faq/setting-up-waydroid-only-sessions, primary), weston alternative on the same page.
- Our design (RetroArch on KMS/DRM with no compositor is the normal state):
  1. Frontend `exec`s a launcher script as a systemd service (`rotate-waydroid.service`), after stopping RetroArch/other DRM masters (only one DRM master).
  2. Script: `modprobe panfrost`; `systemctl start waydroid-container`; `cage -- waydroid show-full-ui` (cage runs as that process's wrapper; when the client exits cage exits).
  3. Exit path (single function, used for button-combo and normal exit): `waydroid session stop` → `systemctl stop waydroid-container` → wait for `lxc-info -n waydroid` = STOPPED → kill cage if alive → `modprobe -r` nothing (panfrost stays) → return to frontend.
  4. Pass check: no `cage|weston|waydroid|lxc-start|dnsmasq` processes remain (`pgrep -a`), `waydroid0` bridge deleted, wakelocks/clk usage back to baseline (Test 5).
- Power: set `persist.waydroid.suspend true` (default on kernel ≥4.9, https://docs.waydro.id/usage/waydroid-prop-options) so the container freezes when idle; but Waydroid keeps the CPU awake otherwise: stop it fully before system suspend (https://github.com/waydroid/waydroid/issues/168, PinePhone suspend interference; also noted in `docs/research/02-power-gaming-ux.md:222`).
- Display: panel is 720×720 square; Waydroid prop `persist.waydroid.width/height` (docs link above) to 720×720; Android apps expecting landscape/portrait will letterbox or rotate. UNVERIFIED behaviour.
- Cage vs weston: cage = one fullscreen client, smaller, wlroots (#770 risk); weston is bigger (≈ tens of MB UNVERIFIED) but known-good with Panfrost in #770. Start with cage; keep weston `kiosk-shell` as the alternative.

## D. Image: LineageOS arm64, size, storage

From the Waydroid OTA JSON (primary, fetched 2026-10-31):
- System VANILLA `lineage-20.0-20260927-VANILLA-waydroid_arm64-system.zip`: 912,267,974 B (≈870 MiB) – https://ota.waydro.id/system/lineage/waydroid_arm64/VANILLA.json
- System GAPPS (same date): 1,326,476,767 B (≈1.24 GiB) – https://ota.waydro.id/system/lineage/waydroid_arm64/GAPPS.json
- Vendor MAINLINE `lineage-20.0-20261022-MAINLINE-waydroid_arm64-vendor.zip`: 147,676,229 B (≈141 MiB) – https://ota.waydro.id/vendor/waydroid_arm64/MAINLINE.json
- Unzipped system.img will be larger (UNVERIFIED; plan for ~2-3 GB with VANILLA). Userdata grows with installed apps. Budget 8 GB minimum on SD. `waydroid_arm64_only` variant exists (https://sourceforge.net/projects/waydroid/files/images/system/lineage/waydroid_arm64_only/) – pick it if apps are arm64-only; it avoids 32-bit libs (UNVERIFIED size difference).
- Architecture: arm64 only on this SoC (A75/A55 are ARMv8.2 and support AArch32 EL0, and `CONFIG_COMPAT=y` is set (config), but whether the T618 cores' AArch32 EL0 is enabled by firmware is UNVERIFIED; use the arm64-only image if 32-bit apps are not needed).
- Storage: images default to `/var/lib/waydroid/images` (`images_path` in waydroid.cfg, seen in https://github.com/waydroid/waydroid/issues/1445). Put the whole `/var/lib/waydroid` on an ext4 partition on the SD card (bind mount / separate partition, F2FS optional) – never on eMMC (the eMMC holds the stock Android fallback). Verify `mount_overlays` setting and that the SD is ext4 with `noatime`. Use `waydroid init -s VANILLA` or `-i /path/to/images` after copying images manually (https://docs.waydro.id/faq/using-custom-waydroid-images).
- Install route on Arch Linux ARM: UNVERIFIED availability of a `waydroid` package for aarch64 in ALARM repos / AUR build; plan to build from AUR (python, lxc, dnsmasq, nftables).

## E. Step-by-step test procedure (pass/fail)

Pre-req: the Phase-3 baseline: record idle SD-boot current with the screen at the standard brightness (power meter on the charger path or `current_now` average 60 s). Call it **I0**.

1. **Kernel gate** – `zcat /proc/config.gz` or `lxc-checkconfig`; `grep -E 'BINDER|PSI|MEMFD|IP_MULTIPLE_TABLES' config`; `cat /proc/pressure/memory`; `modprobe panfrost && ls /dev/dri`. Pass: binder + PSI + renderD128 present, no "missing" items except accepted.
2. **GPU gate** – native GLES test under Panfrost (kmscube or `weston-simple-egl` in a temporary weston). Pass: renders for 60 s without GPU fault in `dmesg` (`panfrost ... fault`), ≥ 30 fps on a simple scene. Fail → stop; Waydroid cannot use HW GPU; test SwiftShader path only.
3. **Container start** – `waydroid init -s VANILLA -f` (images on SD), `systemctl start waydroid-container`, `waydroid status`. Pass: `Container: RUNNING`, `lxc-info` RUNNING, `ls /dev/binderfs` or the binder devices exist, `waydroid shell getprop sys.boot_completed` → 1 within 120 s. Fail logs: `waydroid log`, `waydroid logcat`.
4. **SurfaceFlinger renders** – `cage -- waydroid show-full-ui` (or weston). Pass: the Lineage launcher is visible on the 720×720 panel; `waydroid shell dumpsys SurfaceFlinger | grep -i 'GLES\|EGL'` shows Mesa/Panfrost (not SwiftShader/"Android" software); no black screen. Record boot-to-launcher time.
5. **App runs** – `waydroid app install <apk>` (e.g. an F-Droid game) / launch `waydroid app launch <pkg>`. Pass: app draws, runs ≥ 5 min, ≥ 30 fps on a 2D game, no kernel oops.
6. **Input works** – touchscreen taps navigate; D-pad/buttons (gamepad is evdev on the host; Waydroid receives keyboard/pointer from the compositor, gamepad pass-through UNVERIFIED – may need `persist.waydroid.uevent true`, https://docs.waydro.id/usage/waydroid-prop-options, plus udev events, or a uinput bridge). Pass: touch + at least back/home mapped; gamepad pass-through = stretch goal.
7. **Network** – in container `ping 1.1.1.1`, DNS; Pass if Wi-Fi host → NAT → container works. Fail → check the gaps (legacy iptables / ip rule).
8. **Shutdown and power** – exit via the frontend path (C.3). Pass: no waydroid/lxc/cage processes, `waydroid0` gone, `lxc-info` STOPPED, GPU runtime-suspended (`cat /sys/class/devfreq/*gpu*/cur_freq` back to lowest / `power/runtime_status`=suspended; UNVERIFIED path), idle current within **±3 %** of I0 after 2 min settle, and a successful `echo mem` suspend/resume cycle (the container must not block suspend).
9. **Soak** – 30 min app + exit + repeat 3×; no memory leak (`free -m` returns to baseline ±50 MB), no thermal trip (see 03-cpufreq).

Each step: one device-side instruction for Dakoda: "SD-boot, `sudo /opt/rotate/waydroid-test.sh N`, copy `/var/log/waydroid-test-N.txt`" (script to be written in the build phase; not created now).

## F. Known blockers and reports (Panfrost / Mali-G52)

- Old-Mesa-in-image vs wlroots compositor failure on Panfrost: https://github.com/waydroid/waydroid/issues/770 (closed).
- Low GLES performance on Panfrost/Panthor in Waydroid: https://github.com/waydroid/waydroid/issues/2087 (open, Mali-G610 not G52).
- gralloc `minigbm_gbm_mesa` black screen: https://github.com/waydroid/waydroid/issues/1445 (open, AMD, but signals the setting is fragile).
- Corrupted display on a GPU with HW render, worked with software rendering: https://github.com/waydroid/waydroid/issues/356 (closed, device in issue not G52).
- Redroid on Mali-G52 (RK3568) with Panfrost kernel module: container boots, `/dev/dri/renderD128` works, "hardware acceleration (Panfrost) in progress" – not a Waydroid report, no result: https://github.com/1yeshen/redroid-mali-g52 (README fetched via curl).
- Our own: untested shader submission (see B); panfrost is `=m`; no DDR devfreq; suspend interaction.

## G. Prior reports on T618 (fresh searches, 2026-10-31)

Result: **none found**. Queries (web search tool):
1. `waydroid Unisoc T618 OR UMS512 OR "Mali-G52" mainline`
2. `"waydroid" "T618"`
3. `waydroid Retroid Pocket 3+ OR "Unisoc" mainline linux` (hit: Seriousattempts/rp3plus-native-attempts, a Retroid RP3+ UMS512 Linux attempts list; page text fetched has no Waydroid mention)
4. `waydroid unisoc ums512 OR sc9863a OR t606 OR t618 linux mainline panfrost`
5. `waydroid anbernic OR retroid OR "rg rotate" mainline linux`
6. `Anbernic RG Rotate Waydroid`
7. `waydroid mali-g52 panfrost` (only RK3568 redroid, not Waydroid)
8. `Unisoc T618 Panfrost mesa mainline Mali-G52 MP2 upstream kernel`
9. `waydroid mainline postmarketOS panfrost works Mali` and `waydroid panfrost black screen OR crash OR "SIGSEGV" Bifrost` (second returned off-topic news)
Absence of results is not proof; the search index may miss forum/Telegram/Matrix posts.

## H. Decision

- Keep Waydroid as a **gated experiment**: first kernel gaps A, then Panfrost shader gate (B/E2). If E2 fails, do not continue to E3+ (software rendering is not worth the storage/battery cost for a games-first device).
- Hand-offs: kernel config deltas → `build` lane for a test config; frontend launch/exit scripts → Phase 5 frontend; device runs → Dakoda.
