# 03 – CPU frequency scaling on the T618 `rotate-fixes` kernel

Accessed 2026-10-31. Local tree: `upstream/linux-mainline-sprd` @ `fedeb60b6` (branch `rotate-fixes`, 2026-10-26). The local clone is **shallow (50 commits)**, so history was read from a blobless bare clone of the same remote (`https://github.com/thwonp/linux-mainline-sprd`, branch `rotate-fixes`, scratch dir, not in the project). All SHAs below are from that history; `fedeb60b6` is a descendant of all of them (verified with `git merge-base --is-ancestor`).

## Verdict

- **Scaling is real on the A75 cluster; the "fixed 1536/1228.8 MHz" DTS comment is stale.** It describes the state before 2026-08-05/06 fixes. The comment was never removed (`ums512.dtsi:178-185`).
- **A55 cluster (cluster0): fixed in source, but no post-fix clock measurement found in the log.** The commits that cured the engine are generic to both clusters (`0029f8e5ed05`, `5cd4359705d3`, `4333aa960f6d`), but the only post-fix numbers are for the A75 voltage path. Treat A55 scaling as **probable, UNVERIFIED on hardware** until the measurement plan below is run.
- `scaling_max_freq` therefore does change the real clock on the big cores, and is expected to on the little cores. Real DVFS = frequency (MPLL/mux via TOP DVFS engine) + voltage (DCDC grade over ADI for cluster0, over the hw-I2C master to a FAN53555 for cluster1). Nothing is missing in driver/regulator/calibration for a working baseline. Open items are in "What is still missing".

## Which driver binds

- Not `cpufreq-dt`, not SCMI. `cpufreq-dt-platdev.c` has no sprd entry (grep returned nothing) although `CONFIG_CPUFREQ_DT=y` is built (`build/out/kernel/config:648-650`).
- Driver is the vendor-"realme-gen" hardware DVFS port: `drivers/cpufreq/sprd-hwdvfs-cpufreq.c`, registered as `.name = "sprd-cpufreq"`, `.target_index = sprd_hardware_cpufreq_set_target_index`, `CPUFREQ_IS_COOLING_DEV` (`sprd-hwdvfs-cpufreq.c:933-941`). Binds node `hardware-dvfs-platform` (`compatible = "sprd,hardware-cpufreq"`, `ums512.dtsi:902-904`), engine `cpudvfs-dev@322a8000` (`sprd,sharkl5pro-cpudvfs`, `ums512.dtsi:907-911`), TOP DVFS syscon `322a0000` (`ums512.dtsi:826-886`). Per-SoC tables: `sprd-hwdvfs-ums512.c`, ported from the stock `sprd-hwdvfs-ums512-arch.ko` (commit `0029f8e5ed05`).
- Config: `CONFIG_ARM_SPRD_CPUFREQ_V2=y`, `ARM_SPRD_HW_CPUFREQ_NORMAL=y`, `ARM_SPRD_HW_CPUFREQ_ARCH_UMS512=y`, `ARM_SPRD_CPUFREQ_PUBLIC=y`, `ARM_SPRD_TOP_DVFS=y` (`build/out/kernel/config:651-655`). Governor TEO idle (`:614`), `HOTPLUG_CPU=y` (`:454`), `ENERGY_MODEL=y` (`:598`), `CPU_IDLE_THERMAL=y` (`:2767`).
- beebono's bring-up (`upstream/rg-rotate-linux/DEVICE-BRINGUP.md:35`) refers to an **older, different** implementation (`sprd-cpufreqhw.c`, "register-level engine ported from 4.14 vendor", 6.16/7.1). The in-tree `sprd-cpufreq-v2` SMCCC path was a dead end there ("stock firmware returns NOT_SUPPORTED", same line). rotate-fixes replaced the engine with the realme-5.4-generation stack (`e585a6ac1df9`, "retire old engine"), so beebono's "working" claim and thwonp's "inert" claim describe **different code at different dates**, not a contradiction about one kernel.

## OPP tables (kHz / µV), `ums512.dtsi:81-113`

- cluster0 `opp-table0`, cpumask 0x3f (6×A55; `:83`): 614400/756250, 768000/756250, 962000/756250, 1144000/756250, 1228800/787500, 1482000/875000, 1536000/890625, 1716000/953125, 1820000/990625, 2002000/1050000.
- cluster1 `opp-table1`, cpumask 0xc0 (2×A75; `:102`): 1228800/750000, 1482000/850000, 1536000/870000, 1742000/950000, 1872000/1000000, 2002000/1050000.
- These are the stock T618-binned tables decoded from the factory DTB (`ea1101899521`). Note: 1536000 (A55) and 1228800 (A75) are simply table entries, which is why the old "fixed at" figures coincide with real OPP rows (presumably boot-loader leftovers at that time; UNVERIFIED inference).
- Board file adds nothing to OPPs: `ums512-rg-rotate.dts` has no opp-hz; it only enables the engine (`&cpudvfs_dev`, `:503-506`, `:756-758`), sets the DVFS-I2C pinmux (`:448-458`) and `&aon_i2c0` at 1 MHz (`:749-754`).
- No `opp-hz` (operating-points-v2) table exists for CPUs; the CPU tables are legacy `operating-points` read by `sprd-cpufreq-public.c` by node name `opp-table0/1` (`ums512.dtsi:74-80`). Only the GPU uses `operating-points-v2` (`:1390-1420`).
- Supplies: `dvfs-dcdc-cpu0-cfg` (PMIC type 0, 9375 µV/µs) and `dvfs-dcdc-cpu1-cfg` (third-party PMIC over I2C, 4000 µV/µs, `ums512.dtsi:115-127`). No `cpu-supply` regulator phandles: voltage is programmed by the hardware engine, not the regulator framework.

## History (git log on rotate-fixes; `git log -i --grep`, `-S`)

- `9d7a760cd2a3` 2026-06-29: first "register-level CPU DVFS".
- `0029f8e5ed05` 2026-07-26: root-cause fixes: missing `.data` for `sharkl5pro-cpudvfs`; PMIC index 2 not populated (hung boot); `host_cluster_auto_tuning_enable()` cleared the enable bits ("pinned the big cluster to its boot clock source"). Result quoted: 200k-iteration loop on cpu6 2.06 s vs 3.23 s on cpu0.
- `ea1101899521` 2026-07-26: correct tables + hw-I2C master enabled.
- `d790b229b7bd` 2026-08-03: states DVFS "never applies" latched index; "openssl throughput and battery draw identical at 614400 and 2002000 (2.68 W vs 2.71 W active)". This is the commit behind the DTS comment. Switched thermal to idle injection.
- `fa6c2c7b57ef` 2026-08-04: per-CPU thermal-idle nodes (stop-gap).
- `5cd4359705d3`, `4333aa960f6d` 2026-08-05: polarity fix on TOP DVFS enable bits; MPLL output switched to DVFS control before relock.
- `fc0b2a861e04` (interim voltage-meet bypass for DCDC_CPU1) then `ef2bee3b2af1` 2026-08-06: **the A75 voltage really scales**; hw-I2C DVFS request channels were never enabled on our boot path; measured "1.23 GHz @ 0.75 V / 2.00 GHz @ 1.05 V" with real FAN53555 writes; bypass removed.
- `20188b1a4967` 2026-08-06: "With real A75 voltage scaling, cpufreq cooling cuts power ~f·V² per step and regulates sustained all-core load (openssl speed -multi 8) at 68-71 C ... idle injection never needed". Idle-injection **cooling maps removed**; thermal-idle nodes kept. Current cooling maps reference `&CPU0`/`&CPU6` cpufreq cooling devices (`ums512.dtsi:2332-2345`). Passive trip in this tree is 85 °C (`:2320`), not the 70 °C in beebono's doc.
- No later commit touches `drivers/cpufreq/sprd*`; Oct commits are suspend/pm_sys work (`fc50b7af2e71`, `b2c6582badd8`). So the HEAD behaviour = state after `20188b1a4967`.
- Note: pm_sys remoteproc (CM4) stays disabled behind the stock lock (`9d13425e36cc`); the commit says "A75 DVFS does not need pm_sys". Whether anything needs it for DDR DVFS is separate (DDR devfreq absent, see 01-boot-kernel).

## What is still missing / risks

- No A55 hardware measurement after the fixes (UNVERIFIED). cluster0 uses the on-die ADI DCDC path; cluster1 the I2C path that needed the extra fix.
- Stale comment `ums512.dtsi:176-190` should be rewritten (hand to `git` lane, with the measurement below as evidence).
- Per-core clock: clusters share a clock; `policy0`=cpu0-5, `policy6`=cpu6-7. No per-core DVFS.
- DDR/bus (scu/periph/gic tables exist in the engine) not exposed as devfreq; memory-bound loads will not scale.
- Calibration: voltages come from the stock T618 bin tables, not a per-chip efuse bin ("no cpu-bin nvmem cells", `ums512.dtsi:76-78`). Some chips may be unstable at the table minimum; test stability at each OPP.

## Measurement plan (needs the device; Dakoda runs it)

Step 0: read, do not set:
- `cat /sys/devices/system/cpu/cpufreq/policy*/{scaling_driver,scaling_governor,cpuinfo_min_freq,cpuinfo_max_freq,scaling_available_frequencies,scaling_cur_freq}` (expect driver `sprd-cpufreq`, policies 0 and 6; if the driver is `cpufreq-dt`, this analysis does not apply).
- `cat /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_cur_freq` (may be absent; `scaling_cur_freq` is the driver's view, `.get` = hardware work-index readback, `sprd-hwdvfs-cpufreq.c:941`).
- `ls /sys/kernel/debug/ | grep -i dvfs` (driver has `sprd-hwdvfs-debugfs.c`); dump engine state, UNVERIFIED file names.
- `dmesg | grep -i -E 'dvfs|cpufreq|hw-i2c'`.

Step 1: ground-truth frequency by timing (does not trust sysfs). For each policy `P` in {0 (use cpu2), 6 (use cpu6)}:
1. `echo performance > /sys/devices/system/cpu/cpufreq/policyP/scaling_governor`.
2. For each frequency F in `scaling_available_frequencies`: `echo F > scaling_min_freq; echo F > scaling_max_freq` (set max first when going down, min first when going up), `sleep 1`.
3. `taskset -c N ./spin` where `spin` is a fixed N-iteration dependent-add loop compiled with `-O1` and `asm volatile` to stop folding (e.g. 2×10^9 iterations of a 1-cycle dependent add), and print wall time. Run 5×, take median. Frequency ≈ iterations / time / cycles-per-iter. Cross-check with the PMU: `perf stat -e cycles -C N -- taskset -c N ./spin` gives cycles/elapsed; `perf` requires `CONFIG_PERF_EVENTS` and the PMU nodes (`pmu-a55` exists, `ums512.dtsi:391`).
4. **Pass:** measured freq tracks F within ±5 % across the whole range and time scales inversely; **Fail (pinned):** time flat across F (what d790b229 saw). Report the table in a result file.
5. Also read `scaling_cur_freq` before/after each step to check readback consistency.

Step 2: voltage / power. Needs a USB power meter or the battery gauge:
- Battery gauge (no extra hardware): `/sys/class/power_supply/*/current_now` and `voltage_now` (FGU driver, `sc27xx_fgu`; UNVERIFIED sign convention), screen off, wifi off, 60 s average per OPP, unplugged.
- External meter (preferred): inline USB-C meter on the charge port with the battery full and charger-through, or a bench supply at the battery terminals. Idle baseline first (all cores idle, screen off) then one core 100 % per OPP. Pass: power rises monotonically with F and roughly follows f·V² (e.g. A75 at 2.0 GHz/1.05 V vs 1.23 GHz/0.75 V should differ by ≈3.3× in dynamic core power by f·V²: (2.0/1.23)·(1.05/0.75)² = 3.19; compute, not measured).
- Compare with the historical figure in `d790b229b7bd` (2.68 W vs 2.71 W, the "pinned" signature). A change from that signature is the proof.
- Thermal: `cat /sys/class/thermal/thermal_zone*/temp` while running `openssl speed -multi 8`; expect regulation at the 85 °C passive trip (`ums512.dtsi:2320`) with `cooling_device*/cur_state` changing for the `cpufreq-cpu0`/`cpufreq-cpu6` devices.

## Power-save profile alternatives

- **Frequency cap (primary):** if Step 1 passes, a Power-Save profile = governor `schedutil`/`conservative`, `scaling_max_freq` ≈ 1144000 (A55, still 0.75 V per table, the best perf/W row; rows 614-1144 MHz share 756250 µV so there is no voltage gain below 1144 MHz) and 1228800 for A75. Insight from the table: below 1144 MHz on cluster0 only frequency drops, not voltage, so the lowest rows are inefficient for energy-per-task (race-to-idle favours 1144 MHz). This is derived from the table, not measured.
- **Core parking (hotplug):** `echo 0 > /sys/devices/system/cpu/cpuN/online` for cpu6/7 (A75) is supported (`CONFIG_HOTPLUG_CPU=y`, `config:454`). PSCI CPU_OFF is available (`enable-method = "psci"`, `ums512.dtsi:170`). Expected effect: removes A75 leakage and, importantly, lets cluster1 power down; the DCDC_CPU1 rail is I2C-controlled and stays at its last grade (UNVERIFIED whether it drops). Costs: games lose peak performance (A75 capacity 1024 vs A55 544, `capacity-dmips-mhz`). Suspend already hotplugs CPUs, and the resume WARN was fixed (bring-up doc line 62), so hotplug is exercised. Measure with the power meter (idle with cpu6,7 online vs offline).
- **Idle-state depth:** only one idle state is defined, `CORE_PD` (psci param `0x00010000`, entry/exit 4000 µs, min residency 10000 µs, `ums512.dtsi:364-374`) plus implicit WFI. No cluster-level state, so depth cannot be tuned further from DT without firmware (TF-A PSCI) support. TEO governor is already built in (`config:614`). `cpuidle/state1/disable` can be set per CPU to test the effect (power with `CORE_PD` disabled vs enabled). Idle depth is not a profile knob; hotplug is cruder but effective for an "ultra-save" mode.
- **Idle-injection (thermal-idle)** nodes still exist per CPU (`ums512.dtsi:208-355`) and `CONFIG_CPU_IDLE_THERMAL=y`; can be driven manually through `/sys/class/thermal/cooling_device*/cur_state` for a duty-cycle cap that works even if frequency control fails; this is the fallback if Step 1 shows A55 pinned.
- Recommended order for the frontend (proposal): Max = performance both clusters; Normal = schedutil full range; Save = schedutil with max_freq caps + cpu6/7 offline; each setting verified by Step 1/2 before it ships.

## Handoffs

- `git` lane: replace the stale comment at `ums512.dtsi:176-190` after Step 1 results exist (do not edit upstream tree).
- Device test: Dakoda, one instruction: "boot the SD image, run `scripts/cpufreq-probe.sh` (to be written by the build lane per this plan), save the output to the SD card".
