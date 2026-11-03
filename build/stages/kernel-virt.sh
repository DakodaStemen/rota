#!/usr/bin/env bash
# Stage kernel-virt: the SAME Rotate OS kernel source as stages/kernel.sh (pinned SHA + patches/kernel/*.patch,
# ums512_defconfig + kernel/rotate.config + kernel/fragments.d/*.config) plus kernel/virt.d/*.config, which adds
# only what the QEMU aarch64 "virt" machine needs (virtio-pci/blk/net/gpu, generic PCIe host, PL011, PL031).
# Used by qemu/run-vm.sh -k rotate. Separate work dirs (work/kernel-virt-*) so it never disturbs the device kernel build.
# Output: out/kernel-virt/{Image,config,release,SHA256SUMS}   (no dtb: QEMU generates it; no modules: all built in)
. "$(dirname "$0")/../lib/common.sh"
setup_ccache
SRC="$ROOT/upstream/linux-mainline-sprd"
shopt -s nullglob
FRAGS=("$B/kernel/rotate.config" "$B"/kernel/fragments.d/*.config "$B"/kernel/virt.d/*.config)
PATCHES=("$B"/patches/kernel/*.patch)
stage_begin kernel-virt "$KERNEL_SHA $KERNEL_BASE_DEFCONFIG $(file_hash "${FRAGS[@]}" "${PATCHES[@]}" "$0")" "$OUT/kernel-virt/Image"
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$KERNEL_SHA" ]] || die "$SRC is not at pinned $KERNEL_SHA"
[[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]] || die "$SRC has tracked modifications"
SRC_UP="$SRC"; SRC="$WORK/kernel-virt-src"
rm -rf "$SRC"; mkdir -p "$SRC"
git -C "$SRC_UP" archive "$KERNEL_SHA" | tar -x -C "$SRC"
for p in "${PATCHES[@]}"; do say "patch $(basename "$p")"; patch -d "$SRC" -p1 --no-backup-if-mismatch < "$p"; done
O="$WORK/kernel-virt-build"; rm -rf "$O"; mkdir -p "$O" "$OUT/kernel-virt"
CC_="ccache ${CROSS}gcc"
MK=(make -C "$SRC" O="$O" ARCH=arm64 CROSS_COMPILE="$CROSS" CC="$CC_" HOSTCC="ccache gcc" -j"$JOBS")
"${MK[@]}" "$KERNEL_BASE_DEFCONFIG"
say "merging fragments: ${FRAGS[*]}"
( cd "$O" && ARCH=arm64 KCONFIG_CONFIG="$O/.config" "$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "${FRAGS[@]}" )
"${MK[@]}" olddefconfig
python3 -I - "$O/.config" "${FRAGS[@]}" <<'PY'
import re, sys
have = {}
for l in open(sys.argv[1]).read().splitlines():
    m = re.match(r'(CONFIG_\w+)=(.*)', l)
    if m: have[m[1]] = m[2]
    m = re.match(r'# (CONFIG_\w+) is not set', l)
    if m: have[m[1]] = 'n'
# later fragments override earlier ones (virt.d re-enables symbols the trim fragment turned off)
want = {}
src = {}
for f in sys.argv[2:]:
    for l in open(f):
        l = l.strip()
        m = re.match(r'(CONFIG_\w+)=(.*)', l)
        if m: want[m[1]] = m[2]; src[m[1]] = f; continue
        m = re.match(r'# (CONFIG_\w+) is not set', l)
        if m: want[m[1]] = 'n'; src[m[1]] = f
bad = 0
for k, v in want.items():
    if have.get(k, 'n') != v:
        print(f"CONFIG MISMATCH {k}: wanted {v} got {have.get(k, 'unset')} ({src[k]})", file=sys.stderr); bad = 1
sys.exit(bad)
PY
"${MK[@]}" Image
REL="$(cat "$O/include/config/kernel.release")"
cp "$O/arch/arm64/boot/Image" "$OUT/kernel-virt/Image"
cp "$O/.config" "$OUT/kernel-virt/config"
echo "$REL" > "$OUT/kernel-virt/release"
( cd "$OUT/kernel-virt" && sha256sum Image config release | tee SHA256SUMS )
ccache -s | head -8 >&2 || true
stage_end
