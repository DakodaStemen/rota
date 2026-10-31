#!/usr/bin/env bash
# Stage kernel: thwonp/linux-mainline-sprd rotate-fixes @ pinned sha, out-of-tree (O=) build so
# upstream/ stays untouched. ums512_defconfig + kernel/rotate.config + kernel/fragments.d/*.config.
# Output: out/kernel/{Image,ums512-rg-rotate.dtb,config,modules/(lib/modules/<rel>),release,SHA256SUMS}
. "$(dirname "$0")/../lib/common.sh"
setup_ccache
SRC="$ROOT/upstream/linux-mainline-sprd"
shopt -s nullglob
FRAGS=("$B/kernel/rotate.config" "$B"/kernel/fragments.d/*.config)
PATCHES=("$B"/patches/kernel/*.patch)
stage_begin kernel "$KERNEL_SHA $KERNEL_BASE_DEFCONFIG $(file_hash "${FRAGS[@]}" "${PATCHES[@]}" "$0")" "$OUT/kernel/Image" "$OUT/kernel/ums512-rg-rotate.dtb"
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$KERNEL_SHA" ]] || die "$SRC is not at pinned $KERNEL_SHA"
[[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]] || die "$SRC has tracked modifications"
# Patched work copy: pristine export of the pinned SHA + patches/kernel/*.patch. upstream/ is never edited.
SRC_UP="$SRC"; SRC="$WORK/kernel-src"
rm -rf "$SRC"; mkdir -p "$SRC"
git -C "$SRC_UP" archive "$KERNEL_SHA" | tar -x -C "$SRC"
for p in "${PATCHES[@]}"; do say "patch $(basename "$p")"; patch -d "$SRC" -p1 --no-backup-if-mismatch < "$p"; done
O="$WORK/kernel-build"; rm -rf "$O"; mkdir -p "$O" "$OUT/kernel"
CC_="ccache ${CROSS}gcc"
MK=(make -C "$SRC" O="$O" ARCH=arm64 CROSS_COMPILE="$CROSS" CC="$CC_" HOSTCC="ccache gcc" -j"$JOBS")
"${MK[@]}" "$KERNEL_BASE_DEFCONFIG"
say "merging fragments: ${FRAGS[*]}"
( cd "$O" && ARCH=arm64 KCONFIG_CONFIG="$O/.config" "$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "${FRAGS[@]}" ) 
"${MK[@]}" olddefconfig
# verify every requested symbol landed (merge_config only warns)
python3 -I - "$O/.config" "${FRAGS[@]}" <<'PY'
import re, sys
cfg = open(sys.argv[1]).read().splitlines()
have = {}
for l in cfg:
    m = re.match(r'(CONFIG_\w+)=(.*)', l)
    if m: have[m[1]] = m[2]
    m = re.match(r'# (CONFIG_\w+) is not set', l)
    if m: have[m[1]] = 'n'
bad = 0
for f in sys.argv[2:]:
    for l in open(f):
        l = l.strip()
        m = re.match(r'(CONFIG_\w+)=(.*)', l)
        if m: want = (m[1], m[2])
        else:
            m = re.match(r'# (CONFIG_\w+) is not set', l)
            if not m: continue
            want = (m[1], 'n')
        if have.get(want[0], 'n') != want[1]:
            print(f"CONFIG MISMATCH {want[0]}: wanted {want[1]} got {have.get(want[0], 'unset')} ({f})", file=sys.stderr); bad = 1
sys.exit(bad)
PY
"${MK[@]}" Image dtbs modules
REL="$(cat "$O/include/config/kernel.release")"
rm -rf "$OUT/kernel/modules"; mkdir -p "$OUT/kernel/modules"
"${MK[@]}" modules_install INSTALL_MOD_PATH="$OUT/kernel/modules" INSTALL_MOD_STRIP=1
rm -f "$OUT"/kernel/modules/lib/modules/*/{build,source}
cp "$O/arch/arm64/boot/Image" "$OUT/kernel/Image"
cp "$O/arch/arm64/boot/dts/$KERNEL_DTB" "$OUT/kernel/ums512-rg-rotate.dtb"
cp "$O/.config" "$OUT/kernel/config"
echo "$REL" > "$OUT/kernel/release"
grep -E '^CONFIG_ANDROID_BINDER(FS|_IPC)=' "$O/.config" | tee "$OUT/kernel/binder.txt"
( cd "$OUT/kernel" && sha256sum Image ums512-rg-rotate.dtb config release | tee SHA256SUMS )
ccache -s | head -8 >&2 || true
stage_end
