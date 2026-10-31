# Phase 1 research 05: GPU shader cost on Mali-G52 via Mesa drm-shim

Accessed 2026-10-31. Author: `writer` profile from the on-disk results of a prior agent run (results dir
`build/work/gpu-shim/results/`, files dated 2026-10-31 13:51-14:08). **Nothing here ran on a G52.** The real Panfrost
userspace driver and the real Bifrost compiler produced these numbers on an x86 host; no GPU executed any shader.
Mesa was not rebuilt for this document; the aggregation scripts are in `build/work/gpu-shim/report/`.

```text
 shader source ──► Mesa 26.2.6 Panfrost (GLES 3.1) ──► Bifrost compiler ──► per-shader stats
 (RetroArch /        real userspace driver, kernel          arch v7 (G52)      cycles, regs, threads,
  melonDS / libretro  ioctls faked by drm-shim                                  spills, tex, ld/st
  glsl-shaders)       (no GPU, no execution)
```

## 0. Headline findings

1. **490 of 684 libretro GLSL shaders compile for the G52 on the GLES 3.1 path; 638 of 684 on the desktop GL 3.1 path.** The failures are GLSL source problems, not Panfrost problems: an llvmpipe control run fails the same 194 shaders (section 2.2).
2. **The median libretro shader is cheap:** p50 = 4.62 fragment cycles, p90 = 27, but the tail is long (p99 = 430, max = 880). 118 of 490 compiled shaders (24%) use more than 32 registers and so run at half occupancy (`threads = 1`); 35 spill registers to memory (spills > 0).
3. **Cheap enough for 720x720@60 (≤ ~4 cycles, 2 threads, no spills):** `sharp-bilinear` (1.67), `zfast_lcd`, `lcd3x`, `zfast_crt` (2.29), `crt-pi` (3.46), `scale2x`, `gba-color`/`gbc-color`. **Avoid:** `crt-lottes` (135, 29 spills), `xbrz` 4x-6x (164-674), `nnedi3`, `jinc2` (spills).
4. **melonDS's compute renderer does not compile on this stack** (32 of 33 compute shaders fail; one trivial one links). Its GL (fragment) renderer shaders all compile and are cheap except the 2D compositor (44.92 cycles, threads 1). Use the software renderer (plorpOS builds melonDS with `-DENABLE_OPENGL=OFF`, research 02 s2.4).
5. **`MESA: error: export failed` is not a shader failure** (section 2.3). It appears on every compiled case and does not change rc or stats.
6. **PanVK on G52 says it is non-conformant** and refuses to enumerate without `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` (section 7). The `video_driver = "gl"` choice in `retroarch.cfg` stands.

## 1. Method and exact commands

### 1.1 Environment

"Task log" below is the prior agent's transcript, `~/.hermes/cache/delegation/live/deleg_57b2e90f/task-1.log` (outside the repo).

| Item | Value | Source |
|---|---|---|
| Mesa | **26.2.6** (release tarball `mesa-26.2.6.tar.xz` from archive.mesa3d.org) | task log 13:38:42; `inst/lib/.../libgallium-26.2.6.so`; `GL_VERSION: OpenGL ES 3.1 Mesa 26.2.6` (`out_info.txt`) |
| Host | x86_64 Linux, native build, LLVM 21.1.8 (`llvm-config-21`) | task log 13:39:48 |
| meson options | `-Dbuildtype=release -Dgallium-drivers=panfrost -Dvulkan-drivers=panfrost -Dtools=drm-shim,panfrost -Dplatforms= -Dglx=disabled -Degl=enabled -Dgles1=enabled -Dgles2=enabled -Dllvm=enabled -Dshared-llvm=enabled -Dgbm=enabled` (+ tests, rusticl, va, valgrind off) | `bld/meson-logs/meson-log.txt` line 3 |
| GPU id | **0x7212 (Mali-G52)**, the drm-shim default; `PAN_GPU_ID` was **not** set | `src/panfrost/drm-shim/panfrost_noop.c` L19-L30 (`PAN_GPU_ID_DEFAULT (0x7212)`) |
| Core count | the shim advertises `shader_present = 0x50005` = **4 cores**, so GL/Vulkan report "Mali-G52 **MC4**". The RG Rotate's T618 has a G52 **MP2**. | `panfrost_noop.c` L189; `out_info.txt` `GL_RENDERER` |
| GL reported | `OpenGL ES 3.1 Mesa 26.2.6`, GLSL ES 3.10, 139 extensions | `out_info.txt` |
| RetroArch source | checkout `cbe5e51` (`build/work/gpu-shim/ext/ra`), stock GL shaders from `gfx/drivers/gl_shaders/` | `git -C ext/ra log -1` |
| libretro shaders | checkout `cba01a4` (2026-10-23, `ext/glsl`), 684 single-file `.glsl` shaders with a `VERTEX` stage | `git -C ext/glsl log -1`; `harness/corpus.py` |
| melonDS | melonDS-ds `f394adb` (v1.4.0, same pin as research 02 s2.4) plus its melonDS submodule shaders | task log 13:46:50 |

### 1.2 Environment file (`harness/env.sh`)

```sh
B=/home/dakoda/Projects/rotate-os/build/work/gpu-shim
I=$B/inst/lib/x86_64-linux-gnu
export __EGL_VENDOR_LIBRARY_FILENAMES=$B/inst/share/glvnd/egl_vendor.d/50_mesa.json
export LD_LIBRARY_PATH=$I
export LD_PRELOAD=$I/libpanfrost_noop_drm_shim.so   # the drm-shim
export MESA_LOADER_DRIVER_OVERRIDE=panfrost
export MESA_SHADER_CACHE_DISABLE=true               # else repeat runs hit the disk cache and print no stats
```

`corpus.py` adds `BIFROST_MESA_DEBUG=shaderdb`, which makes the compiler print one
`MESA_SHADER_<STAGE> shader: N instrs, N tuples, N clauses, X cycles, ...` line per compiled stage.

### 1.3 Re-run

```sh
cd build/work/gpu-shim
harness/run_all.sh          # needs inst/, ext/ra, ext/glsl, ex/melonDS, ex/melonds-ds (all present)
python3 -I report/analyze.py && python3 -I report/build_doc.py   # regenerates summary.json and this doc
```

`run_all.sh` runs six corpora; `shprobe` (surfaceless EGL, `harness/shprobe.c`) is the probe. For each vertex+fragment pair it
creates a GLES 3.1 (or desktop GL 3.1 when `SHPROBE_API=gl`) context, compiles, links, binds dummy textures for every sampler and
draws one 256x256 triangle strip so the gallium driver builds the final variant, then `glFinish()`. Exit codes: 0 ok, 2 EGL/context,
3 compile fail, 4 link fail. Eight cases run in parallel (`JOBS`).

| Run | Command | Output |
|---|---|---|
| RetroArch stock, GLES | `python3 -I harness/corpus.py results/ra-stock.jsonl ra-stock ext/ra` | `ra-stock.jsonl` |
| RetroArch stock, desktop | `CORPUS_DESKTOP=1 SHPROBE_API=gl python3 -I harness/corpus.py results/ra-stock.desktop.jsonl ra-stock ext/ra` | `ra-stock.desktop.jsonl` |
| libretro GLSL, GLES | `python3 -I harness/corpus.py results/glsl-all.jsonl glsl ext/glsl` | `glsl-all.jsonl` |
| libretro GLSL, desktop | `CORPUS_DESKTOP=1 SHPROBE_API=gl python3 -I harness/corpus.py results/glsl-all.desktop.jsonl glsl ext/glsl` | `glsl-all.desktop.jsonl` |
| llvmpipe control | `CORPUS_ENV=$PWD/harness/env-control.sh python3 -I harness/corpus.py results/glsl-all.llvmpipe.jsonl glsl ext/glsl` | `glsl-all.llvmpipe.jsonl` |
| melonDS GL | `SHPROBE_API=gl python3 -I harness/corpus.py results/melonds-gl.jsonl melonds-cases cases/melonds-cases.json` | `melonds-gl.jsonl` |

`glsl-crt.jsonl` (3 shaders) and `glsl-desktop-table.json` (637 desktop rows) are earlier/derived files; `melonds-compute.json` and
`vulkaninfo-full.txt` come from ad hoc commands in the task log (13:55-13:59), not from `run_all.sh`.
The ES path reproduces RetroArch's GLES rewrite of `#version` (130-329 → `300 es`, 330 → `310 es`, >330 → `320 es`, none → `100`),
as mirrored from `gfx/drivers_shader/shader_glsl.c` in `corpus.py`; it is a re-implementation, not RetroArch itself.

### 1.4 How to read the stats

From `bi_gather_stats` in Mesa (`compiler/bifrost/bifrost_compile.c`, quoted in the task log at 14:05): **cycles** is a normalised
estimate that takes the busiest of the arithmetic (24 tuples/cycle), texture (2 messages/cycle), varying (16 x 16-bit channels/cycle) and
load/store (1/cycle) units. The `Arith`, `Tex`, `LD/ST` columns are those per-unit figures, so `cycles = max(...)`. **threads** is 2 when the
shader uses ≤ 32 work registers on v7, else 1 (`full_threads = ctx->arch == 7 && work_reg_count <= 32`): `threads = 1` means half the
resident threads per core, so less latency hiding. Where a record holds several FRAGMENT lines (driver variants), the tables use the highest-cycle one.

## 2. Results at a glance

| Corpus | File | Cases | Compiled (rc 0) | Failed | With Bifrost stats | rc histogram |
|---|---|---|---|---|---|---|
| G52 ES3.1 (glsl) | `glsl-all.jsonl` | 684 | 490 | 194 | 490 | rc0×490, rc3×193, rc4×1 |
| G52 desktop GL3.1 (glsl) | `glsl-all.desktop.jsonl` | 684 | 638 | 46 | 638 | rc0×638, rc3×46 |
| llvmpipe control (glsl) | `glsl-all.llvmpipe.jsonl` | 684 | 489 | 195 | 0 | rc0×489, rc3×193, rc-11×1, rc4×1 |
| RA stock ES | `ra-stock.jsonl` | 16 | 12 | 4 | 12 | rc0×12, rc3×4 |
| RA stock desktop | `ra-stock.desktop.jsonl` | 16 | 16 | 0 | 16 | rc0×16 |
| melonDS GL | `melonds-gl.jsonl` | 14 | 14 | 0 | 14 | rc0×14 |
| crt subset | `glsl-crt.jsonl` | 3 | 3 | 0 | 3 | rc0×3 |

### 2.1 Why 194 libretro shaders fail on the GLES path

| Error (G52, GLES 3.1 path) | Cases |
|---|---|
| `error: initializer of global variable `X' must be a constant expression` | 123 |
| `error: No precision specified in this scope for type `X'` | 29 |
| `error: could not implicitly convert operands to arithmetic operator` | 13 |
| `error: no function with name 'fwidth'` | 7 |
| `error: no matching function for call to `X'; candidates are:` | 3 |
| `error: array constructors forbidden in GLSL ES 1.00 (GLSL 1.20 or GLSL ES 3.00 required)` | 3 |
| `error: syntax error, unexpected NEW_IDENTIFIER` | 3 |
| `error: no function with name 'texelFetch'` | 2 |
| (7 more distinct messages, 1-2 cases each) | 11 |

Most of the `procedural/` shaders (110 of the 194 failures) are Shadertoy ports with non-constant global initialisers and implicit
precision, which GLSL ES 3.x forbids. The remaining classes (`fwidth`/`texelFetch`/`round` without an extension, `%` and array
constructors under `#version 100`, token pasting) are libretro `.glsl` files written for desktop GLSL 1.10-1.50 that do not
survive the ES rewrite. Exceptions worth knowing: `crt-hyllian`, `xbr-lv2`, `lanczos3`, `cubic`, `zfast_crt_geo` all fail on the ES path yet compile on the desktop path (section 5).

### 2.2 The control run

An llvmpipe run of the same 684 cases (`env-control.sh`, no drm-shim) fails **195** cases; **194** of them are the same shaders that fail on the G52, and **0** fail only on the G52. The one extra llvmpipe failure is `crt/shaders/crt-geom.glsl` with rc -11 (a crash inside llvmpipe's JIT), while the G52 path compiles it. So the 194 G52 failures (193 compile, 1 link) come from Mesa's front end (GLSL ES validation), not from the Bifrost back end. No case failed inside the Bifrost compiler.

### 2.3 `MESA: error: export failed`

It is printed by `panfrost_fence_create()` in `src/gallium/drivers/panfrost/pan_fence.c` L119-L123: it calls
`drmSyncobjExportSyncFile(fd, ctx->syncobj, &fd)` and logs this message when that returns an error or fd -1. That is the fence created when the harness
calls `glFinish()` after the draw. Under drm-shim there is no kernel syncobj to export, so the call fails.
Evidence that it is harmless: all 490 compiled ES cases carry the message *and* rc 0 *and* full Bifrost stats (compilation happens when the draw builds the shader variant, before the flush); the llvmpipe control, which has no Panfrost, prints it 0 times. **What I did not do:** trace the shim's syncobj ioctl handler to confirm the exact return value. The cause is therefore "very likely the fake kernel", not proven.

## 3. Cost distribution (G52, GLES path, 490 compiled shaders)

| Fragment cycles | p10 | p25 | p50 | p75 | p90 | p99 | max |
|---|---|---|---|---|---|---|---|
| 490 compiled GLES shaders | 1.38 | 2.21 | 4.62 | 12.83 | 27 | 430 | 880 |

| Cycles bucket | Shaders | Share |
|---|---|---|
| <=2 | 111 | 23% |
| 2-5 | 141 | 29% |
| 5-10 | 89 | 18% |
| 10-20 | 65 | 13% |
| 20-50 | 50 | 10% |
| 50-100 | 17 | 3% |
| >100 | 17 | 3% |

| Occupancy / pressure | Count (of 490) |
|---|---|
| `threads = 1` (more than 32 registers) | 118 |
| `threads = 2` | 372 |
| any spills | 35 |
| contains loops the compiler could not unroll | 23 |

### 3.1 Top 20 most expensive fragment shaders (GLES path)

| # | Shader (fragment, worst variant) | Cycles | Inst | Tex | LD/ST | Regs | Threads | Spill/Fill |
|---|---|---|---|---|---|---|---|---|
| 1 | `nnedi3/shaders/nnedi3-nns64-win8x4-pass2-rgb.glsl` | 880 | 18349 | 16.5 | 880 | 64 | 1 | 41/836 |
| 2 | `nnedi3/shaders/nnedi3-nns64-win8x4-pass1-rgb.glsl` | 750 | 18222 | 16.5 | 750 | 64 | 1 | 40/707 |
| 3 | `xbrz/shaders/6xbrz.glsl` | 674 | 2821 | 10.5 | 674 | 63 | 1 | 169/385 |
| 4 | `nnedi3/shaders/nnedi3-nns32-win8x4-pass2-rgb.glsl` | 496 | 9421 | 16.5 | 496 | 64 | 1 | 41/452 |
| 5 | `xbrz/shaders/5xbrz.glsl` | 452 | 2476 | 10.5 | 452 | 62 | 1 | 94/294 |
| 6 | `nnedi3/shaders/nnedi3-nns32-win8x4-pass1-rgb.glsl` | 430 | 9358 | 16.5 | 430 | 64 | 1 | 40/387 |
| 7 | `nnedi3/shaders/nnedi3-nns16-win8x4-pass2-rgb.glsl` | 304 | 4957 | 16.5 | 304 | 64 | 1 | 41/260 |
| 8 | `nnedi3/shaders/nnedi3-nns16-win8x4-pass1-rgb.glsl` | 270 | 4926 | 16.5 | 270 | 64 | 1 | 40/227 |
| 9 | `nnedi3/shaders/nnedi3-nns64-win8x4-pass1-luma.glsl` | 199.62 | 5866 | 16.5 | 0 | 43 | 1 | 0/0 |
| 10 | `nnedi3/shaders/nnedi3-nns64-win8x4-pass2-luma.glsl` | 199.5 | 5863 | 16.5 | 0 | 43 | 1 | 0/0 |
| 11 | `xbrz/shaders/4xbrz.glsl` | 164 | 2096 | 10.5 | 164 | 64 | 1 | 44/106 |
| 12 | `crt/shaders/crt-lottes.glsl` | 135 | 2253 | 15.5 | 135 | 64 | 1 | 29/106 |
| 13 | `scalenx/shaders/mmpx.glsl` | 117 | 965 | 12.5 | 117 | 64 | 1 | 36/85 |
| 14 | `crt/shaders/crt-mattias.glsl` | 111 | 2812 | 111 | 0 | 51 | 1 | 0/0 |
| 15 | `nnedi3/shaders/nnedi3-nns32-win8x4-pass1-luma.glsl` | 101.88 | 3018 | 16.5 | 0 | 44 | 1 | 0/0 |
| 16 | `nnedi3/shaders/nnedi3-nns32-win8x4-pass2-luma.glsl` | 101.75 | 3015 | 16.5 | 0 | 44 | 1 | 0/0 |
| 17 | `scalefx/shaders/scalefx-pass3.glsl` | 101 | 1068 | 6.5 | 101 | 64 | 1 | 48/53 |
| 18 | `sharpen/shaders/adaptive-sharpen-pass2.glsl` | 88 | 1080 | 12.5 | 88 | 64 | 1 | 18/24 |
| 19 | `denoisers/shaders/median_5x5.glsl` | 82 | 693 | 12.5 | 82 | 64 | 1 | 39/43 |
| 20 | `xbrz/shaders/xbrz-freescale-multipass/xbrz-freescale-pass0.glsl` | 78.5 | 2594 | 18.5 | 4 | 64 | 1 | 2/2 |

All twenty run at `threads = 1`. 15 spill, and for the xBRZ and NNEDI3 rgb passes `cycles` is set by LD/ST (the spill traffic),
not by arithmetic: for example `6xbrz` has 71 arith cycles but 674 LD/ST cycles. Reducing register pressure would help those
more than reducing maths.

## 4. Frame budget for 720x720 at 60 fps (assumptions stated)

**Assumptions, none measured on hardware:**
- Panel 720x720 = 518,400 pixels (`retroarch.cfg` `video_fullscreen_x/y`); target 60 fps = 16.67 ms.
- G52 MP2 at the top devfreq step **850 MHz** (research 02 s1.4: "devfreq 384-850 MHz"). Not verified that the board sustains it (thermals, cpufreq open question Q1 in research 02 s0).
- Interpretation of `cycles`: **one shader-core cycle per fragment at the stated unit throughputs**, so a full-screen pass costs
  `cycles x pixels / (clock x cores)`. This reading of Mesa's "normalised cycles" is **my assumption**; I did not verify it against ARM's own tools or hardware. If the real figure is a multiple of this, scale the milliseconds below by the same factor. Rankings between shaders do not depend on it.
- Ideal case: both cores busy, perfect scheduling, every pixel shaded once (no overdraw), no stalls on memory.

Total core cycles per frame = 850 MHz x 2 cores / 60 fps = **28.33 M**, i.e. **54.7 cycles per pixel per frame** if the GPU did nothing else. Reserving half for the emulator's own GL work, the menu, the blit and memory stalls (my rule of thumb, not a measurement) leaves **~27 cycles per pixel** for post-processing shaders, summed over all passes that run at output resolution.

| Fragment cycles | ms per full 720x720 pass | Share of 16.67 ms |
|---|---|---|
| 1 | 0.30 | 2% |
| 2 | 0.61 | 4% |
| 4 | 1.22 | 7% |
| 8 | 2.44 | 15% |
| 16 | 4.88 | 29% |
| 27 | 8.23 | 49% |
| 55 | 16.77 | 101% |
| 135 | 41.17 | 247% |
| 674 | 205.53 | 1233% |

Multipass presets add: passes that render at source resolution (for example 240x160) cost far less than the 720x720 final pass, so only
output-resolution passes should be counted at full price.

## 5. Selected shaders and what they cost

| Shader | Kind | Cycles | Regs | Threads | Spill/Fill | ms/pass (720²) |
|---|---|---|---|---|---|---|
| `interpolation/shaders/sharp-bilinear.glsl` | scaling | 1.67 | 14 | 2 | 0/0 | 0.51 |
| `handheld/shaders/zfast_lcd.glsl` | LCD | 1.92 | 11 | 2 | 0/0 | 0.58 |
| `handheld/shaders/lcd3x.glsl` | LCD | 2.92 | 16 | 2 | 0/0 | 0.89 |
| `handheld/shaders/color/gbc-color.glsl` | colour | 2.58 | 10 | 2 | 0/0 | 0.79 |
| `handheld/shaders/color/gba-color.glsl` | colour | 2.58 | 10 | 2 | 0/0 | 0.79 |
| `scanlines/shaders/scanline.glsl` | scanlines | 1.67 | 12 | 2 | 0/0 | 0.51 |
| `crt/shaders/zfast_crt.glsl` | CRT | 2.29 | 13 | 2 | 0/0 | 0.70 |
| `crt/shaders/crt-pi.glsl` | CRT | 3.46 | 14 | 2 | 0/0 | 1.05 |
| `scalenx/shaders/scale2x.glsl` | pixel-art scaler | 3.42 | 26 | 2 | 0/0 | 1.04 |
| `hqx/single-pass/shader-files/hq2x.glsl` | pixel-art scaler | 8.88 | 41 | 1 | 0/0 | 2.71 |
| `windowed/shaders/lanczos3.glsl` | scaling | 8.46 | 24 | 2 | 0/0 | 2.58 |
| `crt/shaders/crt-easymode.glsl` | CRT | 14.38 | 34 | 1 | 0/0 | 4.38 |
| `handheld/shaders/lcd-cgwg/lcd-grid-v2.glsl` | LCD | 19.75 | 31 | 2 | 0/0 | 6.02 |
| `crt/shaders/crt-geom.glsl` | CRT | 28.38 | 46 | 1 | 0/0 | 8.65 |
| `windowed/shaders/jinc2.glsl` | scaling | 19.54 | 63 | 1 | 7/7 | 5.96 |
| `crt/shaders/crt-aperture.glsl` | CRT | 26.17 | 40 | 1 | 0/0 | 7.98 |
| `crt/shaders/crt-lottes.glsl` | CRT | 135 | 64 | 1 | 29/106 | 41.17 |
| `xbrz/shaders/4xbrz.glsl` | pixel-art scaler | 164 | 64 | 1 | 44/106 | 50.01 |
| `xbrz/shaders/6xbrz.glsl` | pixel-art scaler | 674 | 63 | 1 | 169/385 | 205.53 |

### 5.1 GLES 3.1 path versus desktop GL 3.1 path (`gl` vs `glcore`-style desktop contexts)

Across 475 shaders that compile on both: 98 have identical cycles, the desktop path is cheaper in
326 and the ES path in 51 (median desktop/ES cycles ratio 0.95). But the desktop path has **more shaders at half
occupancy** (threads=1: 235 of 638 against 118 of 490) and **more with spills** (71 against 35).
My guess is that ES shaders carry `precision mediump` (16-bit values, fewer registers) while the desktop path is all fp32; I did not verify this by diffing the generated code.

| Shader (largest gaps) | ES cycles | ES thr | Desktop cycles | Desktop thr |
|---|---|---|---|---|
| `omniscale/shaders/omniscale.glsl` | 40.54 | 1 | 385 | 1 |
| `xbrz/shaders/4xbrz.glsl` | 164 | 1 | 482 | 1 |
| `xbrz/shaders/6xbrz.glsl` | 674 | 1 | 924 | 1 |
| `xbrz/shaders/5xbrz.glsl` | 452 | 1 | 646 | 1 |
| `handheld/shaders/authentic_gbc/authentic_gbc.glsl` | 43.5 | 1 | 209 | 1 |
| `handheld/shaders/authentic_gbc/authentic_gbc_single_pass.glsl` | 46.54 | 1 | 209 | 1 |
| `xbr/shaders/xbr-lv3-multipass/xbr-lv3-pass0.glsl` | 23.08 | 1 | 138 | 1 |
| `scalenx/shaders/mmpx.glsl` | 117 | 1 | 191 | 1 |

The desktop path compiles 148 more shaders (163 compile only on desktop, 15 only on ES) because the libretro `.glsl` files are written for desktop GLSL 1.10-1.50:

| Shader | GLES path | Desktop cycles | Desktop thr |
|---|---|---|---|
| `crt/shaders/hyllian/crt-hyllian.glsl` | fails to compile | 329 | 1 |
| `xbr/shaders/xbr-lv2.glsl` | fails to compile | 52 | 1 |
| `windowed/shaders/lanczos3-x.glsl` | fails to compile | 8.67 | 1 |
| `cubic/shaders/cubic.glsl` | fails to compile | 8 | 2 |
| `crt/shaders/zfast_crt_geo.glsl` | fails to compile | 4.5 | 2 |
| `interpolation/shaders/uniform-nearest.glsl` | fails to compile | 3.46 | 2 |
| `crt/shaders/mame_hlsl/shaders/mame_scanline.glsl` | fails to compile | 2.25 | 2 |

Caveat: `crt-hyllian` costs 329 cycles on the desktop path, so it compiling does not make it usable. The desktop run used a **GL 3.1** context (`SHPROBE_GLVER` default), not a 3.2 core context; whether Panfrost on this G52 would actually give RetroArch's `glcore` driver a core context was **not tested here**.

### 5.2 RetroArch's own pipeline shaders (menu, XMB)

| RetroArch pipeline shader (fragment) | GLES: result | GLES cycles | GLES thr | Desktop 3.1: result | Desktop cycles |
|---|---|---|---|---|---|
| `video-default__gl` | ok | 0.71 | 2 | ok | 0.71 |
| `video-default__glcore` | ok | 0.5 | 2 | ok | 0.5 |
| `menu-blend__gl` | ok | 1.17 | 2 | ok | 1.04 |
| `menu-blend__glcore` | ok | 0.75 | 2 | ok | 0.75 |
| `xmb-ribbon__gl` | ok | 1.71 | 2 | ok | 1.71 |
| `xmb-ribbon__glcore` | ok | 1.71 | 2 | ok | 1.71 |
| `xmb-ribbon-simple__gl` | ok | 0.67 | 2 | ok | 0.71 |
| `xmb-ribbon-simple__glcore` | ok | 0.12 | 2 | ok | 0.12 |
| `xmb-snow-simple__gl` | ok | 15.25 | 2 | ok | 15.29 |
| `xmb-snow-simple__glcore` | **compile fail** (vertex) | - | - | ok | 15.29 |
| `xmb-snow__gl` | ok | 15.25 | 2 | ok | 15.29 |
| `xmb-snow__glcore` | **compile fail** (vertex) | - | - | ok | 15.29 |
| `xmb-bokeh__gl` | ok | 11.88 | 2 | ok | 11.88 |
| `xmb-bokeh__glcore` | **compile fail** (vertex) | - | - | ok | 11.88 |
| `xmb-snowflake__gl` | ok | 5.17 | 2 | ok | 5.12 |
| `xmb-snowflake__glcore` | **compile fail** (vertex) | - | - | ok | 5.12 |

The four failures are the `glcore` snow/bokeh/snowflake **vertex** shaders after the GLES rewrite (`illegal use of reserved word`: the harness
feeds RetroArch's GLES 1.00-style source behind a `#version 3xx es` line, as `shader_glsl.c` does for `glcore` on GLES builds). The `gl` variants of all eight pairs compile. That supports `video_driver = "gl"` in
`build/rootfs/overlay/etc/retroarch.cfg`. The XMB snow/bokeh pipelines are the costliest menu fragments (15.25 and 11.88 cycles), roughly 17-21x the stock `video-default` fragment (0.71); use a plain menu or turn the pipeline animation off.

## 6. melonDS

Which shaders the melonDS-ds libretro core would use depends on the renderer: plorpOS ships it **software-rendered** (`-DENABLE_OPENGL=OFF`); the core's `OpenGL` renderer uses the shaders below.

### 6.1 GL renderer shaders (14 shaders, all compile, desktop GL 3.1 context)

| melonDS GL shader (fragment) | Cycles | Inst | Arith | Tex | LD/ST | Regs | Threads | Spills |
|---|---|---|---|---|---|---|---|---|
| `2DCompositor` | 44.92 | 1593 | 44.92 | 29 | 38 | 55 | 1 | 0 |
| `2DLayerPre` | 24 | 349 | 9.42 | 9.5 | 24 | 13 | 2 | 0 |
| `2DSprite` | 16 | 204 | 5.12 | 1.5 | 16 | 16 | 2 | 0 |
| `2DSpritePre` | 10 | 122 | 3.21 | 3 | 10 | 8 | 2 | 0 |
| `3DClear` | 0.46 | 16 | 0.46 | 0 | 0 | 6 | 2 | 0 |
| `3DClearBitmap` | 1 | 24 | 0.5 | 1 | 0 | 7 | 2 | 0 |
| `3DFinalPass_Edge` | 5 | 86 | 1.96 | 5 | 2 | 16 | 2 | 0 |
| `3DFinalPass_Fog` | 4 | 44 | 1.04 | 1 | 4 | 8 | 2 | 0 |
| `3DRender_Z` | 4 | 132 | 3.5 | 1.5 | 4 | 19 | 2 | 0 |
| `3DRender_W` | 4 | 132 | 3.46 | 1.5 | 4 | 20 | 2 | 0 |
| `Capture` | 6 | 118 | 3.04 | 2 | 6 | 12 | 2 | 0 |
| `CaptureDownscale` | 0.54 | 24 | 0.54 | 0.5 | 0 | 8 | 2 | 0 |
| `FinalPass` | 9 | 180 | 4.96 | 1 | 9 | 19 | 2 | 0 |
| `melondsds_hw_screen(libretro frontend)` | 3 | 33 | 0.75 | 0.5 | 3 | 13 | 2 | 0 |

`2DCompositor` is the only one at 1 thread and over 25 cycles. From its name it composites the 2D layers of the two 256x192 screens; I did not read the renderer to establish how often or at what resolution it runs, so the per-frame cost is not modelled.

### 6.2 Compute renderer shaders (33 shaders)

Only `ClearIndirectWorkCount_` links (1 cycle estimate, 2 threads; in the first run it crashed the probe with rc -11, a later run with the probe modified to skip dispatch linked it OK, so the stored record has rc -11 but the task log shows the link). The other 32 are rejected by Mesa's GLSL front end after the GLES 3.1 rewrite:

| melonDS compute shader error (GLES 3.1 `#version 310 es` rewrite) | Shaders |
|---|---|
| `error: type error in array constructor: expected: uint, found int` | 18 |
| `error: initializer of type int cannot be assigned to variable of type ` | 12 |
| `error: operands of `==' must have the same type` | 1 |
| `error: could not implicitly convert operands to arithmetic operator` | 1 |

The desktop GL 3.1 attempt (`gl_rc`) also failed for all, expectedly: compute shaders need GL 4.3. Treat the melonDS compute renderer as **not available** on this stack until the type errors are patched in the shader generator. Whether the errors come from melonDS or from my `#version 310 es` conversion of its desktop sources is **not determined**.

## 7. What panvk says about the G52 (`vulkaninfo-full.txt`)

| Field | Value |
|---|---|
| Warning | `WARNING: panvk is not a conformant Vulkan implementation, testing use only.` (line 2) |
| `deviceName` | `Mali-G52 MC4` (line 56) |
| `deviceID` / `vendorID` | `0x72120000` / `0x13b5` (ARM) |
| `deviceType` | `PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU` |
| `apiVersion` | **1.0.354** (line 51) |
| `driverVersion` / `driverInfo` | 26.2.6 / `Mesa 26.2.6`, `driverID = DRIVER_ID_MESA_PANVK` |
| `conformanceVersion` | **0.0.0.0** (no conformance claim) |
| Instance version | 1.4.341 (loader); ICD manifest says `api_version 1.4.354` |

Vulkan only enumerates the device with `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`. Without it panvk returns `VK_ERROR_INCOMPATIBLE_DRIVER` with "panvk is not well-tested on v7" (`src/panfrost/vulkan/panvk_physical_device.c` L463-L470, architectures 6, 7 and 14 are gated; 10, 12, 13 are not). The device exposes core Vulkan **1.0** on Bifrost v7. Conclusion: Vulkan on the G52 is experimental; keep the GL driver.

## 8. Recommendations

### 8.1 RetroArch video settings

These agree with the existing `retroarch.cfg` and add cost numbers; nothing below was run on a device.

| Setting | Recommendation | Why (data) |
|---|---|---|
| `video_driver` | keep `"gl"` | all 8 `gl` pipeline pairs compile on the GLES path; 4 of 8 `glcore` pairs fail the GLES rewrite (5.2). Vulkan: section 7. |
| `video_shader_enable` | keep `false` as the default; opt in per system | p50 libretro shader is 4.62 cycles, but 118 of 490 run half-occupancy and the maximum is 190x the median |
| `video_smooth` | keep `false` | not covered by this data; unchanged |
| `video_threaded` / `video_hard_sync` | keep `false` | no shader data on this; unchanged |
| Shader budget | sum of output-resolution passes ≤ ~27 cycles | section 4, my 50% rule |
| Pixel-art scalers | avoid `xbrz` 4x-6x and `nnedi3` | spills dominate their cost (section 3.1) |
| Menu | disable the XMB snow/bokeh pipeline | 5.2 |

### 8.2 Per-system shaders (G52 estimates; the emulator's own cost is not included)

| System family | Source res (typical) | Recommended | Fallback | Never |
|---|---|---|---|---|
| NES, SNES, Genesis, PC Engine, Master System | 256x224 and similar | `zfast_crt` (2.29) or `crt-pi` (3.46) | `sharp-bilinear` (1.67) | `crt-lottes`, `xbrz` |
| Game Boy, Game Boy Color | 160x144 | `lcd3x` (2.92) or `zfast_lcd` (1.92) + `gbc-color` (2.58) | `sharp-bilinear` | `lcd-cgwg` multi-pass presets |
| Game Boy Advance, Neo Geo Pocket, WonderSwan | 240x160 | `gba-color` (2.58) alone, or with `zfast_lcd` | `sharp-bilinear` | `xbrz`, `jinc2` |
| Arcade (FBNeo, MAME-class) | 320x240 and similar | `crt-pi` or `zfast_crt` | `crt-easymode` (14.38, 1 thread) if the core is light | `crt-royale`, `crt-lottes` |
| Nintendo DS | 2 x 256x192 | no shader; software renderer | `sharp-bilinear` | melonDS compute renderer |
| PS1, N64, Dreamcast, PSP (3D) | varies | no shader; the core's GL cost is unmeasured here | at most `sharp-bilinear` | any full-screen filter over ~5 cycles until profiled on the device |

Order of preference inside a row is by increasing GPU cost, not by looks.

## 9. What this cannot tell us

- **No execution.** Nothing ran on a G52. Cycle counts are compiler estimates; actual time depends on scheduling, caches and driver behaviour I cannot see.
- **No memory bandwidth.** Texture fetches are counted as issue slots only. A 720x720 RGBA target at 60 fps is ~124 MB/s per full-screen write (518400x4 bytes x 60 = 124 MB/s) before reads, tiling, blending or LPDDR contention; none of that is in these numbers. Mali is tile-based, which helps; I did not model it.
- **No thermals or clocks.** 850 MHz is the top devfreq step quoted by base618 docs. Research 02 s0 item 4 notes that CPU frequency scaling may not follow requests on this kernel; GPU devfreq behaviour is unverified.
- **MC4, not MP2.** The shim fakes 4 cores. The compiler output should not depend on core count (not tested, e.g. by setting `PAN_GPU_ID`); throughput does, and section 4 uses 2 cores by hand.
- **Overdraw, blending, MSAA and the emulator's own GL work** are outside these shader-only numbers.
- **The ES rewrite is a re-implementation of RetroArch's** (`corpus.py`), not RetroArch itself; the desktop path assumes a desktop-GL RetroArch build, and which build the rootfs ships (the Arch package vs a GLES-only build) is **not determined here**.
- **A GL 3.2 core context (`glcore`)** was never created; only 3.1.
- **Correct output is not checked.** A shader that compiles may still render wrongly; no pixels were read back.
- **Driver and compiler versions move.** Mesa 26.2.6 only; the board will run whatever Mesa the rootfs ships.

## 10. Files

| File | Role |
|---|---|
| `build/work/gpu-shim/results/*.jsonl, *.json, vulkaninfo-full.txt` | raw results (input to this doc) |
| `build/work/gpu-shim/harness/{env.sh,env-control.sh,run_all.sh,corpus.py,shprobe.c}` | the harness (written by the prior run) |
| `build/work/gpu-shim/report/analyze.py` | aggregation; writes `report/summary.json` |
| `build/work/gpu-shim/report/build_doc.py` | renders this document from `summary.json` and the results |
| `build/work/gpu-shim/report/summary.json` | derived summary used for every number above |
