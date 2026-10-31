#!/usr/bin/env python3
"""Download aarch64 SDL2/SDL2_ttf/zlib dev packages from ports.ubuntu.com and unpack them (dpkg -x, no install,
no apt config change) into a directory usable as ROTATE_AARCH64_SYSROOT for cmake/aarch64-toolchain.cmake.

usage: fetch-aarch64-sysroot.py OUT_DIR [suite]     (suite defaults to this host's VERSION_CODENAME)
libc/libstdc++/libgcc come from the host's g++-aarch64-linux-gnu cross toolchain and are skipped.
"""
import io, lzma, os, re, subprocess, sys, urllib.request

ROOTS = ["libsdl2-dev", "libsdl2-ttf-dev", "zlib1g-dev"]
SKIP = {"libc6", "libc6-dev", "libgcc-s1", "libstdc++6", "gcc-15-base", "gcc-14-base", "gcc-16-base", "libgcc-15-dev", "debconf", "dpkg", "perl-base", "install-info"}
BASE = "http://ports.ubuntu.com/ubuntu-ports"


def suite_default():
    for line in open("/etc/os-release"):
        if line.startswith("VERSION_CODENAME="):
            return line.split("=")[1].strip()
    raise SystemExit("pass a suite name")


def load_index(suite):
    pk = {}
    for pocket in (suite, suite + "-updates", suite + "-security"):
        for comp in ("main", "universe"):
            url = f"{BASE}/dists/{pocket}/{comp}/binary-arm64/Packages.xz"
            try:
                data = lzma.decompress(urllib.request.urlopen(url, timeout=60).read()).decode()
            except Exception as e:  # pocket may not exist yet
                print("skip", url, e, file=sys.stderr)
                continue
            for block in data.split("\n\n"):
                f = dict(re.findall(r"^([A-Za-z-]+): (.*)$", block, re.M))
                if "Package" in f:
                    pk[f["Package"]] = f  # later pockets override
    return pk


def deps(f):
    out = []
    for fld in ("Depends", "Pre-Depends"):
        for part in f.get(fld, "").split(","):
            name = part.strip().split("|")[0].strip().split(" ")[0].split(":")[0]
            if name:
                out.append(name)
    return out


def main():
    out = os.path.abspath(sys.argv[1])
    suite = sys.argv[2] if len(sys.argv) > 2 else suite_default()
    idx = load_index(suite)
    want, todo = [], list(ROOTS)
    while todo:
        n = todo.pop()
        if n in want or n in SKIP or n not in idx:
            continue
        want.append(n)
        todo += deps(idx[n])
    os.makedirs(out, exist_ok=True)
    dl = os.path.join(out, ".debs")
    os.makedirs(dl, exist_ok=True)
    for n in sorted(want):
        fn = os.path.join(dl, os.path.basename(idx[n]["Filename"]))
        if not os.path.exists(fn):
            urllib.request.urlretrieve(f"{BASE}/{idx[n]['Filename']}", fn)
        subprocess.check_call(["dpkg", "-x", fn, out])
    print(f"unpacked {len(want)} packages into {out}")


main()
