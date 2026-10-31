#!/usr/bin/env python3
"""Aggregate build/work/gpu-shim/results/*.jsonl into report/summary.json and print GFM tables.
Run: python3 -I build/work/gpu-shim/report/analyze.py   (from the repo root; read-only on results/)

Conventions
- "compiled" = rc==0 (shprobe linked and drew; the Bifrost backend ran, stats present).
- A record can hold several FRAGMENT stat lines (driver shader variants). We use the MAX cycles variant.
- "MESA: error: export failed" is NOT a failure: see the doc (pan_fence.c, drmSyncobjExportSyncFile under drm-shim).
"""
import json, collections, re, statistics, sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent / "results"
OUT = Path(__file__).resolve().parent / "summary.json"

def load(n):
    return [json.loads(l) for l in open(R / n)]

def frag(r):
    f = [s for s in r["stats"] if s.get("stage") == "FRAGMENT" and "cycles" in s]
    return max(f, key=lambda s: s["cycles"]) if f else None

def real_errors(r):
    """err lines minus the fence message, COMPILE_LOG warnings and pci noise."""
    out = []
    for l in r["err"].splitlines():
        l = l.strip()
        if not l or l == "MESA: error: export failed" or "warning:" in l or l.startswith("COMPILE_LOG") or "libEGL warning" in l:
            continue
        out.append(l)
    return out

def err_class(r):
    for l in r["err"].splitlines():
        if "error" in l.lower() and not l.startswith("COMPILE_FAIL") and "export failed" not in l:
            l = re.sub(r"^\d+:\d+\(\d+\):\s*", "", l.strip())
            return re.sub(r"`[^`]*'", "`X'", l)[:100]
    return "(no error text)"

def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p / 100 * (len(v) - 1))))]

S = {}
files = {"G52 ES3.1 (glsl)": "glsl-all.jsonl", "G52 desktop GL3.1 (glsl)": "glsl-all.desktop.jsonl",
         "llvmpipe control (glsl)": "glsl-all.llvmpipe.jsonl", "RA stock ES": "ra-stock.jsonl",
         "RA stock desktop": "ra-stock.desktop.jsonl", "melonDS GL": "melonds-gl.jsonl", "crt subset": "glsl-crt.jsonl"}
S["corpora"] = {}
for label, fn in files.items():
    rs = load(fn)
    ok = [r for r in rs if r["rc"] == 0]
    S["corpora"][label] = dict(file=fn, total=len(rs), rc0=len(ok), failed=len(rs) - len(ok),
                               rc_hist=dict(collections.Counter(r["rc"] for r in rs)),
                               with_frag_stats=sum(1 for r in ok if frag(r)),
                               export_failed_msgs=sum(1 for r in rs if "export failed" in r["err"]))
# failure classes
S["fail_classes"] = {}
for label in ["G52 ES3.1 (glsl)", "G52 desktop GL3.1 (glsl)", "RA stock ES"]:
    rs = load(files[label])
    c = collections.Counter(err_class(r) for r in rs if r["rc"] != 0)
    S["fail_classes"][label] = c.most_common()

# cost stats over G52 ES3.1 compiled
es = [r for r in load("glsl-all.jsonl") if r["rc"] == 0 and frag(r)]
cy = [frag(r)["cycles"] for r in es]
S["es"] = dict(n=len(es), p10=pct(cy, 10), p25=pct(cy, 25), p50=pct(cy, 50), p75=pct(cy, 75), p90=pct(cy, 90), p99=pct(cy, 99), max=max(cy),
               threads1=sum(1 for r in es if frag(r)["threads"] == 1), threads2=sum(1 for r in es if frag(r)["threads"] == 2),
               spills=sum(1 for r in es if frag(r)["spills"] > 0), loops=sum(1 for r in es if frag(r)["loops"] > 0),
               regs_gt32=sum(1 for r in es if frag(r)["regs"] > 32))
S["es"]["cat_counts"] = dict(collections.Counter(err_class(r) for r in load("glsl-all.jsonl") if r["rc"] != 0).most_common())
S["es"]["buckets"] = {k: sum(1 for c in cy if lo <= c < hi) for k, (lo, hi) in
                      {"<=2": (0, 2.0001), "2-5": (2.0001, 5.0001), "5-10": (5.0001, 10.0001), "10-20": (10.0001, 20.0001),
                       "20-50": (20.0001, 50.0001), "50-100": (50.0001, 100.0001), ">100": (100.0001, 1e9)}.items()}
S["top20_es"] = [dict(name=r["name"], cycles=frag(r)["cycles"], inst=frag(r)["inst"], tex=frag(r)["tex"], arith=frag(r)["arith"], ldst=frag(r)["ldst"],
                      regs=frag(r)["regs"], threads=frag(r)["threads"], spills=frag(r)["spills"], fills=frag(r)["fills"])
                 for r in sorted(es, key=lambda r: -frag(r)["cycles"])[:20]]
S["spill_worst_es"] = [dict(name=r["name"], cycles=frag(r)["cycles"], spills=frag(r)["spills"], fills=frag(r)["fills"])
                       for r in sorted(es, key=lambda r: -frag(r)["spills"])[:10]]

# glsl vs glcore (ES path vs desktop GL path) on the same shader
D = {r["name"]: r for r in load("glsl-all.desktop.jsonl") if r["rc"] == 0 and frag(r)}
E = {r["name"]: r for r in es}
both = sorted(set(D) & set(E))
diffs = [(n, frag(E[n])["cycles"], frag(D[n])["cycles"]) for n in both]
S["gl_vs_es"] = dict(both=len(both), only_es=len(set(E) - set(D)), only_desktop=len(set(D) - set(E)),
                     identical_cycles=sum(1 for _, a, b in diffs if abs(a - b) < 1e-6),
                     es_cheaper=sum(1 for _, a, b in diffs if a < b - 1e-6), desktop_cheaper=sum(1 for _, a, b in diffs if b < a - 1e-6),
                     median_ratio_desktop_over_es=statistics.median(b / a for _, a, b in diffs if a > 0),
                     desktop_threads1=sum(1 for n in D if frag(D[n])["threads"] == 1), es_threads1=sum(1 for n in E if frag(E[n])["threads"] == 1),
                     desktop_spill=sum(1 for n in D if frag(D[n])["spills"] > 0), es_spill=sum(1 for n in E if frag(E[n])["spills"] > 0))
S["gl_vs_es"]["biggest_diffs"] = [dict(name=n, es=a, desktop=b) for n, a, b in sorted(diffs, key=lambda t: -abs(t[2] - t[1]))[:8]]
S["desktop_only_failures_es_ok"] = sorted(set(E) - set(D))[:50]

# stock RetroArch pipeline shaders
S["ra_stock"] = {}
for k, fn in [("es", "ra-stock.jsonl"), ("desktop", "ra-stock.desktop.jsonl")]:
    S["ra_stock"][k] = [dict(name=r["name"], rc=r["rc"], cycles=(frag(r) or {}).get("cycles"), inst=(frag(r) or {}).get("inst"),
                             regs=(frag(r) or {}).get("regs"), threads=(frag(r) or {}).get("threads"), spills=(frag(r) or {}).get("spills"),
                             err=err_class(r) if r["rc"] else "") for r in load(fn)]
# CRT subset
S["crt"] = [dict(name=r["name"], cycles=frag(r)["cycles"], inst=frag(r)["inst"], tex=frag(r)["tex"], regs=frag(r)["regs"], threads=frag(r)["threads"])
            for r in load("glsl-crt.jsonl") if frag(r)]
# melonDS
S["melonds_gl"] = [dict(name=r["name"], **{k: frag(r)[k] for k in ("cycles", "inst", "arith", "tex", "ldst", "regs", "threads", "spills")}) for r in load("melonds-gl.jsonl") if frag(r)]
mc = json.load(open(R / "melonds-compute.json"))
S["melonds_compute_raw_keys"] = sorted(mc[0].keys())
S["melonds_compute"] = [dict(name=m["name"], es_rc=m["es_rc"], es_stats=m["es_stats"], es_err=(m["es_err"] or [""])[0][:100]) for m in mc]

# multipass-aware families (by directory)
fam = collections.defaultdict(list)
for r in es:
    fam[r["name"].split("/")[0]].append(frag(r)["cycles"])
S["families_es"] = sorted(((k, len(v), statistics.median(v), max(v)) for k, v in fam.items()), key=lambda t: -t[3])

# melonDS compute: compiled = got Bifrost stats; rc: 0/-11 etc. as recorded
S["melonds_compute_summary"] = dict(total=len(mc), with_stats=sum(1 for m in mc if m["es_stats"]),
    errs=collections.Counter(re.sub(r"^\d+:\d+\(\d+\):\s*", "", (m["es_err"] or ["-"])[0])[:70] for m in mc if not m["es_stats"]).most_common(6))
L = load("glsl-all.llvmpipe.jsonl"); G = load("glsl-all.jsonl")
lf = {r["name"] for r in L if r["rc"] != 0}; gf = {r["name"] for r in G if r["rc"] != 0}
S["llvmpipe_control"] = dict(llvmpipe_fail=len(lf), g52_fail=len(gf), both=len(lf & gf), g52_only=sorted(gf - lf), llvmpipe_only=sorted((r["name"], r["rc"]) for r in L if r["name"] in lf - gf))
# frame budget (assumptions are in the doc)
px = 720 * 720; clk = 850e6; cores = 2; fps = 60
S["budget"] = dict(pixels=px, clock_hz=clk, cores=cores, fps=fps, core_cycles_per_frame=clk * cores / fps,
                   cycles_per_pixel_100pct=clk * cores / fps / px, cycles_per_pixel_50pct=clk * cores / fps / px * 0.5)
OUT.write_text(json.dumps(S, indent=1))
print("wrote", OUT)
