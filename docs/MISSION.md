The verbatim mission brief is at /home/dakoda/.hermes/attachments/Pasted content (5.5 KB). Read it first.

# Rotate OS: shared context for every lane

Device: Anbernic RG Rotate. SoC Unisoc T618 (UMS512 / sharkl5pro). 2x A75 + 6x A55 cores, Mali-G52 GPU. 720x720 swivel touch panel, eMMC + microSD, Marlin3 Lite Wi-Fi/BT.
Goal: minimal Arch Linux ARM (aarch64) CFW called "Rotate OS". Priorities, in order: (1) game performance, (2) lowest battery drain, (3) games-first swivel UX, (4) optional Waydroid.

Project root: /home/dakoda/Projects/rotate-os (durable; not scratch)
- docs/research/   research reports (Phase 1)
- upstream/        cloned upstream repos (read-only references)
- build/           reproducible image build scripts (Phase 2)
- frontend/        swivel-aware menu (Phase 5)
- qemu/            QEMU aarch64 virt test harness

Upstream repos:
- beebono/rg-rotate-linux (DEVICE-BRINGUP.md; submodules linux-mainline-sprd, u-boot-ums512, ums512_spl)
- thwonp/base618 (Armbian base; deep sleep docs/deep-sleep.md; first-boot firmware extraction; kernel thwonp/linux-mainline-sprd branch rotate-fixes)
- thwonp/plorpOS (game switcher, resume-on-wake, PGO melonDS, PortMaster)
- beebono/MIROKI (eMMC install, RetroArch 'ramen' swivel menu)
- crackerjacques/ums512_spl + armbian/build PR #10813 (sprd-ums512 family)
- TheGammaSqueeze/GammaOSNext (UX reference only; UnisocBypass FDL files for spd_dump)

Target: crackerjacques SPL + beebono U-Boot, SD-first boot with stock-Android eMMC fallback; kernel from thwonp rotate-fixes trimmed for this board with ANDROID_BINDER_IPC + binderfs; Arch Linux ARM rootfs; Mesa Panfrost; RetroArch on KMS/DRM with no compositor.

Host: Ubuntu 26.04, 16 cores, 14 GB RAM, passwordless sudo, ~680 GB free. No hardware is attached to this host. Do not flash anything and do not run spd_dump. Any step that needs the physical device must be written down as one short, exact instruction for Dakoda instead of performed.

Rules: cite every factual claim with a URL, or a file path plus line number. Mark unverified claims UNVERIFIED. Never invent build output or measurements. Write deliverables to disk as you go, so a crash keeps partial work.
