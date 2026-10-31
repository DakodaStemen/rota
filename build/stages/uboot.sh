#!/usr/bin/env bash
# Stage uboot: crackerjacques/u-boot-ums512 @ pinned sha + 5 Armbian patches (armbian/build PR #10813),
# ARCH=arm on EVERY make pass, wrapped in a DHTB container.
# Output: out/uboot/uboot_dhtb.img (SD p1 'uboot'), u-boot-dtb.bin, pm_bootcode.bin
. "$(dirname "$0")/../lib/common.sh"
setup_ccache
PATCHES=("$B"/patches/uboot/*.patch)
stage_begin uboot "$UBOOT_SHA $UBOOT_DEFCONFIG $UBOOT_DT $(file_hash "${PATCHES[@]}" "$B/lib/dhtb-wrap.py" "$0")" "$OUT/uboot/uboot_dhtb.img"
GITDIR="$CACHE/u-boot-ums512.git"
[[ -d "$GITDIR" ]] || git clone --bare --filter=blob:none "$UBOOT_REPO" "$GITDIR"
git -C "$GITDIR" cat-file -e "$UBOOT_SHA^{commit}" 2>/dev/null || git -C "$GITDIR" fetch origin "$UBOOT_SHA"
D="$WORK/uboot"; rm -rf "$D"; mkdir -p "$D" "$OUT/uboot"
git -C "$GITDIR" archive "$UBOOT_SHA" | tar -x -C "$D"
cd "$D"
for p in "${PATCHES[@]}"; do say "patch $(basename "$p")"; patch -p1 --no-backup-if-mismatch < "$p"; done
# GCC>=15 defaults to C23 (bool is a keyword): force gnu11 via KCFLAGS, no source patch needed
MK=(make ARCH=arm CROSS_COMPILE="$CROSS" KCFLAGS=-std=gnu11 -j"$JOBS")
"${MK[@]}" "$UBOOT_DEFCONFIG"
"${MK[@]}" DEVICE_TREE="$UBOOT_DT" u-boot-dtb.bin
[[ -f u-boot-dtb.bin ]] || die "u-boot-dtb.bin not produced"
python3 -I "$B/lib/dhtb-wrap.py" u-boot-dtb.bin uboot_dhtb.img
cp u-boot-dtb.bin uboot_dhtb.img "$OUT/uboot/"
python3 -I "$B/lib/check-dhtb.py" "$OUT/uboot/uboot_dhtb.img" - "$UBOOT_MAX_PAYLOAD"
# pm_bootcode.bin: CM4 boot stub from GPL source (base618 scripts/build-pm-bootcode.sh, MIT)
sh "$ROOT/upstream/base618/scripts/build-pm-bootcode.sh" "$D" "$OUT/uboot/pm_bootcode.bin" || say "pm_bootcode not built (non-fatal)"
( cd "$OUT/uboot" && sha256sum * | tee SHA256SUMS )
stage_end
