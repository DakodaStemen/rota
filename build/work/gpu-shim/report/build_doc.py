#!/usr/bin/env python3
"""Render docs/research/05-gpu-shim.md from report/summary.json + results/*.jsonl.
Run (repo root): python3 -I build/work/gpu-shim/report/analyze.py && python3 -I build/work/gpu-shim/report/build_doc.py
Every number in the tables comes from the result files; prose carries only the cited assumptions."""
import json, re, statistics
from pathlib import Path

HERE = Path(__file__).resolve().parent
GS = HERE.parent
REPO = GS.parent.parent.parent
S = json.loads((HERE / "summary.json").read_text())
R = GS / "results"

def load(n): return [json.loads(l) for l in open(R / n)]
def frag(r):
    f = [s for s in r["stats"] if s.get("stage") == "FRAGMENT" and "cycles" in s]
    return max(f, key=lambda s: s["cycles"]) if f else None
def fmt(x): return f"{x:.2f}".rstrip("0").rstrip(".") if isinstance(x, float) else str(x)
def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)

ES = {r["name"]: r for r in load("glsl-all.jsonl")}
DK = {r["name"]: r for r in load("glsl-all.desktop.jsonl")}
def cyc(d, n):
    r = d.get(n)
    f = frag(r) if r and r["rc"] == 0 else None
    return f

C = S["corpora"]; es = S["es"]; B = S["budget"]; gv = S["gl_vs_es"]

# ---------- corpus table
corp_rows = []
for k, v in C.items():
    corp_rows.append([k, f"`{v['file']}`", v["total"], v["rc0"], v["failed"], v["with_frag_stats"],
                      ", ".join(f"rc{a}×{b}" for a, b in v["rc_hist"].items())])
t_corp = table(["Corpus", "File", "Cases", "Compiled (rc 0)", "Failed", "With Bifrost stats", "rc histogram"], corp_rows)

# ---------- failure reasons (ES)
fr = sorted(es["cat_counts"].items(), key=lambda kv: -kv[1])
t_fail = table(["Error (G52, GLES 3.1 path)", "Cases"], [[f"`{k}`", v] for k, v in fr[:8]] + [["(7 more distinct messages, 1-2 cases each)", sum(v for _, v in fr[8:])]])

# ---------- percentile / bucket
t_dist = table(["Fragment cycles", "p10", "p25", "p50", "p75", "p90", "p99", "max"],
               [[ "490 compiled GLES shaders" ] + [fmt(es[k]) for k in ("p10", "p25", "p50", "p75", "p90", "p99", "max")]])
t_bucket = table(["Cycles bucket", "Shaders", "Share"], [[k, v, f"{100*v/es['n']:.0f}%"] for k, v in es["buckets"].items()])

# ---------- top 20
t_top = table(["#", "Shader (fragment, worst variant)", "Cycles", "Inst", "Tex", "LD/ST", "Regs", "Threads", "Spill/Fill"],
              [[i + 1, f"`{r['name']}`", fmt(r["cycles"]), r["inst"], fmt(r["tex"]), fmt(r["ldst"]), r["regs"], r["threads"], f"{r['spills']}/{r['fills']}"]
               for i, r in enumerate(S["top20_es"])])

# ---------- frame cost table
def ms(c): return c * B["pixels"] / (B["clock_hz"] * B["cores"]) * 1000
t_ms = table(["Fragment cycles", "ms per full 720x720 pass", "Share of 16.67 ms"],
             [[c, f"{ms(c):.2f}", f"{100*ms(c)/(1000/B['fps']):.0f}%"] for c in (1, 2, 4, 8, 16, 27, 55, 135, 674)])

# ---------- picks table
picks = [("interpolation/shaders/sharp-bilinear.glsl", "scaling"), ("handheld/shaders/zfast_lcd.glsl", "LCD"),
         ("handheld/shaders/lcd3x.glsl", "LCD"), ("handheld/shaders/color/gbc-color.glsl", "colour"),
         ("handheld/shaders/color/gba-color.glsl", "colour"), ("scanlines/shaders/scanline.glsl", "scanlines"),
         ("crt/shaders/zfast_crt.glsl", "CRT"), ("crt/shaders/crt-pi.glsl", "CRT"), ("scalenx/shaders/scale2x.glsl", "pixel-art scaler"),
         ("hqx/single-pass/shader-files/hq2x.glsl", "pixel-art scaler"), ("windowed/shaders/lanczos3.glsl", "scaling"),
         ("crt/shaders/crt-easymode.glsl", "CRT"), ("handheld/shaders/lcd-cgwg/lcd-grid-v2.glsl", "LCD"),
         ("crt/shaders/crt-geom.glsl", "CRT"), ("windowed/shaders/jinc2.glsl", "scaling"), ("crt/shaders/crt-aperture.glsl", "CRT"),
         ("crt/shaders/crt-lottes.glsl", "CRT"), ("xbrz/shaders/4xbrz.glsl", "pixel-art scaler"), ("xbrz/shaders/6xbrz.glsl", "pixel-art scaler")]
pk = []
for n, kind in picks:
    f = cyc(ES, n)
    pk.append([f"`{n}`", kind, fmt(f["cycles"]), f["regs"], f["threads"], f"{f['spills']}/{f['fills']}", f"{ms(f['cycles']):.2f}"])
t_picks = table(["Shader", "Kind", "Cycles", "Regs", "Threads", "Spill/Fill", "ms/pass (720²)"], pk)

# ---------- es vs desktop
gd = []
for e in gv["biggest_diffs"]:
    fe, fd = cyc(ES, e["name"]), cyc(DK, e["name"])
    gd.append([f"`{e['name']}`", fmt(fe["cycles"]), fe["threads"], fmt(fd["cycles"]), fd["threads"]])
t_gd = table(["Shader (largest gaps)", "ES cycles", "ES thr", "Desktop cycles", "Desktop thr"], gd)
only_desktop_hot = []
for n in ("crt/shaders/hyllian/crt-hyllian.glsl", "xbr/shaders/xbr-lv2.glsl", "windowed/shaders/lanczos3-x.glsl", "cubic/shaders/cubic.glsl",
          "crt/shaders/zfast_crt_geo.glsl", "interpolation/shaders/uniform-nearest.glsl", "crt/shaders/mame_hlsl/shaders/mame_scanline.glsl"):
    fd = cyc(DK, n)
    only_desktop_hot.append([f"`{n}`", "fails to compile" if ES[n]["rc"] else "ok", fmt(fd["cycles"]) if fd else "fails", fd["threads"] if fd else "-"])
t_only = table(["Shader", "GLES path", "Desktop cycles", "Desktop thr"], only_desktop_hot)

# ---------- RA stock
rs, rd = S["ra_stock"]["es"], S["ra_stock"]["desktop"]
dmap = {r["name"]: r for r in rd}
t_ra = table(["RetroArch pipeline shader (fragment)", "GLES: result", "GLES cycles", "GLES thr", "Desktop 3.1: result", "Desktop cycles"],
             [[f"`{r['name']}`", "ok" if r["rc"] == 0 else "**compile fail** (vertex)", fmt(r["cycles"]) if r["cycles"] is not None else "-",
               r["threads"] if r["threads"] else "-", "ok" if dmap[r["name"]]["rc"] == 0 else "fail", fmt(dmap[r["name"]]["cycles"])] for r in rs])

# ---------- melonDS
mg = S["melonds_gl"]
t_mg = table(["melonDS GL shader (fragment)", "Cycles", "Inst", "Arith", "Tex", "LD/ST", "Regs", "Threads", "Spills"],
             [[f"`{r['name']}`", fmt(r["cycles"]), r["inst"], fmt(r["arith"]), fmt(r["tex"]), fmt(r["ldst"]), r["regs"], r["threads"], r["spills"]] for r in mg])
mcs = S["melonds_compute_summary"]
t_mc = table(["melonDS compute shader error (GLES 3.1 `#version 310 es` rewrite)", "Shaders"], [[f"`{k}`", v] for k, v in mcs["errs"]])
crt = {r["name"]: r for r in S["crt"]}

doc = f"""# Phase 1 research 05: GPU shader cost on Mali-G52 via Mesa drm-shim

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

1. **{C['G52 ES3.1 (glsl)']['rc0']} of {C['G52 ES3.1 (glsl)']['total']} libretro GLSL shaders compile for the G52 on the GLES 3.1 path; {C['G52 desktop GL3.1 (glsl)']['rc0']} of {C['G52 desktop GL3.1 (glsl)']['total']} on the desktop GL 3.1 path.** The failures are GLSL source problems, not Panfrost problems: an llvmpipe control run fails the same {S['llvmpipe_control']['both']} shaders (section 2.2).
2. **The median libretro shader is cheap:** p50 = {fmt(es['p50'])} fragment cycles, p90 = {fmt(es['p90'])}, but the tail is long (p99 = {fmt(es['p99'])}, max = {fmt(es['max'])}). {es['threads1']} of {es['n']} compiled shaders ({100*es['threads1']//es['n']}%) use more than 32 registers and so run at half occupancy (`threads = 1`); {es['spills']} spill registers to memory (spills > 0).
3. **Cheap enough for 720x720@60 (≤ ~4 cycles, 2 threads, no spills):** `sharp-bilinear` ({fmt(frag(ES['interpolation/shaders/sharp-bilinear.glsl'])['cycles'])}), `zfast_lcd`, `lcd3x`, `zfast_crt` ({fmt(frag(ES['crt/shaders/zfast_crt.glsl'])['cycles'])}), `crt-pi` ({fmt(frag(ES['crt/shaders/crt-pi.glsl'])['cycles'])}), `scale2x`, `gba-color`/`gbc-color`. **Avoid:** `crt-lottes` ({fmt(frag(ES['crt/shaders/crt-lottes.glsl'])['cycles'])}, {frag(ES['crt/shaders/crt-lottes.glsl'])['spills']} spills), `xbrz` 4x-6x (164-674), `nnedi3`, `jinc2` (spills).
4. **melonDS's compute renderer does not compile on this stack** ({mcs['total'] - mcs['with_stats']} of {mcs['total']} compute shaders fail; one trivial one links). Its GL (fragment) renderer shaders all compile and are cheap except the 2D compositor ({fmt(mg[0]['cycles'])} cycles, threads {mg[0]['threads']}). Use the software renderer (plorpOS builds melonDS with `-DENABLE_OPENGL=OFF`, research 02 s2.4).
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

{t_corp}

### 2.1 Why 194 libretro shaders fail on the GLES path

{t_fail}

Most of the `procedural/` shaders (110 of the 194 failures) are Shadertoy ports with non-constant global initialisers and implicit
precision, which GLSL ES 3.x forbids. The remaining classes (`fwidth`/`texelFetch`/`round` without an extension, `%` and array
constructors under `#version 100`, token pasting) are libretro `.glsl` files written for desktop GLSL 1.10-1.50 that do not
survive the ES rewrite. Exceptions worth knowing: `crt-hyllian`, `xbr-lv2`, `lanczos3`, `cubic`, `zfast_crt_geo` all fail on the ES path yet compile on the desktop path (section 5).

### 2.2 The control run

An llvmpipe run of the same 684 cases (`env-control.sh`, no drm-shim) fails **{S['llvmpipe_control']['llvmpipe_fail']}** cases; **{S['llvmpipe_control']['both']}** of them are the same shaders that fail on the G52, and **0** fail only on the G52. The one extra llvmpipe failure is `crt/shaders/crt-geom.glsl` with rc -11 (a crash inside llvmpipe's JIT), while the G52 path compiles it. So the {C['G52 ES3.1 (glsl)']['failed']} G52 failures ({C['G52 ES3.1 (glsl)']['rc_hist'].get('3', 0)} compile, {C['G52 ES3.1 (glsl)']['rc_hist'].get('4', 0)} link) come from Mesa's front end (GLSL ES validation), not from the Bifrost back end. No case failed inside the Bifrost compiler.

### 2.3 `MESA: error: export failed`

It is printed by `panfrost_fence_create()` in `src/gallium/drivers/panfrost/pan_fence.c` L119-L123: it calls
`drmSyncobjExportSyncFile(fd, ctx->syncobj, &fd)` and logs this message when that returns an error or fd -1. That is the fence created when the harness
calls `glFinish()` after the draw. Under drm-shim there is no kernel syncobj to export, so the call fails.
Evidence that it is harmless: all {C['G52 ES3.1 (glsl)']['rc0']} compiled ES cases carry the message *and* rc 0 *and* full Bifrost stats (compilation happens when the draw builds the shader variant, before the flush); the llvmpipe control, which has no Panfrost, prints it 0 times. **What I did not do:** trace the shim's syncobj ioctl handler to confirm the exact return value. The cause is therefore "very likely the fake kernel", not proven.

## 3. Cost distribution (G52, GLES path, {es['n']} compiled shaders)

{t_dist}

{t_bucket}

| Occupancy / pressure | Count (of {es['n']}) |
|---|---|
| `threads = 1` (more than 32 registers) | {es['threads1']} |
| `threads = 2` | {es['threads2']} |
| any spills | {es['spills']} |
| contains loops the compiler could not unroll | {es['loops']} |

### 3.1 Top 20 most expensive fragment shaders (GLES path)

{t_top}

All twenty run at `threads = 1`. {sum(1 for r in S['top20_es'] if r['spills']>0)} spill, and for the xBRZ and NNEDI3 rgb passes `cycles` is set by LD/ST (the spill traffic),
not by arithmetic: for example `6xbrz` has 71 arith cycles but 674 LD/ST cycles. Reducing register pressure would help those
more than reducing maths.

## 4. Frame budget for 720x720 at 60 fps (assumptions stated)

**Assumptions, none measured on hardware:**
- Panel 720x720 = {B['pixels']:,} pixels (`retroarch.cfg` `video_fullscreen_x/y`); target 60 fps = 16.67 ms.
- G52 MP2 at the top devfreq step **850 MHz** (research 02 s1.4: "devfreq 384-850 MHz"). Not verified that the board sustains it (thermals, cpufreq open question Q1 in research 02 s0).
- Interpretation of `cycles`: **one shader-core cycle per fragment at the stated unit throughputs**, so a full-screen pass costs
  `cycles x pixels / (clock x cores)`. This reading of Mesa's "normalised cycles" is **my assumption**; I did not verify it against ARM's own tools or hardware. If the real figure is a multiple of this, scale the milliseconds below by the same factor. Rankings between shaders do not depend on it.
- Ideal case: both cores busy, perfect scheduling, every pixel shaded once (no overdraw), no stalls on memory.

Total core cycles per frame = {B['clock_hz']/1e6:.0f} MHz x {B['cores']} cores / {B['fps']} fps = **{B['core_cycles_per_frame']/1e6:.2f} M**, i.e. **{B['cycles_per_pixel_100pct']:.1f} cycles per pixel per frame** if the GPU did nothing else. Reserving half for the emulator's own GL work, the menu, the blit and memory stalls (my rule of thumb, not a measurement) leaves **~{B['cycles_per_pixel_50pct']:.0f} cycles per pixel** for post-processing shaders, summed over all passes that run at output resolution.

{t_ms}

Multipass presets add: passes that render at source resolution (for example 240x160) cost far less than the 720x720 final pass, so only
output-resolution passes should be counted at full price.

## 5. Selected shaders and what they cost

{t_picks}

### 5.1 GLES 3.1 path versus desktop GL 3.1 path (`gl` vs `glcore`-style desktop contexts)

Across {gv['both']} shaders that compile on both: {gv['identical_cycles']} have identical cycles, the desktop path is cheaper in
{gv['desktop_cheaper']} and the ES path in {gv['es_cheaper']} (median desktop/ES cycles ratio {gv['median_ratio_desktop_over_es']:.2f}). But the desktop path has **more shaders at half
occupancy** (threads=1: {gv['desktop_threads1']} of {C['G52 desktop GL3.1 (glsl)']['rc0']} against {gv['es_threads1']} of {C['G52 ES3.1 (glsl)']['rc0']}) and **more with spills** ({gv['desktop_spill']} against {gv['es_spill']}).
My guess is that ES shaders carry `precision mediump` (16-bit values, fewer registers) while the desktop path is all fp32; I did not verify this by diffing the generated code.

{t_gd}

The desktop path compiles {C['G52 desktop GL3.1 (glsl)']['rc0'] - C['G52 ES3.1 (glsl)']['rc0']} more shaders ({gv['only_desktop']} compile only on desktop, {gv['only_es']} only on ES) because the libretro `.glsl` files are written for desktop GLSL 1.10-1.50:

{t_only}

Caveat: `crt-hyllian` costs 329 cycles on the desktop path, so it compiling does not make it usable. The desktop run used a **GL 3.1** context (`SHPROBE_GLVER` default), not a 3.2 core context; whether Panfrost on this G52 would actually give RetroArch's `glcore` driver a core context was **not tested here**.

### 5.2 RetroArch's own pipeline shaders (menu, XMB)

{t_ra}

The four failures are the `glcore` snow/bokeh/snowflake **vertex** shaders after the GLES rewrite (`illegal use of reserved word`: the harness
feeds RetroArch's GLES 1.00-style source behind a `#version 3xx es` line, as `shader_glsl.c` does for `glcore` on GLES builds). The `gl` variants of all eight pairs compile. That supports `video_driver = "gl"` in
`build/rootfs/overlay/etc/retroarch.cfg`. The XMB snow/bokeh pipelines are the costliest menu fragments ({fmt(rs[8]['cycles'])} and {fmt(rs[12]['cycles'])} cycles), roughly 17-21x the stock `video-default` fragment ({fmt(rs[0]['cycles'])}); use a plain menu or turn the pipeline animation off.

## 6. melonDS

Which shaders the melonDS-ds libretro core would use depends on the renderer: plorpOS ships it **software-rendered** (`-DENABLE_OPENGL=OFF`); the core's `OpenGL` renderer uses the shaders below.

### 6.1 GL renderer shaders ({len(mg)} shaders, all compile, desktop GL 3.1 context)

{t_mg}

`2DCompositor` is the only one at 1 thread and over 25 cycles. From its name it composites the 2D layers of the two 256x192 screens; I did not read the renderer to establish how often or at what resolution it runs, so the per-frame cost is not modelled.

### 6.2 Compute renderer shaders (33 shaders)

Only `ClearIndirectWorkCount_` links (1 cycle estimate, 2 threads; in the first run it crashed the probe with rc -11, a later run with the probe modified to skip dispatch linked it OK, so the stored record has rc -11 but the task log shows the link). The other 32 are rejected by Mesa's GLSL front end after the GLES 3.1 rewrite:

{t_mc}

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
| `video_shader_enable` | keep `false` as the default; opt in per system | p50 libretro shader is {fmt(es['p50'])} cycles, but {es['threads1']} of {es['n']} run half-occupancy and the maximum is {es['max']/es['p50']:.0f}x the median |
| `video_smooth` | keep `false` | not covered by this data; unchanged |
| `video_threaded` / `video_hard_sync` | keep `false` | no shader data on this; unchanged |
| Shader budget | sum of output-resolution passes ≤ ~{B['cycles_per_pixel_50pct']:.0f} cycles | section 4, my 50% rule |
| Pixel-art scalers | avoid `xbrz` 4x-6x and `nnedi3` | spills dominate their cost (section 3.1) |
| Menu | disable the XMB snow/bokeh pipeline | 5.2 |

### 8.2 Per-system shaders (G52 estimates; the emulator's own cost is not included)

| System family | Source res (typical) | Recommended | Fallback | Never |
|---|---|---|---|---|
| NES, SNES, Genesis, PC Engine, Master System | 256x224 and similar | `zfast_crt` ({fmt(frag(ES['crt/shaders/zfast_crt.glsl'])['cycles'])}) or `crt-pi` ({fmt(frag(ES['crt/shaders/crt-pi.glsl'])['cycles'])}) | `sharp-bilinear` ({fmt(frag(ES['interpolation/shaders/sharp-bilinear.glsl'])['cycles'])}) | `crt-lottes`, `xbrz` |
| Game Boy, Game Boy Color | 160x144 | `lcd3x` ({fmt(frag(ES['handheld/shaders/lcd3x.glsl'])['cycles'])}) or `zfast_lcd` ({fmt(frag(ES['handheld/shaders/zfast_lcd.glsl'])['cycles'])}) + `gbc-color` ({fmt(frag(ES['handheld/shaders/color/gbc-color.glsl'])['cycles'])}) | `sharp-bilinear` | `lcd-cgwg` multi-pass presets |
| Game Boy Advance, Neo Geo Pocket, WonderSwan | 240x160 | `gba-color` ({fmt(frag(ES['handheld/shaders/color/gba-color.glsl'])['cycles'])}) alone, or with `zfast_lcd` | `sharp-bilinear` | `xbrz`, `jinc2` |
| Arcade (FBNeo, MAME-class) | 320x240 and similar | `crt-pi` or `zfast_crt` | `crt-easymode` ({fmt(frag(ES['crt/shaders/crt-easymode.glsl'])['cycles'])}, 1 thread) if the core is light | `crt-royale`, `crt-lottes` |
| Nintendo DS | 2 x 256x192 | no shader; software renderer | `sharp-bilinear` | melonDS compute renderer |
| PS1, N64, Dreamcast, PSP (3D) | varies | no shader; the core's GL cost is unmeasured here | at most `sharp-bilinear` | any full-screen filter over ~5 cycles until profiled on the device |

Order of preference inside a row is by increasing GPU cost, not by looks.

## 9. What this cannot tell us

- **No execution.** Nothing ran on a G52. Cycle counts are compiler estimates; actual time depends on scheduling, caches and driver behaviour I cannot see.
- **No memory bandwidth.** Texture fetches are counted as issue slots only. A 720x720 RGBA target at 60 fps is ~124 MB/s per full-screen write ({B['pixels']}x4 bytes x 60 = {B['pixels']*4*60/1e6:.0f} MB/s) before reads, tiling, blending or LPDDR contention; none of that is in these numbers. Mali is tile-based, which helps; I did not model it.
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
| `build/work/gpu-shim/harness/{{env.sh,env-control.sh,run_all.sh,corpus.py,shprobe.c}}` | the harness (written by the prior run) |
| `build/work/gpu-shim/report/analyze.py` | aggregation; writes `report/summary.json` |
| `build/work/gpu-shim/report/build_doc.py` | renders this document from `summary.json` and the results |
| `build/work/gpu-shim/report/summary.json` | derived summary used for every number above |
"""
out = REPO / "docs/research/05-gpu-shim.md"
out.write_text(doc)
print("wrote", out, len(doc.splitlines()), "lines")
