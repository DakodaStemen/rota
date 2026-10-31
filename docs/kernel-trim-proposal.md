# Kernel config trim proposal: RG Rotate (ums512_defconfig, Linux 7.2.9)

Author lane: codebase (internal research, read-only). Date: 2026-10-31.
Tree analysed: `/home/dakoda/Projects/rotate-os/upstream/linux-mainline-sprd` @ `fedeb60b6` (thwonp `rotate-fixes`), not modified. Validation ran in a throw-away worktree (removed).

Artifacts:
- Fragment: `/home/dakoda/Projects/rotate-os/docs/kernel-trim.config` (374 lines of settings)
- Evidence: `/home/dakoda/Projects/rotate-os/docs/kernel-trim-evidence/` (`base.config`, `trimmed.config`, `merge_config.log`, `dtb-enabled-compatibles.txt`, `removed-symbols.txt`)

## 1. Summary

- **Merge result:** `ums512_defconfig` + fragment through `scripts/kconfig/merge_config.sh`: every one of the 374 requested symbols stuck (0 warnings that concern a fragment symbol). `olddefconfig` is idempotent afterwards. Built-in options `=y`: **2029 -> 1484**; modules `=m`: **155 -> 18**. Not measured: image size, boot time, power, frame time (no build, no hardware).
- **Three findings in the existing tree (code vs docs):**
  1. `CONFIG_DMABUF_HEAPS_SYSTEM=y` and `_CMA=y` (defconfig:769-770) never reach `.config`: the parent `DMABUF_HEAPS` is missing. Verified by diffing defconfig lines against the generated `.config` (only these two plus `CRYPTO_DRBG_MENU` fail).
  2. `INPUT_UINPUT` is off; PortMaster/InputPlumber need it. `ANDROID_BINDERFS` is off (as research 01 said).
  3. `DRM_SPRD` calls `drm_client_setup()` (`drivers/gpu/drm/sprd/sprd_drm.c:169`) but does not `select DRM_CLIENT_SELECTION` (`drivers/gpu/drm/sprd/Kconfig:3-11`). It only works today because `DRM_HDLCD`/`DRM_KOMEDA`/`DRM_MALI_DISPLAY` (all useless here) select it (`drivers/gpu/drm/arm/Kconfig:9,32`, `arm/display/Kconfig:6`). Trimming those without a keep removes fbdev emulation and fbcon (reproduced in the first validation pass). The fragment keeps `DRM_HDLCD=y` as a workaround; proper fix is a one-line Kconfig patch (handoff below).
- **Important caveat on "trim":** most dead-weight entries are `=m` or never-bound `=y` drivers. Unbound `=y` drivers cost image size and boot probe/initcall time, not steady-state power. The real runtime/power wins are in sections 3.2 and 3.7; do not expect the symbol count reduction to translate into FPS.

## 2. What the RG Rotate DTS actually binds

Method: compiled `ums512-rg-rotate.dtb` in the worktree (`make ... sprd/ums512-rg-rotate.dtb`, includes `ums512.dtsi`, `sc2730.dtsi`, `sprd-sound-fe-be.dtsi`: `ums512-rg-rotate.dts` lines 1-12), decompiled with `dtc`, took every node with a `compatible` and no `status = "disabled"`: 166 nodes with a compatible, 158 enabled (`dtb-enabled-compatibles.txt`). Compatible -> source file by `grep` over `drivers/ sound/ kernel/`, source file -> symbol by the directory Makefile. Final column is the value after the fragment.

| Function | Compatible(s) | Source | Symbol(s) = final value |
|---|---|---|---|
| CPU / timer / idle | arm,cortex-a55/a75, arm,armv8-timer, arm,psci-0.2, arm,idle-state | drivers/clocksource/arm_arch_timer.c, drivers/firmware/psci/psci.c, drivers/cpuidle/cpuidle-psci.c | `ARM_ARCH_TIMER`=y, `ARM_PSCI_FW`=y, `ARM_PSCI_CPUIDLE`=y |
| PMU | arm,cortex-a55-pmu / a75-pmu | drivers/perf/arm_pmuv3.c | `ARM_PMUV3`=y |
| Interrupts | arm,gic-v3 | (arm64 core) | `ARM64`=y |
| SoC clocks | sprd,ums512-*-gate / -pll / -clk (13 nodes) | drivers/clk/sprd/ums512-clk.c | `SPRD_UMS512_CLK`=y |
| Power domains | sprd,ums512-power-controller | drivers/pmdomain/sprd/pm-domains.c | `SPRD_PM_DOMAINS`=y |
| CM4 power coprocessor | sprd,ums512-pmsys | drivers/remoteproc/sprd_pmsys.c | `SPRD_PMSYS_REMOTEPROC`=y, `REMOTEPROC`=y |
| Audio DSP boot | sprd,ums512-audcp-boot | drivers/soc/sprd/sprd-audcp-boot.c | `SPRD_AUDCP_BOOT`=y |
| Mailbox / hwspinlock / DMA | sprd,ums512-mailbox, sprd,hwspinlock-r3p0, sprd,sharkl5pro-dma | drivers/mailbox/sprd-mailbox.c, drivers/hwspinlock/sprd_hwspinlock.c, drivers/dma/sprd-dma.c | `SPRD_MBOX`=y, `HWSPINLOCK_SPRD`=y, `SPRD_DMA`=y, `RPMSG_SPRD_SIPC`=y |
| IOMMU | sprd,ums512-iommu | drivers/iommu/sprd-iommu.c | `SPRD_IOMMU`=y |
| Timers | sprd,sharkl5pro-timer / -suspend-timer | drivers/clocksource/timer-sprd.c | `SPRD_TIMER`=y |
| cpufreq / DVFS | sprd,hardware-cpufreq, sprd,sharkl5pro-cpudvfs, sprd,topdvfs-dev | drivers/cpufreq/sprd-hwdvfs-cpufreq.c, sprd-hwdvfs-normal.c, sprd-top-dvfs.c | `ARM_SPRD_HW_CPUFREQ_NORMAL`=y, `ARM_SPRD_HW_CPUFREQ_ARCH_UMS512`=y, `ARM_SPRD_TOP_DVFS`=y, `ARM_SPRD_CPUFREQ_PUBLIC`=y |
| Thermal | sprd,ums512-thermal x3 | drivers/thermal/sprd_thermal.c | `SPRD_THERMAL`=y |
| PMIC core | sprd,sc2730 on sprd,ums512-adi | drivers/mfd/sprd-sc27xx-spi.c, drivers/spi/spi-sprd-adi.c | `MFD_SC27XX_PMIC`=y, `SPI_SPRD_ADI`=y |
| PMIC subdevices | sc2730-rtc/-wdt/-bltc/-eic/-adc/-efuse/-vibrator | rtc-sc27xx.c, sc27xx_wdt.c, leds-sc27xx-bltc.c, gpio-pmic-eic-sprd.c, sc27xx_adc.c, sc27xx-efuse.c, sc27xx-vibra.c | `RTC_DRV_SC27XX`=y, `SC27XX_WATCHDOG`=y, `LEDS_SC27XX_BLTC`=y, `GPIO_PMIC_EIC_SPRD`=y, `SC27XX_ADC`=y, `NVMEM_SC27XX_EFUSE`=y, `INPUT_SC27XX_VIBRA`=y, `REGULATOR_SC2730`=y, `POWER_RESET_SC27XX`=y |
| Charging | awinic,aw32257 (bq2415x), sprd,sc2730-fgu, simple-battery | drivers/power/supply/bq2415x_charger.c, sc27xx_fuel_gauge.c | `CHARGER_BQ2415X`=y, `FUEL_GAUGE_SC27XX`=y, `POWER_SUPPLY`=y |
| USB Type-C / PD | sprd,sc2730-pd, usb-c-connector | drivers/usb/typec/tcpm/sc27xx_pd.c | `TYPEC`=y, `TYPEC_TCPM`=y, `TYPEC_SC27XX_PD`=y |
| USB controller + PHY | sprd,ums512-musb, sprd,ums512-hsphy | drivers/usb/musb/sprd.c, drivers/phy/phy-sprd-usb2.c | `USB_MUSB_HDRC`=y, `USB_MUSB_SPRD`=y, `PHY_SPRD_USB2`=y, `USB_OTG`=y, `NOP_USB_XCEIV`=y, `USB_CONN_GPIO`=y |
| SoC watchdog / eFuse / EIC / GPIO / pinctrl / PWM / I2C | sprd,ums512-wdt, sprd,ums312-efuse, sprd,sc9860-eic-*, sprd,ums512-gpio, -pinctrl, -pwm, sprd,ums512-i2c, sprd,sharkl5pro-hw-i2c | drivers/watchdog/sprd_wdt.c, nvmem/sprd-efuse.c, gpio/gpio-eic-sprd.c, gpio-sprd.c, pinctrl/sprd/pinctrl-sprd-ums512.c, pwm/pwm-sprd.c, i2c/busses/i2c-sprd.c, i2c-sprd-hw.c | `SPRD_WATCHDOG`=y, `NVMEM_SPRD_EFUSE`=y, `GPIO_EIC_SPRD`=y, `GPIO_SPRD`=y, `PINCTRL_SPRD_UMS512`=y, `PWM_SPRD`=y, `I2C_SPRD`=y, `I2C_SPRD_HW`=y |
| SD / eMMC | sprd,sdhci-r11 x3 | drivers/mmc/host/sdhci-sprd.c | `MMC_SDHCI_SPRD`=y, `MMC_SDHCI_PLTFM`=y, `MMC_CQHCI`=y |
| Display | sprd,display-subsystem, sprd,ums512-dpu, sprd,ums512-dsi-host, rocknix,generic-dsi, pwm-backlight | drivers/gpu/drm/sprd/*, panel-generic-dsi.c, pwm_bl.c | `DRM_SPRD`=y, `DRM_PANEL_GENERIC_DSI`=y, `BACKLIGHT_PWM`=y, `DRM_FBDEV_EMULATION`=y, `FRAMEBUFFER_CONSOLE`=y, `DRM_CLIENT_SELECTION`=y |
| GPU | sprd,ums512-mali / arm,mali-bifrost | drivers/gpu/drm/panfrost/panfrost_drv.c | `DRM_PANFROST`=m, `DRM_SCHED`=m, `DRM_GEM_SHMEM_HELPER`=m, `PM_DEVFREQ`=y |
| Touch | goodix,gt911 (i2c3 0x14) | drivers/input/touchscreen/goodix.c | `TOUCHSCREEN_GOODIX`=y |
| Buttons + hall switch (SW_LID) | gpio-keys x2 | drivers/input/keyboard/gpio_keys.c | `KEYBOARD_GPIO`=y, `INPUT_EVDEV`=y, `INPUT_JOYDEV`=y |
| Audio | unisoc,vbc-v4-codec-sc2730, sc2730-audio-codec, anbernic,rgds-amp | sound/soc/sprd/vendor/*, sound/soc/codecs/aw87390.c | `SND_SOC_UNISOC_VBC_V4_SC2730`=y, `SND_SOC_AW87390`=y, `SND_SOC_SPRD`=y |
| Wi-Fi | sprd,sc2355-sdio-wifi | drivers/net/wireless/unisoc/* | `SC23XX_SDIO`=y, `SC23XX`=y, `SC2355`=y, `CFG80211`=y, `MAC80211`=y |
| Marlin3 / BT | unisoc,marlin3lite_sdio, sprd,mtty | drivers/net/wireless/sprdwcn/*, drivers/bluetooth/btsprd_hci.c | `UNISOC_WCN_BSP`=y, `SDIOHAL`=y, `WCN_SLP`=y, `SPRD_WCN_REMOTEPROC`=y, `BT`=y, `BT_SPRDSDIO`=y, `BT_HCIUART`=y |
| Crash log | ramoops | fs/pstore/ram.c | `PSTORE`=y, `PSTORE_RAM`=y |

Disabled in the DTS (drivers are dead weight unless re-enabled): `sprd,shub` (rpmsg@0, status disabled), second DMA controller, `unisoc,i2s`, two `sprd,ums512-uart` nodes, i2c@300000/@400000, `unisoc,i2s-null-codec`.
Note `arm,coresight-*` nodes (funnels, ETM, TMC) are enabled in the DTS but the `CORESIGHT` drivers are `=m` debug-only; the fragment disables them (nodes simply stay unbound).

Not verified: the compatible->symbol mapping for the vendor sound stack (`sound/soc/sprd/vendor/Makefile`, one gate `SND_SOC_UNISOC_VBC_V4_SC2730`, `sound/soc/sprd/Makefile:14`) was taken from the Kconfig gate, not per-file; Wi-Fi and WCN sub-options (`SC23XX_CORE`, `SC23XX_SIPC`) were left untouched.

Magnitude caveat: statements such as 'lz4 is faster than zstd', 'stack protector ~1%' or 'frame pointers cost 1-2%' are general kernel experience, **not measured on this device (UNVERIFIED)**. Sections 5 and 9 list what to measure.

## 3. Recommended changes (symbol, from -> to, justification)

`from` is the value in the config generated from `ums512_defconfig` (`unset` = symbol not present/visible). `defconfig:N` cites `arch/arm64/configs/ums512_defconfig`. 'n' means `# CONFIG_X is not set`. Rows marked KEEP/y that equal `from` are guard lines so a later trim cannot remove them.

### 1. Add: needed but missing or not sticking

| Symbol | From | To | Justification |
|---|---|---|---|
| `ANDROID_BINDERFS` | n | y | Waydroid binderfs; depends on ANDROID_BINDER_IPC, default n (drivers/android/Kconfig:30-33), absent from defconfig |
| `INPUT_UINPUT` | n | y | PortMaster/InputPlumber virtual pads need /dev/uinput; '# CONFIG_INPUT_UINPUT is not set' in the built .config |
| `SQUASHFS_ZSTD` | n | y | PortMaster/ports squashfs images; defconfig only has ZLIB(default)+LZ4 (defconfig:727-729) |
| `SQUASHFS_XZ` | n | y | same; xz is mksquashfs's common choice |
| `DMABUF_HEAPS` | n | y | defconfig:769-770 (DMABUF_HEAPS_SYSTEM/CMA) do NOT stick without this parent; verified, they are absent from the generated .config |
| `DMABUF_HEAPS_SYSTEM` | unset | y | dma-buf heap for zero-copy buffer sharing (Waydroid gralloc, Mesa) |
| `DMABUF_HEAPS_CMA` | unset | y | CMA heap; DTS reserves linux,cma |
| `ARM_PMU` | n | y | DTS has pmu-a55/pmu-a75 nodes (compat arm,cortex-a55-pmu); ARM_PMUV3 is unset in .config so they never bind; needed for perf and the cpufreq cycle-counter test (research 02, Q1) |

### 2. Game performance: scheduler, preemption, idle, memory

| Symbol | From | To | Justification |
|---|---|---|---|
| `PREEMPT` | y | n | leave the full-PREEMPT choice (defconfig:9) |
| `PREEMPT_LAZY` | n | y | full-preempt latency for RT/audio threads, voluntary-like throughput for fair tasks; ARCH_HAS_PREEMPT_LAZY=y on arm64 (kernel/Kconfig.preempt:78-82). Keep HZ=250: lazy resched for fair tasks waits for the tick, so HZ_100 would add up to 10 ms |
| `PREEMPT_DYNAMIC` | n | y | boot-time A/B of preempt=lazy/full/voluntary without rebuild (needed to measure game perf); also keeps PREEMPT_RCU, which PREEMPT_LAZY alone drops (kernel/rcu/Kconfig:19-21 defaults PREEMPT_RCU only for PREEMPT/RT/DYNAMIC) |
| `UCLAMP_TASK` | n | y | lets the game launcher boost/cap utilisation per cgroup for the performance/powersave profiles (init/Kconfig:887, needs schedutil) |
| `UCLAMP_TASK_GROUP` | unset | y | per-cgroup uclamp via cpu.uclamp.min/max |
| `SCHED_SMT` | y | n | no SMT in A55/A75; dead code in the load balancer |
| `CPU_IDLE_GOV_TEO` | n | y | tickless-oriented governor (drivers/cpuidle/Kconfig:27); fewer wrong deep-state picks than menu on NO_HZ_IDLE; Armbian also uses TEO (research 01 section 8) |
| `CPU_IDLE_GOV_MENU` | y | n | replaced by TEO (currently the only governor, .config) |
| `TRANSPARENT_HUGEPAGE` | y | n | THP always (.config) adds compaction/latency spikes and RAM bloat on a small-RAM handheld (defconfig:77) |
| `KSM` | y | n | no VM dedup use; ksmd wakes CPU (defconfig:75) |
| `NUMA` | y | n | single node SoC (defconfig:45) |
| `NUMA_BALANCING` | y | n | pure overhead without NUMA (defconfig:19) |
| `MEMORY_HOTPLUG` | y | n | no hot-pluggable memory (defconfig:73) |
| `MEMORY_HOTREMOVE` | y | n | same (defconfig:74) |
| `MEMORY_FAILURE` | y | n | no RAS hardware on this SoC (defconfig:76) |
| `ZRAM_DEF_COMP_LZ4` | n | y | lz4 compresses/decompresses ~2-3x faster than zstd on A55; ratio matters less than CPU time while gaming (defconfig:285 is zstd). ZRAM_BACKEND_LZ4 already y (defconfig:283) |
| `ZRAM_DEF_COMP_ZSTD` | y | n | swap default to lz4; zstd stays selectable at runtime |
| `LRU_GEN` | n | y | multi-gen LRU: better reclaim under memory pressure from Waydroid/emulator working sets |
| `LRU_GEN_ENABLED` | unset | y | enable at boot |

### 3. cpufreq governors

Keep schedutil (default), performance, powersave - the three profile governors. EAS stays enabled: ENERGY_MODEL=y (defconfig:51), per-CPU capacity-dmips-mhz and dynamic-power-coefficient are in ums512.dtsi:173-174 (A55: 544/102) and :329-330 (A75: 1024/501), and the cpufreq driver registers the EM (drivers/cpufreq/sprd-hwdvfs-cpufreq.c:797). EAS only runs under schedutil, so profiles must switch governor via sysfs, not the other way round. UNVERIFIED on device: thwonp's DT comment says the clocks do not follow requests (research 02, Q1).

| Symbol | From | To | Justification |
|---|---|---|---|
| `CPU_FREQ_GOV_ONDEMAND` | y | n | not a profile governor (defconfig:58) |
| `CPU_FREQ_GOV_CONSERVATIVE` | m | n | same (defconfig:61) |
| `CPU_FREQ_GOV_USERSPACE` | m | n | same (defconfig:57) |
| `ARM_SCPI_CPUFREQ` | y | n | SCPI firmware cpufreq; DTS uses sprd,hardware-cpufreq (defconfig:63) |
| `ARM_SCMI_CPUFREQ` | y | n | same, SCMI (defconfig:64) |

### 4. Other SoCs, buses and firmware interfaces never described by the DTS

The enabled-node compatible list was extracted from the compiled ums512-rg-rotate.dtb (see proposal section 2); none of these have a matching node.

| Symbol | From | To | Justification |
|---|---|---|---|
| `XEN` | y | n | Xen guest support on a bare-metal handheld (defconfig:46); drags ~20 XEN_* symbols |
| `KVM` | y | n | no virtualisation use; Waydroid is LXC, not a VM (defconfig:70) |
| `VIRTUALIZATION` | y | n | menu parent of KVM (defconfig:69) |
| `VFIO` | y | n | no passthrough (defconfig:634) |
| `ARM_SCMI_PROTOCOL` | y | n | no SCMI node in the DTS (defconfig:274) |
| `ARM_SCPI_PROTOCOL` | y | n | no SCPI node (defconfig:276) |
| `IMX_SCMI_BBM_EXT` | y | n | NXP i.MX only (defconfig:275) |
| `KEYBOARD_IMX_BBM_SCMI` | y | n | NXP i.MX only (defconfig:351) |
| `COMMON_CLK_SCMI` | y | n | (defconfig:641) |
| `COMMON_CLK_SCPI` | y | n | (defconfig:642) |
| `SENSORS_ARM_SCMI` | y | n | (defconfig:418) |
| `SENSORS_ARM_SCPI` | y | n | (defconfig:419) |
| `VEXPRESS_CONFIG` | y | n | ARM Versatile Express (defconfig:272) |
| `CLK_VEXPRESS_OSC` | y | n | (defconfig:640) |
| `COMMON_CLK_CS2000_CP` | y | n | external clock chip not on board (defconfig:643) |
| `COMMON_CLK_XGENE` | y | n | APM X-Gene (defconfig:644) |
| `COMMON_CLK_RS9_PCIE` | y | n | PCIe clock gen, board has no PCIe (defconfig:646) |
| `COMMON_CLK_VC3` | y | n | (defconfig:647) |
| `COMMON_CLK_VC5` | y | n | (defconfig:648) |
| `MHI_BUS` | y | n | PCIe modem bus (defconfig:273) |
| `MHI_NET` | y | n | (defconfig:314) |
| `ARM_SMMU` | y | n | board uses sprd,iommu-v1 (SPRD_IOMMU), not ARM SMMU (defconfig:655) |
| `ARM_SMMU_V3` | y | n | same (defconfig:656) |
| `IOMMU_IO_PGTABLE_DART` | y | n | Apple DART (defconfig:654) |
| `SERIAL_AMBA_PL011` | y | n | no UART enabled in DTS: serial@0/@100000 are status=disabled; console is USB ttyGS0 (defconfig:359) |
| `SERIAL_AMBA_PL011_CONSOLE` | y | n | (defconfig:360) |
| `SERIAL_XILINX_PS_UART` | y | n | Xilinx (defconfig:361) |
| `SERIAL_XILINX_PS_UART_CONSOLE` | y | n | (defconfig:362) |
| `SERIAL_FSL_LPUART` | y | n | NXP (defconfig:363) |
| `SERIAL_FSL_LPUART_CONSOLE` | y | n | (defconfig:364) |
| `SERIAL_FSL_LINFLEXUART` | y | n | NXP (defconfig:365) |
| `SERIAL_FSL_LINFLEXUART_CONSOLE` | y | n | (defconfig:366) |
| `SERIO_AMBAKMI` | y | n | PL050 KMI (defconfig:357) |
| `SERIAL_SPRD_CONSOLE` | y | n | UART nodes disabled in DTS; console on USB gadget. Driver itself stays (SERIAL_SPRD) in case a UART is enabled for debug (defconfig:368) |
| `IPMI_HANDLER` | y | n | server BMC (defconfig:373) |
| `IPMI_DEVICE_INTERFACE` | y | n | (defconfig:374) |
| `IPMI_SI` | y | n | (defconfig:375) |
| `TCG_TPM` | y | n | no TPM on the board (defconfig:378) |
| `TEE` | y | n | OP-TEE is not described in the DTS; Unisoc secure world is reached via PSCI/SMC only (defconfig:705) |
| `OPTEE` | y | n | (defconfig:706) |
| `STM` | y | n | System Trace Module, debug only (defconfig:704) |
| `CORESIGHT` | m | n | debug-only tracing (=m, no runtime cost, trim for rootfs size) (defconfig:782) |
| `GOOGLE_FIRMWARE` | y | n | coreboot/Chromebook (defconfig:277) |
| `GNSS` | y | n | no GNSS node (defconfig:280) |
| `ATA` | y | n | no SATA (defconfig:297) |
| `SCSI_HISI_SAS` | y | n | HiSilicon server SAS (defconfig:296) |
| `SCSI_SAS_ATA` | y | n | (defconfig:295) |
| `SCSI_UFSHCD` | y | n | board storage is eMMC/SD via sdhci-sprd (defconfig:614) |
| `MD` | y | n | no RAID/LVM; DM_CRYPT only if encrypted rootfs is planned. Dropping MD drops DM_* too (defconfig:302-307) |
| `UACCE` | y | n | crypto accelerator framework (defconfig:292) |
| `EEPROM_AT24` | m | n | no EEPROM node (defconfig:290) |
| `EEPROM_AT25` | m | n | (defconfig:291) |
| `CRYPTO_DEV_CCREE` | y | n | ARM CryptoCell; not in DTS (defconfig:763) |
| `CRYPTO_DEV_AMLOGIC_GXL` | m | n | Amlogic (defconfig:764) |
| `SPI_SPIDEV` | y | n | no SPI client nodes; the SPI controller is only the ADI PMIC bus (defconfig:390) |
| `TCG_TIS` | y | n | (defconfig:379) |
| `GPIO_MAX732X` | y | n | no such expander in DTS (defconfig:401) |
| `GPIO_PCA953X` | y | n | (defconfig:402) |
| `GPIO_AGGREGATOR` | y | n | (defconfig:405) |
| `MUX_GPIO` | y | n | (defconfig:707) |
| `MUX_MMIO` | y | n | (defconfig:708) |
| `PTP_1588_CLOCK` | y | n | no network PHC (defconfig:391) |
| `VIRTIO_BALLOON` | y | n | not a VM guest (defconfig:635) |
| `HW_RANDOM_VIRTIO` | y | n | (defconfig:377) |
| `VIRTIO_CONSOLE` | y | n | (defconfig:372) |
| `VIRTIO_NET` | y | n | (defconfig:313) |
| `VIRTIO_BLK` | y | n | (defconfig:288) |
| `VIRTIO_MMIO` | y | n | kept only if the QEMU harness must boot THIS kernel; qemu/ should use a generic arm64 virt kernel instead (defconfig:636) |
| `BLK_DEV_NBD` | y | n | (defconfig:287) |
| `SRAM` | y | n | no generic mmio-sram node (defconfig:289) |
| `REALTEK_PHY` | y | n | no Ethernet PHY (defconfig:315) |
| `REALTEK_PHY_HWMON` | y | n | (defconfig:316) |
| `MDIO_BITBANG` | y | n | (defconfig:317) |
| `MDIO_BCM_UNIMAC` | y | n | (defconfig:318) |
| `MDIO_GPIO` | y | n | (defconfig:319) |
| `MDIO_BUS_MUX_MULTIPLEXER` | y | n | (defconfig:320) |
| `MDIO_BUS_MUX_MMIOREG` | y | n | (defconfig:321) |
| `PCS_XPCS` | y | n | (defconfig:322) |
| `NET_DSA` | y | n | Ethernet switch framework (defconfig:215) |
| `WWAN` | y | n | no cellular modem node; Wi-Fi/BT go through the sprdwcn driver, not WWAN (defconfig:342) |
| `NFC` | y | n | no NFC node (defconfig:260) |
| `NET_9P` | y | n | (defconfig:258) |
| `9P_FS` | y | n | (defconfig:739) |
| `NFS_FS` | y | n | no network root (defconfig:735) |
| `ROOT_NFS` | y | n | (defconfig:738) |
| `IP_PNP` | y | n | kernel-level DHCP, root is on SD (defconfig:82) |

### 5. Hardware-specific drivers with no DTS node (mostly =m: zero runtime cost, saves build time and modules size)

| Symbol | From | To | Justification |
|---|---|---|---|
| `DRM_HDLCD` | y | y | KEEP as a workaround: it is the only remaining selector of DRM_CLIENT_SELECTION (drivers/gpu/drm/arm/Kconfig:9); sprd_drm.c:169 calls drm_client_setup() but drivers/gpu/drm/sprd/Kconfig:3-11 does not select it, so without HDLCD the fbdev client/fbcon vanish (validated). Proper fix: add 'select DRM_CLIENT_SELECTION' to DRM_SPRD (kernel patch, coding lane) |
| `DRM_KOMEDA` | y | n | ARM Komeda display, =y, large (defconfig:471) |
| `DRM_MALI_DISPLAY` | y | n | ARM Mali-DP, not the Mali-G52 GPU (defconfig:470) |
| `MEDIA_SUPPORT` | y | n | no camera/radio/SDR/UVC use on a game handheld; removes V4L2 (defconfig:450-460) |
| `VIDEO_IMX219` | m | n | (defconfig:461) |
| `VIDEO_IMX412` | m | n | (defconfig:462) |
| `VIDEO_OV5640` | m | n | (defconfig:463) |
| `VIDEO_OV5645` | m | n | (defconfig:464) |
| `USB_XHCI_HCD` | y | n | host is musb only (USB_MUSB_SPRD); no xhci/ehci/ohci node (defconfig:561) |
| `USB_EHCI_HCD` | y | n | (defconfig:563) |
| `USB_OHCI_HCD` | y | n | (defconfig:566) |
| `USB_SERIAL` | m | n | USB-serial adapters (defconfig:572) |
| `USB_HSIC_USB3503` | y | n | (defconfig:576) |
| `USB_ONBOARD_DEV` | m | n | (defconfig:577) |
| `USB_SNP_UDC_PLAT` | y | n | Synopsys UDC, board is musb (defconfig:582) |
| `USB_BDC_UDC` | y | n | Broadcom UDC (defconfig:583) |
| `TYPEC_FUSB302` | y | n | PD is sc27xx_pd (TYPEC_SC27XX_PD, kept) (defconfig:600) |
| `TYPEC_TCPCI` | y | n | (defconfig:599) |
| `TYPEC_UCSI` | y | n | (defconfig:602) |
| `TYPEC_MUX_FSA4480` | m | n | (defconfig:603) |
| `TYPEC_MUX_GPIO_SBU` | m | n | (defconfig:604) |
| `TYPEC_MUX_NB7VPQ904M` | m | n | (defconfig:605) |
| `TYPEC_MUX_WCD939X_USBSS` | m | n | (defconfig:606) |
| `TYPEC_DP_ALTMODE` | m | n | no DP out (defconfig:607) |
| `USB_PEGASUS` | y | n | USB Ethernet dongles other than RTL8152/3 (kept, the PR enumerated an RTL8153) (defconfig:323) |
| `USB_RTL8150` | y | n | (defconfig:324) |
| `USB_LAN78XX` | y | n | (defconfig:326) |
| `USB_NET_DM9601` | y | n | (defconfig:328) |
| `USB_NET_SR9800` | y | n | (defconfig:329) |
| `USB_NET_SMSC75XX` | y | n | (defconfig:330) |
| `USB_NET_SMSC95XX` | y | n | (defconfig:331) |
| `USB_NET_PLUSB` | y | n | (defconfig:332) |
| `USB_NET_MCS7830` | y | n | (defconfig:333) |
| `USB_CONFIGFS_OBEX` | y | n | gadget functions not needed; serial/ACM/NCM/ECM/mass-storage/FFS kept (defconfig:587) |
| `USB_CONFIGFS_ECM_SUBSET` | y | n | (defconfig:590) |
| `USB_CONFIGFS_RNDIS` | y | n | Windows-only USB net; NCM+ECM cover Linux/macOS (defconfig:591) |
| `USB_CONFIGFS_EEM` | y | n | (defconfig:592) |
| `BT_BNEP` | m | n | Bluetooth PAN not needed (defconfig:241) |
| `BT_HIDP` | m | n | userspace bluez uhid/uinput path handles HID pads; HIDP kernel path is legacy. Re-enable if BT gamepads fail to bind (defconfig:244) - UNVERIFIED |
| `BT_HCIBTUSB` | m | n | board BT is btsprdsdio (sprd,mtty node -> btsprd_hci.c); USB BT dongles optional (defconfig:247) |
| `BT_MSFTEXT` | y | n | (defconfig:245) |
| `BT_AOSPEXT` | y | n | (defconfig:246) |
| `INPUT_MATRIXKMAP` | y | n | no matrix keypad (defconfig:345) |
| `KEYBOARD_ADC` | y | n | stick/ADC path is inert on this SKU (research 01 section 7) (defconfig:348) |
| `KEYBOARD_GPIO_POLLED` | y | n | gpio-keys (interrupt) is used (defconfig:350) |
| `INPUT_PWM_BEEPER` | y | n | (defconfig:354) |
| `INPUT_PWM_VIBRA` | y | n | rumble is sc27xx-vibra (kept) (defconfig:355) |
| `I2C_HID_OF` | m | n | no HID-over-I2C node (defconfig:555) |
| `I2C_HID_OF_ELAN` | m | n | (defconfig:556) |
| `HID_A4TECH` | y | n | legacy vendor HID drivers; generic HID covers USB pads/keyboards (defconfig:541) |
| `HID_APPLE` | y | n | (defconfig:542) |
| `HID_BELKIN` | y | n | (defconfig:543) |
| `HID_CHERRY` | y | n | (defconfig:544) |
| `HID_CHICONY` | y | n | (defconfig:545) |
| `HID_CYPRESS` | y | n | (defconfig:546) |
| `HID_EZKEY` | y | n | (defconfig:547) |
| `HID_ITE` | y | n | (defconfig:548) |
| `HID_KENSINGTON` | y | n | (defconfig:549) |
| `HID_REDRAGON` | y | n | (defconfig:551) |
| `HID_MONTEREY` | y | n | (defconfig:553) |
| `HID_LOGITECH` | m | n | (defconfig:550) |
| `HID_MULTITOUCH` | m | n | touch is Goodix on i2c via the input subsystem, not HID (defconfig:554) |
| `BATTERY_SBS` | m | n | battery is sc27xx FGU (defconfig:412) |
| `BATTERY_BQ27XXX` | y | n | (defconfig:413) |
| `BATTERY_MAX17042` | m | n | (defconfig:414) |
| `CHARGER_MANAGER` | y | n | no charger-manager node; charger is bq2415x (kept) (defconfig:415) |
| `SENSORS_JC42` | m | n | no hwmon chips on the board (defconfig:421) |
| `SENSORS_LM75` | m | n | (defconfig:422) |
| `SENSORS_LM90` | m | n | (defconfig:423) |
| `SENSORS_GPIO_FAN` | y | n | (defconfig:420) |
| `SENSORS_PWM_FAN` | m | n | (defconfig:424) |
| `SENSORS_INA2XX` | m | n | (defconfig:425) |
| `SENSORS_INA3221` | m | n | (defconfig:426) |
| `MAX9611` | m | n | IIO parts not on board (defconfig:677) |
| `TI_ADS1015` | m | n | (defconfig:679) |
| `IIO_ST_LSM6DSX` | m | n | IMU is behind the sensor hub (sprd,shub), not directly on I2C (defconfig:680) |
| `SENSORS_ISL29018` | m | n | (defconfig:688) |
| `VCNL4000` | m | n | (defconfig:689) |
| `IIO_ST_MAGN_3AXIS` | m | n | (defconfig:690) |
| `MPL3115` | m | n | (defconfig:691) |
| `IIO_SPRD_SHUB_MAGN` | y | n | magnetometer/compass/proximity/light have no consumer in a swivel-hall design; the shub node is status=disabled in the DTS (defconfig:685). Keep ACCEL/GYRO until the IMU question (research 01 risk 6) is closed |
| `IIO_SPRD_SHUB_COMPASS` | y | n | (defconfig:686) |
| `IIO_SPRD_SHUB_PROXIMITY` | y | n | (defconfig:687) |
| `IIO_SPRD_SHUB_LIGHT` | y | n | (defconfig:684) |
| `EXTCON_PTN5150` | m | n | (defconfig:672) |
| `EXTCON_USB_GPIO` | y | n | (defconfig:673) |
| `TOUCHSCREEN_GOODIX` | y | y | KEEP - listed so no later trim removes it (DTS touchscreen@14 goodix,gt911) |
| `BT_HCIUART` | y | y | KEEP: BT_SPRDSDIO (btsprd_hci.c, DTS sprd,mtty) is silently dropped if BT_HCIUART goes - proven by the validation run |

### 6. Audio: only the sprd vendor stack + AW87390 are bound

DTS binds unisoc,vbc-v4-codec-sc2730 (sound@0), unisoc,sc2730-audio-codec, anbernic,rgds-amp (audio-codec@58, sound/soc/codecs/aw87390.c). All other codec/card entries are =m or unused.

| Symbol | From | To | Justification |
|---|---|---|---|
| `SND_SOC_FSL_ASRC` | m | n | NXP i.MX (defconfig:485) |
| `SND_SOC_FSL_SAI` | m | n | (defconfig:486) |
| `SND_SOC_FSL_AUDMIX` | m | n | (defconfig:487) |
| `SND_SOC_FSL_SSI` | m | n | (defconfig:488) |
| `SND_SOC_FSL_SPDIF` | m | n | (defconfig:489) |
| `SND_SOC_FSL_ESAI` | m | n | (defconfig:490) |
| `SND_SOC_FSL_MICFIL` | m | n | (defconfig:491) |
| `SND_SOC_FSL_EASRC` | m | n | (defconfig:492) |
| `SND_SOC_IMX_AUDMUX` | m | n | (defconfig:493) |
| `SND_SOC_ADAU7002` | m | n | external codecs not on this board (defconfig:496) |
| `SND_SOC_AK4613` | m | n | (defconfig:497) |
| `SND_SOC_AK4619` | m | n | (defconfig:498) |
| `SND_SOC_DA7213` | m | n | (defconfig:501) |
| `SND_SOC_ES7134` | m | n | (defconfig:503) |
| `SND_SOC_ES7241` | m | n | (defconfig:504) |
| `SND_SOC_ES8316` | m | n | (defconfig:505) |
| `SND_SOC_GTM601` | m | n | (defconfig:506) |
| `SND_SOC_MAX98357A` | m | n | (defconfig:507) |
| `SND_SOC_MAX98927` | m | n | (defconfig:508) |
| `SND_SOC_MAX98390` | m | n | (defconfig:509) |
| `SND_SOC_MSM8916_WCD_DIGITAL` | m | n | Qualcomm (defconfig:510) |
| `SND_SOC_PCM3168A_I2C` | m | n | (defconfig:511) |
| `SND_SOC_RT5640` | m | n | (defconfig:512) |
| `SND_SOC_RT5659` | m | n | (defconfig:513) |
| `SND_SOC_SGTL5000` | m | n | (defconfig:514) |
| `SND_SOC_SIMPLE_MUX` | m | n | (defconfig:516) |
| `SND_SOC_SPDIF` | m | n | (defconfig:517) |
| `SND_SOC_TAS2552` | m | n | (defconfig:518) |
| `SND_SOC_TAS571X` | m | n | (defconfig:519) |
| `SND_SOC_TLV320AIC31XX` | m | n | (defconfig:520) |
| `SND_SOC_TLV320AIC32X4_I2C` | m | n | (defconfig:521) |
| `SND_SOC_TLV320AIC3X_I2C` | m | n | (defconfig:522) |
| `SND_SOC_TS3A227E` | m | n | (defconfig:523) |
| `SND_SOC_WM8524` | m | n | (defconfig:525) |
| `SND_SOC_WM8904` | m | n | (defconfig:526) |
| `SND_SOC_WM8960` | m | n | (defconfig:527) |
| `SND_SOC_WM8962` | m | n | (defconfig:528) |
| `SND_SOC_WM8978` | m | n | (defconfig:529) |
| `SND_SOC_MT6357` | m | n | MediaTek (defconfig:530) |
| `SND_SOC_MT6358` | m | n | (defconfig:531) |
| `SND_SOC_NAU8822` | m | n | (defconfig:532) |
| `SND_SOC_LPASS_WSA_MACRO` | m | n | Qualcomm (defconfig:533) |
| `SND_SOC_LPASS_VA_MACRO` | m | n | (defconfig:534) |
| `SND_SOC_LPASS_RX_MACRO` | m | n | (defconfig:535) |
| `SND_SOC_LPASS_TX_MACRO` | m | n | (defconfig:536) |
| `SND_SOC_SIMPLE_AMPLIFIER` | y | n | not bound by the DTS; the amp is AW87390 (defconfig:515) |
| `SND_SIMPLE_CARD` | m | n | DTS card is the vendor vbc-v4 card (defconfig:537) |
| `SND_AUDIO_GRAPH_CARD` | m | n | (defconfig:538) |
| `SND_AUDIO_GRAPH_CARD2` | m | n | (defconfig:539) |
| `SND_SOC_UMS9230_DIGITAL` | y | n | ums9230 SoC digital audio; the board uses the sharkl5pro vendor stack. KEEP IF the build breaks: SND_SOC_SPRD_UMS9230 selects SPRD_COMMON/MCDT/PLATFORM/VBC_V4_DSP (sound/soc/sprd/Kconfig:26-32) which the VBC_V4_SC2730 vendor path may reuse - UNVERIFIED (defconfig:524) |
| `SND_ALOOP` | m | n | loopback card, rarely used (defconfig:483) |
| `SND_SOC_BT_SCO` | m | n | BT audio goes through PipeWire A2DP (defconfig:500) |
| `SND_SOC_DMIC` | m | n | (defconfig:502) |

### 7. Filesystems, accounting and process features

Kept: EXT4, VFAT, EXFAT (SD cards), SQUASHFS, TMPFS, OVERLAY_FS, FUSE.

| Symbol | From | To | Justification |
|---|---|---|---|
| `BTRFS_FS` | y | n | Arch rootfs is ext4 (defconfig:713) |
| `BTRFS_FS_POSIX_ACL` | y | n | (defconfig:714) |
| `QUOTA` | y | n | (defconfig:717) |
| `AUTOFS_FS` | y | n | (defconfig:718) |
| `CUSE` | y | n | (defconfig:720) |
| `FANOTIFY` | y | n | (defconfig:715) |
| `FANOTIFY_ACCESS_PERMISSIONS` | y | n | (defconfig:716) |
| `HUGETLBFS` | y | n | with THP off (defconfig:726) |
| `CGROUP_HUGETLB` | y | n | (defconfig:24) |
| `BSD_PROCESS_ACCT` | y | n | BSD acct per-exit write (defconfig:11) |
| `BSD_PROCESS_ACCT_V3` | y | n | (defconfig:12) |
| `TASKSTATS` | y | n | per-task accounting netlink (defconfig:13); PSI is separate and kept |
| `TASK_XACCT` | y | n | (defconfig:14) |
| `TASK_IO_ACCOUNTING` | y | n | (defconfig:15) |
| `AUDIT` | y | n | syscall-entry audit hooks cost on every syscall when AUDITSYSCALL=y (defconfig:4); systemd works without it |
| `POSIX_MQUEUE` | y | n | rarely used, harmless; remove only if nothing links against it (defconfig:3) |
| `KEXEC` | y | n | no kexec flow in the SPL/U-Boot chain (defconfig:38) |
| `KEXEC_FILE` | y | n | (defconfig:39) |
| `CGROUP_PERF` | y | n | perf cgroup events not needed (defconfig:28) |
| `MEMTEST` | y | n | boot-time RAM test cost (defconfig:790) |
| `BOOT_PRINTK_DELAY` | y | n | debug delay knob (defconfig:774) |
| `PSTORE_BLK` | y | n | ramoops (PSTORE_RAM, kept) is the reserved-memory crash log (DTS ramoops@fff80000); blk backend needs a partition (defconfig:734) |
| `PSTORE_PMSG` | y | n | (defconfig:732) |
| `NFS_V4` | y | n | (defconfig:736) |
| `NFS_V4_2` | y | n | (defconfig:737) |
| `CRYPTO_USER` | y | n | (defconfig:744) |
| `CRYPTO_DH` | y | n | (defconfig:746) |
| `CRYPTO_DES` | y | n | (defconfig:748) |
| `CRYPTO_DEFLATE` | y | n | (defconfig:755) |
| `CRYPTO_LZO` | y | n | (defconfig:756) |

### 8. Network protocols: trim, keep Waydroid NAT/bridge

Waydroid (waydroid-net) needs: BRIDGE, BRIDGE_NETFILTER, VETH, NF_TABLES(+INET/IPV4), NFT_NAT/MASQ/CT/COMPAT, NF_CONNTRACK + NF_CT_NETLINK, NF_NAT + MASQUERADE, XT_TARGET_MASQUERADE/CHECKSUM, XT_MATCH_ADDRTYPE/CONNTRACK/COMMENT. All are untouched here. Source: arch-wiki Waydroid + vicharak waydroid-axon README (bridge/bridge_netfilter), https://wiki.archlinux.org/title/Waydroid ; research 02 section 7.

| Symbol | From | To | Justification |
|---|---|---|---|
| `IP_VS` | y | n | load balancer (defconfig:188) |
| `NETFILTER_XT_MATCH_IPVS` | y | n | (defconfig:164) |
| `NETFILTER_XT_MATCH_CLUSTER` | y | n | (defconfig:148) |
| `NETFILTER_XT_MATCH_DCCP` | y | n | (defconfig:156) |
| `NETFILTER_XT_MATCH_SCTP` | y | n | (defconfig:180) |
| `NETFILTER_XT_MATCH_L2TP` | y | n | (defconfig:165) |
| `NETFILTER_XT_MATCH_ESP` | y | n | (defconfig:159) |
| `NETFILTER_XT_MATCH_IPCOMP` | y | n | (defconfig:162) |
| `NETFILTER_XT_MATCH_RATEEST` | y | n | (defconfig:177) |
| `NETFILTER_XT_MATCH_REALM` | y | n | (defconfig:178) |
| `NETFILTER_XT_MATCH_OSF` | y | n | (defconfig:172) |
| `NETFILTER_XT_MATCH_U32` | y | n | (defconfig:187) |
| `NETFILTER_XT_MATCH_NFACCT` | y | n | (defconfig:171) |
| `NETFILTER_XT_MATCH_CPU` | y | n | (defconfig:155) |
| `NETFILTER_XT_MATCH_STRING` | y | n | (defconfig:184) |
| `NETFILTER_XT_MATCH_TIME` | y | n | (defconfig:186) |
| `NETFILTER_XT_MATCH_DEVGROUP` | y | n | (defconfig:157) |
| `NETFILTER_XT_MATCH_HASHLIMIT` | y | n | (defconfig:160) |
| `NETFILTER_XT_TARGET_HMARK` | y | n | (defconfig:130) |
| `NETFILTER_XT_TARGET_IDLETIMER` | y | n | wakes timers for nothing (defconfig:131) |
| `NETFILTER_XT_TARGET_LED` | y | n | (defconfig:132) |
| `NETFILTER_XT_TARGET_TEE` | y | n | (defconfig:141) |
| `NETFILTER_XT_TARGET_NETMAP` | y | n | (defconfig:136) |
| `NETFILTER_XT_TARGET_TPROXY` | y | n | (defconfig:142) |
| `NETFILTER_XT_TARGET_AUDIT` | y | n | (defconfig:124) |
| `NETFILTER_XT_TARGET_HL` | y | n | (defconfig:129) |
| `NETFILTER_XT_TARGET_TCPOPTSTRIP` | y | n | (defconfig:144) |
| `NETFILTER_XT_TARGET_NFQUEUE` | y | n | (defconfig:138) |
| `NETFILTER_XT_TARGET_CONNMARK` | y | n | (defconfig:127) |
| `NF_CONNTRACK_H323` | y | n | ALG helpers (defconfig:90) |
| `NF_CONNTRACK_PPTP` | y | n | (defconfig:93) |
| `NF_CONNTRACK_SANE` | y | n | (defconfig:94) |
| `NF_CONNTRACK_SIP` | y | n | (defconfig:95) |
| `NF_CONNTRACK_TFTP` | y | n | (defconfig:96) |
| `NF_CONNTRACK_IRC` | y | n | (defconfig:91) |
| `NF_CONNTRACK_NETBIOS_NS` | y | n | (defconfig:92) |
| `NF_CONNTRACK_FTP` | y | n | (defconfig:89) |
| `NFT_NUMGEN` | y | n | (defconfig:101) |
| `NFT_CONNLIMIT` | y | n | (defconfig:103) |
| `NFT_TUNNEL` | y | n | (defconfig:109) |
| `NFT_QUEUE` | y | n | (defconfig:110) |
| `NFT_QUOTA` | y | n | (defconfig:111) |
| `NFT_HASH` | y | n | (defconfig:114) |
| `NFT_SOCKET` | y | n | (defconfig:115) |
| `NFT_OSF` | y | n | (defconfig:116) |
| `NFT_TPROXY` | y | n | (defconfig:117) |
| `NFT_SYNPROXY` | y | n | (defconfig:118) |
| `NFT_DUP_NETDEV` | y | n | (defconfig:119) |
| `NFT_FWD_NETDEV` | y | n | (defconfig:120) |
| `NFT_REJECT_NETDEV` | y | n | (defconfig:121) |
| `NF_TABLES_NETDEV` | y | n | (defconfig:100) |
| `NF_FLOW_TABLE` | y | n | flowtable offload (defconfig:123) |
| `NF_FLOW_TABLE_INET` | y | n | (defconfig:122) |
| `IP_NF_TARGET_SYNPROXY` | y | n | (defconfig:197) |
| `IP6_NF_TARGET_SYNPROXY` | y | n | (defconfig:211) |
| `IP6_NF_TARGET_NPT` | y | n | (defconfig:212) |
| `IP6_NF_MATCH_SRH` | y | n | (defconfig:209) |
| `IP6_NF_MATCH_MH` | y | n | (defconfig:206) |
| `NET_DSA_TAG_NONE` | y | n | with NET_DSA (defconfig:216) |
| `NET_SCH_CBS` | y | n | TSN qdiscs (defconfig:226) |
| `NET_SCH_ETF` | y | n | (defconfig:227) |
| `NET_SCH_TAPRIO` | y | n | (defconfig:228) |
| `NET_SCH_MQPRIO` | y | n | (defconfig:229) |
| `NET_CLS_FLOWER` | y | n | (defconfig:232) |
| `NET_ACT_GATE` | y | n | (defconfig:236) |
| `MACVTAP` | y | n | VM networking (defconfig:310) |
| `VLAN_8021Q_GVRP` | y | n | (defconfig:223) |
| `VLAN_8021Q_MVRP` | y | n | (defconfig:224) |
| `BRIDGE_VLAN_FILTERING` | y | n | Waydroid bridge does not use VLAN filtering (defconfig:214) |
| `MAC80211_LEDS` | y | n | (defconfig:255) |

### 9. Debug and hardening that costs runtime/power

DEBUG_FS stays y: base618 reads the PMU deep-sleep proof counters through debugfs regmap (research 02 section 1.1). No lockdep/KASAN/KFENCE/ftrace/KPROBES are set in the defconfig (verified by grep of the built .config), so the saving here is small by design.

| Symbol | From | To | Justification |
|---|---|---|---|
| `RCU_TRACE` | y | n | 'default y if TREE_RCU' (kernel/rcu/Kconfig.debug:178-182), adds RCU tracepoints; pulls TRACE_CLOCK |
| `SLUB_DEBUG` | y | n | removes slab red-zone/poison plumbing (defconfig EXPERT=y exposes it) |
| `DEBUG_MEMORY_INIT` | y | n | boot-time memory verification (defconfig:780) |
| `DEBUG_INFO_REDUCED` | y | n | build size only; use DEBUG_INFO_NONE below |
| `DEBUG_INFO_NONE` | n | y | no runtime effect; shrinks vmlinux/build time (defconfig:776-777) |
| `KALLSYMS_ALL` | y | n | keeps data symbols; ~1 MB (defconfig:36) |
| `PROFILING` | y | n | oprofile hooks (defconfig:37) |
| `RANDOMIZE_KSTACK_OFFSET` | y | n | per-syscall random stack offset cost; syscall-heavy emulators pay it (.config) |
| `STACKPROTECTOR` | y | n | STRONG canaries on every function with arrays (~1%). Hardening tradeoff, device is a single-user console (.config) |
| `ARM64_PTR_AUTH` | y | n | Cortex-A55/A75 are ARMv8.2, no PAC hardware (.config) |
| `ARM64_BTI` | y | n | no BTI hardware on A55/A75 (.config) |
| `ARM64_MTE` | y | n | no MTE hardware on A55/A75 (.config) |
| `EFI` | y | n | boot is U-Boot BSP extlinux with a raw Image (research 01 section 2); no EFI stub (.config) |
| `CMDLINE` | "loglevel=7" | "loglevel=3" | console writes to fbcon are slow; extlinux appends its own cmdline anyway (defconfig:49 is loglevel=7) |
| `CONSOLE_LOGLEVEL_DEFAULT` | 8 | 4 | (defconfig:773 is 8: every printk goes to the panel fbcon) |


## 4. Waydroid kernel requirements (current state after the fragment)

Sources: ArchWiki Waydroid (https://wiki.archlinux.org/title/Waydroid, flags its kernel section as out of date), waydroid issue 811 (binderfs, memfd), vicharak waydroid-axon README (bridge, bridge_netfilter), research `02-power-gaming-ux.md` section 7. The list below is the minimum I could source plus what Waydroid's network script needs; **no on-device Waydroid run exists, so completeness is UNVERIFIED**. All rows are `y` after the fragment (checked programmatically against the merged `.config`).

| Symbol | Before | After | Why |
|---|---|---|---|
| `ANDROID_BINDER_IPC` | y | y | binder driver |
| `ANDROID_BINDERFS` | n | y | dynamic binder devices; heavily recommended on mainline (waydroid issue 811) |
| `ANDROID_BINDER_DEVICES` | "binder,hwbinder,vndbinder,puddlejumper,hwpuddlejumper,vndpuddlejumper" | "binder,hwbinder,vndbinder,puddlejumper,hwpuddlejumper,vndpuddlejumper" | initial static set; value "binder,hwbinder,vndbinder,puddlejumper,hwpuddlejumper,vndpuddlejumper" kept |
| `NAMESPACES` | y | y | container |
| `PID_NS` | y | y |  |
| `NET_NS` | y | y |  |
| `IPC_NS` | y | y |  |
| `UTS_NS` | y | y |  |
| `USER_NS` | y | y |  |
| `TIME_NS` | y | y |  |
| `SYSVIPC` | y | y | Android userspace |
| `CGROUPS` | y | y |  |
| `CGROUP_SCHED` | y | y |  |
| `FAIR_GROUP_SCHED` | y | y |  |
| `CPUSETS` | y | y |  |
| `CGROUP_DEVICE` | y | y |  |
| `CGROUP_FREEZER` | y | y | also useful to freeze Waydroid before suspend |
| `CGROUP_PIDS` | y | y |  |
| `MEMCG` | y | y |  |
| `BLK_CGROUP` | y | y |  |
| `CGROUP_BPF` | y | y |  |
| `CGROUP_CPUACCT` | y | y |  |
| `PSI` | y | y | lmkd uses PSI; ArchWiki lists it |
| `SECCOMP` | y | y | LXC |
| `SECCOMP_FILTER` | y | y | LXC |
| `FHANDLE` | y | y |  |
| `EPOLL` | y | y |  |
| `INOTIFY_USER` | y | y |  |
| `MEMFD_CREATE` | y | y | ashmem replacement (waydroid issue 811) |
| `SYNC_FILE` | y | y | Android sync fences |
| `DMABUF_HEAPS` | n | y | gralloc buffers |
| `DMABUF_HEAPS_SYSTEM` | unset | y |  |
| `BLK_DEV_LOOP` | y | y | system.img/vendor.img mount; ArchWiki says module loaded before start; =y here, so no modprobe needed |
| `SQUASHFS` | y | y |  |
| `OVERLAY_FS` | y | y | Waydroid overlay |
| `TMPFS_XATTR` | y | y | Android tmpfs xattrs |
| `TMPFS_POSIX_ACL` | y | y |  |
| `EXT4_FS` | y | y | Android images |
| `FUSE_FS` | y | y | sdcardfs/fuse emulation |
| `VETH` | y | y | veth to container |
| `BRIDGE` | y | y | waydroid0 bridge |
| `BRIDGE_NETFILTER` | y | y | bridge firewalling (waydroid-axon README) |
| `NETFILTER` | y | y |  |
| `NF_CONNTRACK` | y | y |  |
| `NF_CT_NETLINK` | y | y | ArchWiki |
| `NF_NAT` | y | y |  |
| `NF_NAT_MASQUERADE` | y | y |  |
| `NF_TABLES` | y | y |  |
| `NF_TABLES_IPV4` | y | y |  |
| `NF_TABLES_INET` | y | y |  |
| `NFT_NAT` | y | y |  |
| `NFT_MASQ` | y | y |  |
| `NFT_CT` | y | y |  |
| `NFT_COMPAT` | y | y | iptables-nft rules |
| `IP_NF_IPTABLES` | y | y | iptables front end over nft |
| `NETFILTER_XT_TARGET_MASQUERADE` | y | y |  |
| `NETFILTER_XT_NAT` | y | y |  |
| `NETFILTER_XT_TARGET_CHECKSUM` | y | y | dnsmasq/DHCP checksum fill |
| `NETFILTER_XT_MATCH_ADDRTYPE` | y | y |  |
| `NETFILTER_XT_MATCH_CONNTRACK` | y | y |  |
| `NETFILTER_XT_MATCH_COMMENT` | y | y |  |
| `NETFILTER_XT_TARGET_MARK` | y | y |  |
| `NETFILTER_XT_MATCH_MARK` | y | y |  |
| `NETFILTER_XT_TARGET_TCPMSS` | y | y |  |
| `IPV6` | y | y |  |
| `TUN` | y | y |  |
| `IP_MULTICAST` | y | y |  |
| `DRM_PANFROST` | m | m | GPU buffer sharing with the container is the unproven part (research 02 section 7) |

Not set and deliberately left: `CFS_BANDWIDTH` (n), `MEMCG_V1` (n; Waydroid on a cgroup-v2 systemd host), `SW_SYNC` (debug), `ASHMEM` (removed from mainline; memfd replaces it), `ANDROID_BINDER_IPC_RUST` (depends on `!ANDROID_BINDER_IPC`, never enable both). If Waydroid's `lmkd` or `cpu` cgroup handling complains, `CFS_BANDWIDTH` and `MEMCG_V1` are the first two to try.

## 5. Scheduler, tick, preemption, cpufreq, EAS, zram

Current (defconfig / built `.config`): `PREEMPT=y` (defconfig:9), `HZ_250` (.config), `NO_HZ_IDLE` + `HIGH_RES_TIMERS` (defconfig:5-6), `ENERGY_MODEL=y` (defconfig:51), schedutil default (`CPU_FREQ_DEFAULT_GOV_SCHEDUTIL=y`), `SCHED_MC=y`, cpuidle governor `menu`. plorpOS's own CFW config is `PREEMPT` + `HZ_250` + schedutil (research 02, line 139).

| Knob | Recommendation | Reason |
|---|---|---|
| Preemption | `PREEMPT_LAZY=y` + `PREEMPT_DYNAMIC=y` (default lazy). Boot-time switch: `preempt=lazy|full|voluntary` | Lazy keeps RT/audio threads fully preemptible but lets fair tasks finish their slice, which helps emulator throughput. DYNAMIC lets us A/B on the device without rebuilds and keeps `PREEMPT_RCU=y` (validated: PREEMPT_LAZY alone makes PREEMPT_RCU disappear, `kernel/rcu/Kconfig:19-21`). Which wins on frame time is **unmeasured**. |
| HZ | keep `HZ_250` | With lazy preemption a fair-class wakeup preemption waits for the next tick: 4 ms at 250 Hz vs 10 ms at 100. `NO_HZ_IDLE` already removes ticks on idle CPUs, so a higher HZ does not cost idle battery. HZ_100 saves little and costs latency; HZ_300/1000 add tick overhead on 6 little cores. |
| Idle | `CPU_IDLE_GOV_TEO=y`, menu off | designed for tickless systems; Armbian's board hook uses TEO too (research 01 section 8). |
| cpufreq | keep schedutil (default), performance, powersave; remove ondemand/conservative/userspace | The three profiles map to governors (schedutil = Normal, performance = Max, powersave or schedutil + `scaling_max_freq` cap = Power Save). Governor is a sysfs write, no reboot. `CPU_FREQ_STAT` kept for profile validation. |
| EAS / energy model | already on: `ENERGY_MODEL=y`, per-CPU `capacity-dmips-mhz` (A55 544, A75 1024) and `dynamic-power-coefficient` (102 / 501) in `ums512.dtsi:173-174, 329-330`; the driver registers the EM (`sprd-hwdvfs-cpufreq.c:797`). Nothing to add except `UCLAMP_TASK` | EAS only operates with schedutil, so the `performance`/`powersave` profiles bypass it by design. `UCLAMP_TASK(_GROUP)` gives the game launcher a per-cgroup min-utilisation floor for the big cores without switching governor. |
| EAS caveat | **UNVERIFIED on device** | thwonp's DTS comment says the cluster clocks stay at 1536 / 1228.8 MHz regardless of requests and throttling is by idle injection (research 02 headline 4, Q1). If true, no governor or EAS change here has any effect until that is fixed. Test first: busy loop pinned per cluster, read the PMU cycle counter across `scaling_setspeed` changes. `ARM_PMUV3` is now enabled by the fragment for exactly this. |
| SCHED_SMT / SCHED_CORE | off | no SMT cores |
| zram | keep `ZRAM=y`; default algorithm `lz4` (`ZRAM_DEF_COMP_LZ4`); userspace sets size/priority (zram-generator) and `vm.swappiness`, `vm.page-cluster=0` | Cheaper (de)compression on A55 while a game runs; zstd stays selectable at runtime (`ZRAM_BACKEND_ZSTD` kept). Defconfig has zstd (defconfig:284-285). **Ratio vs CPU trade UNVERIFIED**; also `LRU_GEN=y` (MGLRU) for reclaim under pressure. |
| THP | off | `TRANSPARENT_HUGEPAGE_ALWAYS=y` in the built `.config` can add compaction stalls and memory bloat on a few-GB device; JIT cores (melonDS) use `madvise`-style hints anyway (research 02, line 104). |
| 39-bit VA | keep `ARM64_VA_BITS_39` | 3-level page tables = one fewer walk level per TLB miss on a 4 KB-page kernel. Armbian forces 48-bit (research 01 section 8); the reason is not documented there. Revisit only if a runtime (some JITs, Android ART in Waydroid) fails on a 39-bit address space - UNVERIFIED. |

## 6. Panfrost: `=y` vs `=m`

- State: `DRM_PANFROST=m` (defconfig:474); `DRM_SCHED=m`, `DRM_GEM_SHMEM_HELPER=m` follow it. The fragment does **not** change it, because I have not run a render test (research 01 risk 3). Decision belongs after `kmscube`/RetroArch GL on hardware.
- Recommended once it renders: **`=y`**. Reasons: the boot initrd carries only firmware (`initrd-fw.cpio.gz`: `dev/console` + three firmware files, research 02 section 1.2), so a modular Panfrost loads only after the rootfs mounts and udev coldplug runs; with `=y` `/dev/dri/renderD128` and the GPU devfreq node exist when the frontend starts, and GPU runtime PM/devfreq are set up before userspace picks a governor. Cost: `DRM_SCHED` + `DRM_GEM_SHMEM_HELPER` become built-in, a few hundred KB of image (UNVERIFIED), and a Panfrost oops is no longer recoverable by not loading the module.
- To switch: add `CONFIG_DRM_PANFROST=y` (plus nothing else, Kconfig selects the helpers).
- Keep `DRM_PANFROST` independent of `mali_kbase`: MIROKI's blob path sets `# CONFIG_DRM_PANFROST is not set` (research 01 section 7); do not mix.

## 7. Must-keep list (all verified present in the merged `.config`)

Deep sleep per base618/plorpOS (research 02 section 1.1): `SPRD_PMSYS_REMOTEPROC` (CM4 `pm_sys`), `REMOTEPROC`, `PHY_SPRD_USB2` + `USB_MUSB_SPRD` (UTMI clock gate in musb noirq suspend), `SPRD_AUDCP_BOOT` + `SND_SOC_UNISOC_VBC_V4_SC2730` (AGDSP), `ARM_ARCH_TIMER` + `SPRD_TIMER` (`arm,no-tick-in-suspend`), `MFD_SC27XX_PMIC` and all SC27xx subdevices, `SPRD_PM_DOMAINS`, `SPRD_IOMMU`, `SPRD_MBOX`, `RPMSG_SPRD_SIPC`, `SC27XX_WATCHDOG`, `SPRD_WATCHDOG`, `CHARGER_BQ2415X`, `FUEL_GAUGE_SC27XX`, `TYPEC_SC27XX_PD`, `SUSPEND`, `PM_SLEEP`, `DEBUG_FS` (PMU deep-sleep proof counters are read via debugfs regmap). Panel/touch/input: `DRM_SPRD`, `DRM_PANEL_GENERIC_DSI`, `BACKLIGHT_PWM`, `TOUCHSCREEN_GOODIX`, `KEYBOARD_GPIO` (gpio-keys incl. `SW_LID` hall), `INPUT_EVDEV`, `INPUT_JOYDEV`. Connectivity: `UNISOC_WCN_BSP`, `SC23XX*`, `SDIOHAL`, `WCN_SLP`, `SPRD_WCN_REMOTEPROC`, `BT`, `BT_SPRDSDIO`, `BT_HCIUART` (BT_SPRDSDIO depends on it: dropping it silently removed the BT driver in my first validation pass), `CFG80211`, `MAC80211`. USB: `USB_GADGET`, `USB_G_SERIAL`, `U_SERIAL_CONSOLE`, `USB_CONFIGFS(+SERIAL/ACM/NCM/ECM/MASS_STORAGE/F_FS)`, host `USB`, `USB_OTG`, `USB_STORAGE`, `USB_ACM`, `USB_RTL8152`. Binder: `ANDROID_BINDER_IPC=y`, `ANDROID_BINDERFS=y`. PortMaster: `SQUASHFS(+LZ4/ZLIB/ZSTD/XZ)`, `BLK_DEV_LOOP=y`, `INPUT_UINPUT=y`, `FUSE_FS`, `EXT4`, `VFAT`, `EXFAT_FS`. `COMPAT=y` stays so 32-bit PortMaster/armhf binaries run (A55/A75 implement AArch32 EL0; UNVERIFIED that any target port needs it). Kept `ARM64_VA_BITS_39`, `RANDOMIZE_BASE`, errata options, KPTI (runtime-detected).

## 8. Validation (what was run)

```
git worktree add --detach /home/dakoda/.hermes/cache/scratch/kcfg-<n> fedeb60b      # removed afterwards
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- ums512_defconfig
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- scripts/kconfig/merge_config.sh .config kernel-trim.config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig                           # no change (idempotent)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- sprd/ums512-rg-rotate.dtb              # DTB only, for section 2
```

Results (`evidence/merge_config.log`, `trimmed.config`):
- **Requested symbols that did not stick: none.** Final script check compared all fragment lines against the merged `.config`.
- merge_config printed 361 `WARNING: Value requested ... not in final .config` lines. **All 361 are symbols that came from `ums512_defconfig`'s own resolved config** (sub-options that vanished because their parent was trimmed: XEN_*, KVM_*, MEDIA tuners, DSA tags, NFS sub-options, USB serial drivers, ...), not from the fragment. The full list of collateral removals (937 symbols, includes derived `ARCH_*`/`HAVE_*` helpers) is `kernel-trim-evidence/removed-symbols.txt`. I reviewed it for anything outside the intended groups; the notable side effects are: `DM_*`/`BLK_DEV_MD` (dm-crypt, dm-verity) go with `MD`; `SERIAL_EARLYCON` goes with the UART console symbols; `SND_SOC_GENERIC_DMAENGINE_PCM`/`SND_COMPRESS_OFFLOAD` go with the FSL/codec drivers (no vendor-stack use found: grep of `sound/soc/sprd/vendor/*.c` for `snd_dmaengine`/`snd_compr` returned nothing); `CRYPTO_DES`, `CRYPTO_DH` drop; `TASK_DELAY_ACCT` goes with TASKSTATS.
- Symbols that Kconfig forced back on despite earlier attempts, so removed from the fragment: `FRAME_POINTER` (arm64 `ARCH_WANT_FRAME_POINTERS`), `CRYPTO_ECDH` (selected by Bluetooth SMP), `DRM_PANEL_ORIENTATION_QUIRKS`.
- Programmatic check: every DTS-bound symbol from section 2 that was on in the base config is still on after the merge.
- **Not done (by instruction):** no kernel build. Link-time / build-time breakage from missing selects is therefore **UNVERIFIED**. Highest-risk spots to look at first in the tools lane's build: the `sound/soc/sprd/vendor` objects (dropping `SND_SOC_GENERIC_DMAENGINE_PCM`, `SND_SOC_UMS9230_DIGITAL`), `drm_client_setup` link in `sprd_drm.c` (guarded by the `DRM_HDLCD` keep), and anything using `CRYPTO_USER_API`-style AF_ALG (kept via `CRYPTO_USER_API_RNG`).
- Worktree `/home/dakoda/.hermes/cache/scratch/kcfg-*` removed with `git worktree remove --force` + `git worktree prune`; source tree `git status` clean.

## 9. Unknowns and hand-offs

Unverified, needs the device or a build:
1. That the trimmed kernel builds, boots and keeps deep sleep (3.3 mA claim from base618, not re-measured). Boot-test with the fragment on SD; keep a known-good `Image` on the bootfs.
2. Frame-time / FPS effect of `PREEMPT_LAZY` vs `PREEMPT` vs `voluntary` (use the `preempt=` boot parameter), of `lz4` vs `zstd` zram, of `TEO` vs `menu`.
3. Whether cpufreq actually changes clocks (research 02 Q1); EAS/uclamp are moot if not.
4. `BT_HIDP`/`BT_BNEP` removal vs Bluetooth gamepads (re-enable `BT_HIDP=m` if pads fail to bind).
5. Waydroid completeness (section 4) and GPU buffer sharing with Panfrost.
6. Whether any PortMaster port is armhf (`COMPAT`), and whether `ARM64_VA_BITS_48` is required by any runtime.
7. `PANIC_ON_OOPS=y` (defconfig:781) was left as is. With `panic=` unset, a panic hangs the device instead of rebooting; set a `panic=5` cmdline in extlinux or drop the option. Not changed because it affects crash-log behaviour (ramoops).
8. Console: `CMDLINE="loglevel=3"`, `CONSOLE_LOGLEVEL_DEFAULT=4` quiets fbcon printk; flip back for bring-up.

Hand-offs:
- **coding lane (kernel patch):** add `select DRM_CLIENT_SELECTION` to `config DRM_SPRD` in `drivers/gpu/drm/sprd/Kconfig` (after line 11), and `select DMABUF_HEAPS` is needed where DMABUF_HEAPS_SYSTEM/CMA are wanted (or put `CONFIG_DMABUF_HEAPS=y` in `ums512_defconfig` at line 769). Both are source changes in the kernel tree, which I did not touch.
- **tools lane (builder):** use `merge_config.sh arch/arm64/configs/ums512_defconfig docs/kernel-trim.config`, report the first build/link failure against the risk list in section 8.
- **git lane:** nothing committed; `docs/` files are untracked changes in the rotate-os project.
